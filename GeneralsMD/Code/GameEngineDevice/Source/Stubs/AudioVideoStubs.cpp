/**
 * XAudio2 + Media Foundation implementation of Miles Sound System (AIL_*) API.
 * Bink video stubs remain as no-ops.
 */
#include "Stubs/bink.h"
#ifdef _WIN32
#include <windows.h>
#include <mmreg.h>
#endif
#include "Stubs/mss/mss.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifdef _WIN32
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
using Microsoft::WRL::ComPtr;
#endif

// ── Bink video stubs ───────────────────────────────────────────────────────
BINK *__stdcall BinkOpen(const char*, unsigned int) { return NULL; }
void __stdcall BinkSetSoundTrack(unsigned int, unsigned int*) {}
int __stdcall BinkSetSoundSystem(SndOpenCallback, uintptr_t) { return 0; }
void *__stdcall BinkOpenDirectSound(uintptr_t) { return NULL; }
void __stdcall BinkClose(BINK*) {}
int __stdcall BinkWait(BINK*) { return 0; }
int __stdcall BinkDoFrame(BINK*) { return 0; }
int __stdcall BinkCopyToBuffer(BINK*, void*, int, unsigned int, unsigned int, unsigned int, unsigned int) { return 0; }
void __stdcall BinkSetVolume(BINK*, unsigned int, int) {}
void __stdcall BinkNextFrame(BINK*) {}
void __stdcall BinkGoto(BINK*, unsigned int, int) {}

// ── XAudio2 globals ────────────────────────────────────────────────────────
static ComPtr<IXAudio2> g_xa;
static IXAudio2MasteringVoice* g_master = nullptr;
static bool g_inited = false;
static DIG_DRIVER g_drvData;
static HDIGDRIVER g_drv = nullptr;
static bool g_mfInited = false;

enum { MAX_SAMPLES = 256, MAX_3D = 64, MAX_STREAMS = 8 };

// XAudio2 voice callback that fires the Miles EOS callback when a buffer finishes.
// Uses a generic function pointer + context to avoid forward-declaration issues.
typedef void (*EosDispatchFn)(void* ctx);

struct VoiceCallback : IXAudio2VoiceCallback {
    EosDispatchFn dispatch;
    void* ctx;
    VoiceCallback() : dispatch(nullptr), ctx(nullptr) {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override {
        if (dispatch) dispatch(ctx);
    }
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

static void dispatch2D(void* ctx);
static void dispatch3D(void* ctx);

struct XSample {
    IXAudio2SourceVoice* voice;
    void* userData[4];
    float vol, pan;
    int rate, loopCount;
    bool inUse;
    WAVEFORMATEX wfx;
    XAUDIO2_BUFFER buf;
    AIL_sample_callback eosCb;
    VoiceCallback vcb;
};
static XSample g_smp[MAX_SAMPLES];

struct X3DSample {
    XSample s;
    float px,py,pz, minD,maxD;
    void* ud[4];
    bool inUse;
    AIL_3dsample_callback eosCb3D;
};
static X3DSample g_3d[MAX_3D];

struct XStream {
    IXAudio2SourceVoice* voice;
    unsigned char* pcm; unsigned int pcmSz;
    float vol, pan; int loopCount;
    bool inUse, playing, paused;
    WAVEFORMATEX wfx;
    AIL_stream_callback cb;
    char fname[260];
};
static XStream g_str[MAX_STREAMS];

// ── EOS dispatch ──────────────────────────────────────────────────────────
static void dispatch2D(void* ctx) {
    auto* s = (XSample*)ctx;
    if (s && s->eosCb) s->eosCb((HSAMPLE)s);
}
static void dispatch3D(void* ctx) {
    auto* s3 = (X3DSample*)ctx;
    if (s3 && s3->eosCb3D) s3->eosCb3D((H3DSAMPLE)s3);
}

// ── Helpers ────────────────────────────────────────────────────────────────
static void SetVoiceVol(IXAudio2SourceVoice* v, float vol) {
    if (v) v->SetVolume(sqrtf(vol) * 1.5f);
}

static bool ParseWAV(const void* f, WAVEFORMATEX* wfx, const void** d, unsigned int* sz) {
    auto p = (const unsigned char*)f;
    if (!f || memcmp(p,"RIFF",4) || memcmp(p+8,"WAVE",4)) return false;
    unsigned int fsz = *(unsigned int*)(p+4)+8, pos=12;
    bool gotFmt=false, gotData=false;
    while (pos+8 <= fsz) {
        unsigned int csz = *(unsigned int*)(p+pos+4);
        if (!memcmp(p+pos,"fmt ",4) && csz>=16) {
            memset(wfx,0,sizeof(*wfx));
            wfx->wFormatTag=*(unsigned short*)(p+pos+8);
            wfx->nChannels=*(unsigned short*)(p+pos+10);
            wfx->nSamplesPerSec=*(unsigned int*)(p+pos+12);
            wfx->nAvgBytesPerSec=*(unsigned int*)(p+pos+16);
            wfx->nBlockAlign=*(unsigned short*)(p+pos+20);
            wfx->wBitsPerSample=*(unsigned short*)(p+pos+22);
            gotFmt=true;
        } else if (!memcmp(p+pos,"data",4)) {
            if(d)*d=p+pos+8; if(sz)*sz=csz; gotData=true;
        }
        pos += 8+csz; if(csz&1) pos++;
    }
    return gotFmt && gotData;
}

static bool DecodeMF(const char* fn, WAVEFORMATEX* wfx, unsigned char** out, unsigned int* outSz) {
    if (!g_mfInited) { MFStartup(MF_VERSION); g_mfInited=true; }
    wchar_t wp[512]; MultiByteToWideChar(CP_ACP,0,fn,-1,wp,512);
    IMFSourceReader* rd=nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(wp,nullptr,&rd))) return false;
    IMFMediaType* mt=nullptr; MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);
    mt->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);
    mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);
    mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,44100);
    mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
    mt->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4);
    mt->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,176400);
    rd->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,mt);
    mt->Release();
    unsigned int total=0, cap=1024*1024;
    auto buf=(unsigned char*)malloc(cap);
    while(true) {
        DWORD fl=0; IMFSample* s=nullptr;
        if(FAILED(rd->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&fl,nullptr,&s))||(fl&MF_SOURCE_READERF_ENDOFSTREAM)){if(s)s->Release();break;}
        if(s){IMFMediaBuffer* mb=nullptr;s->ConvertToContiguousBuffer(&mb);
            if(mb){BYTE*d=nullptr;DWORD l=0;mb->Lock(&d,nullptr,&l);
                if(total+l>cap){cap=(total+l)*2;buf=(unsigned char*)realloc(buf,cap);}
                memcpy(buf+total,d,l);total+=l;mb->Unlock();mb->Release();}
            s->Release();}
    }
    rd->Release();
    if(!total){free(buf);return false;}
    wfx->wFormatTag=1;wfx->nChannels=2;wfx->nSamplesPerSec=44100;
    wfx->wBitsPerSample=16;wfx->nBlockAlign=4;wfx->nAvgBytesPerSec=176400;wfx->cbSize=0;
    *out=buf;*outSz=total;
    return true;
}

// ── Core AIL ───────────────────────────────────────────────────────────────
int __stdcall AIL_startup(void) {
    if(g_inited) return 1;
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(XAudio2Create(g_xa.GetAddressOf()))) return 0;
    if(FAILED(g_xa->CreateMasteringVoice(&g_master))){g_xa.Reset();return 0;}
    memset(g_smp,0,sizeof(g_smp));memset(g_3d,0,sizeof(g_3d));memset(g_str,0,sizeof(g_str));
    g_inited=true; return 1;
}
void __stdcall AIL_shutdown(void) {
    if(!g_inited) return;
    for(int i=0;i<MAX_SAMPLES;i++){if(g_smp[i].voice)g_smp[i].voice->DestroyVoice(); g_smp[i]={};}
    for(int i=0;i<MAX_3D;i++){if(g_3d[i].s.voice)g_3d[i].s.voice->DestroyVoice(); g_3d[i]={};}
    for(int i=0;i<MAX_STREAMS;i++){if(g_str[i].voice)g_str[i].voice->DestroyVoice(); if(g_str[i].pcm)free(g_str[i].pcm); g_str[i]={};}
    if(g_master){g_master->DestroyVoice();g_master=nullptr;}
    g_xa.Reset(); g_inited=false;
}
int __stdcall AIL_set_preference(unsigned int,int){return 0;}
void __stdcall AIL_lock(void){} void __stdcall AIL_unlock(void){}
char* __stdcall AIL_last_error(void){return(char*)"";}
char* __stdcall AIL_set_redist_directory(const char*){return(char*)"";}
unsigned long __stdcall AIL_get_timer_highest_delay(void){return 1;}
void __stdcall AIL_set_file_callbacks(AIL_file_open_callback,AIL_file_close_callback,AIL_file_seek_callback,AIL_file_read_callback){}
void __stdcall AIL_stop_timer(HTIMER){} void __stdcall AIL_release_timer_handle(HTIMER){}

// ── Driver ─────────────────────────────────────────────────────────────────
int __stdcall AIL_waveOutOpen(HDIGDRIVER*d,LPHWAVEOUT*,int,LPWAVEFORMAT){g_drv=&g_drvData;memset(g_drv,0,sizeof(*g_drv));if(d)*d=g_drv;return 1;}
void __stdcall AIL_waveOutClose(HDIGDRIVER){g_drv=nullptr;}
int __stdcall AIL_quick_startup(int,int,unsigned int,int,int){if(!AIL_startup())return 0;AIL_waveOutOpen(&g_drv,nullptr,0,nullptr);return 1;}
void __stdcall AIL_quick_handles(HDIGDRIVER*p,HMDIDRIVER*m,HDLSDEVICE*d){if(!g_drv)AIL_waveOutOpen(&g_drv,nullptr,0,nullptr);if(p)*p=g_drv;if(m)*m=nullptr;if(d)*d=nullptr;}
HAUDIO __stdcall AIL_quick_load_and_play(const char*,unsigned int,int){return nullptr;}
void __stdcall AIL_quick_unload(HAUDIO){} void __stdcall AIL_quick_set_volume(HAUDIO,float,float){}

// ── 2D Samples ─────────────────────────────────────────────────────────────
HSAMPLE __stdcall AIL_allocate_sample_handle(HDIGDRIVER){if(!g_inited)return nullptr;for(int i=0;i<MAX_SAMPLES;i++)if(!g_smp[i].inUse){auto*s=&g_smp[i];memset(s,0,sizeof(*s));new(&s->vcb)VoiceCallback();s->vcb.dispatch=dispatch2D;s->vcb.ctx=s;s->inUse=true;s->vol=1.0f;s->loopCount=1;return(HSAMPLE)s;}return nullptr;}
void __stdcall AIL_release_sample_handle(HSAMPLE h){auto s=(XSample*)h;if(!s)return;if(s->voice){s->voice->DestroyVoice();s->voice=nullptr;}s->inUse=false;}
void __stdcall AIL_init_sample(HSAMPLE h){auto s=(XSample*)h;if(!s)return;if(s->voice){s->voice->Stop();s->voice->FlushSourceBuffers();}s->vol=1.0f;s->pan=0;s->loopCount=1;}
int __stdcall AIL_set_sample_file(HSAMPLE h,const void*f,int){auto s=(XSample*)h;if(!s||!f||!g_xa)return 0;const void*d=nullptr;unsigned int sz=0;if(!ParseWAV(f,&s->wfx,&d,&sz))return 0;if(s->voice){s->voice->DestroyVoice();s->voice=nullptr;}if(FAILED(g_xa->CreateSourceVoice(&s->voice,&s->wfx,0,XAUDIO2_DEFAULT_FREQ_RATIO,&s->vcb)))return 0;memset(&s->buf,0,sizeof(s->buf));s->buf.AudioBytes=sz;s->buf.pAudioData=(const BYTE*)d;s->rate=s->wfx.nSamplesPerSec;return 1;}
int __stdcall AIL_set_named_sample_file(HSAMPLE h,const char*,const void*f,int,int b){return AIL_set_sample_file(h,f,b);}
void __stdcall AIL_start_sample(HSAMPLE h){auto s=(XSample*)h;if(!s||!s->voice)return;s->voice->Stop();s->voice->FlushSourceBuffers();
    // Miles: 1=play once, 0=infinite, N=play N times
    // XAudio2: 0=no loop, XAUDIO2_LOOP_INFINITE=infinite, N=loop N extra times
    // Default loopCount is 1 (set in alloc/init), game sets 0 for ambient loops
    if(s->loopCount==0) s->buf.LoopCount=XAUDIO2_LOOP_INFINITE;
    else if(s->loopCount==1) s->buf.LoopCount=0;
    else s->buf.LoopCount=s->loopCount-1;
    s->voice->SubmitSourceBuffer(&s->buf);SetVoiceVol(s->voice,s->vol);s->voice->Start();}
void __stdcall AIL_stop_sample(HSAMPLE h){auto s=(XSample*)h;if(s&&s->voice)s->voice->Stop();}
void __stdcall AIL_end_sample(HSAMPLE h){auto s=(XSample*)h;if(s&&s->voice){s->voice->Stop();s->voice->FlushSourceBuffers();}}
void __stdcall AIL_resume_sample(HSAMPLE h){auto s=(XSample*)h;if(s&&s->voice)s->voice->Start();}
void __stdcall AIL_set_sample_volume(HSAMPLE h,int v){auto s=(XSample*)h;if(s){s->vol=v/127.0f;if(s->voice)SetVoiceVol(s->voice,s->vol);}}
int __stdcall AIL_sample_volume(HSAMPLE h){auto s=(XSample*)h;return s?(int)(s->vol*127):0;}
void __stdcall AIL_set_sample_pan(HSAMPLE h,int p){auto s=(XSample*)h;if(s)s->pan=(p-64)/64.0f;}
int __stdcall AIL_sample_pan(HSAMPLE h){auto s=(XSample*)h;return s?(int)((s->pan+1)*64):64;}
void __stdcall AIL_set_sample_volume_pan(HSAMPLE h,float v,float p){auto s=(XSample*)h;if(s){s->vol=v;s->pan=p*2-1;if(s->voice)SetVoiceVol(s->voice,s->vol);}}
void __stdcall AIL_sample_volume_pan(HSAMPLE h,float*v,float*p){auto s=(XSample*)h;if(v)*v=s?s->vol:0;if(p)*p=s?(s->pan+1)*0.5f:0.5f;}
void __stdcall AIL_set_sample_playback_rate(HSAMPLE h,int r){auto s=(XSample*)h;if(s&&s->voice&&s->wfx.nSamplesPerSec)s->voice->SetFrequencyRatio((float)r/s->wfx.nSamplesPerSec);}
int __stdcall AIL_sample_playback_rate(HSAMPLE h){auto s=(XSample*)h;return s?s->rate:22050;}
void __stdcall AIL_set_sample_loop_count(HSAMPLE h,int c){auto s=(XSample*)h;if(s)s->loopCount=c;}
int __stdcall AIL_sample_loop_count(HSAMPLE h){auto s=(XSample*)h;return s?s->loopCount:1;}
void __stdcall AIL_sample_ms_position(HSAMPLE,long*t,long*c){if(t)*t=0;if(c)*c=0;}
void __stdcall AIL_set_sample_ms_position(HSAMPLE,int){}
void __stdcall AIL_set_sample_user_data(HSAMPLE h,unsigned int i,void*v){auto s=(XSample*)h;if(s&&i<4)s->userData[i]=v;}
void* __stdcall AIL_sample_user_data(HSAMPLE h,unsigned int i){auto s=(XSample*)h;return(s&&i<4)?s->userData[i]:nullptr;}
AIL_sample_callback __stdcall AIL_register_EOS_callback(HSAMPLE h,AIL_sample_callback cb){auto s=(XSample*)h;auto old=s?s->eosCb:nullptr;if(s)s->eosCb=cb;return old;}
HPROVIDER __stdcall AIL_set_sample_processor(HSAMPLE,SAMPLESTAGE,HPROVIDER){return nullptr;}
void __stdcall AIL_set_filter_sample_preference(HSAMPLE,const char*,const void*){}
void __stdcall AIL_get_DirectSound_info(HSAMPLE,AILLPDIRECTSOUND*a,AILLPDIRECTSOUNDBUFFER*b){if(a)*a=nullptr;if(b)*b=nullptr;}

// ── WAV info ───────────────────────────────────────────────────────────────
int __stdcall AIL_WAV_info(const void*d,AILSOUNDINFO*i){if(!d||!i)return 0;WAVEFORMATEX w;const void*pd;unsigned int ps;if(!ParseWAV(d,&w,&pd,&ps))return 0;i->format=w.wFormatTag;i->data_ptr=pd;i->data_len=ps;i->rate=w.nSamplesPerSec;i->bits=w.wBitsPerSample;i->channels=w.nChannels;i->samples=ps/w.nBlockAlign;i->block_size=w.nBlockAlign;i->initial_ptr=pd;return 1;}
int __stdcall AIL_decompress_ADPCM(const AILSOUNDINFO*,void**o,unsigned long*s){if(o)*o=nullptr;if(s)*s=0;return 0;}
void __stdcall AIL_mem_free_lock(void*p){if(p)free(p);}

// ── Streams (MP3 via Media Foundation) ─────────────────────────────────────
HSTREAM __stdcall AIL_open_stream(HDIGDRIVER,const char*fn,int){if(!g_inited||!fn)return nullptr;for(int i=0;i<MAX_STREAMS;i++)if(!g_str[i].inUse){auto st=&g_str[i];memset(st,0,sizeof(*st));if(!DecodeMF(fn,&st->wfx,&st->pcm,&st->pcmSz))return nullptr;if(FAILED(g_xa->CreateSourceVoice(&st->voice,&st->wfx))){free(st->pcm);st->pcm=nullptr;return nullptr;}strncpy(st->fname,fn,259);st->inUse=true;st->vol=1.0f;st->loopCount=1;return(HSTREAM)st;}return nullptr;}
HSTREAM __stdcall AIL_open_stream_by_sample(HDIGDRIVER d,HSAMPLE,const char*fn,int m){return AIL_open_stream(d,fn,m);}
void __stdcall AIL_start_stream(HSTREAM h){auto st=(XStream*)h;if(!st||!st->voice)return;st->voice->Stop();st->voice->FlushSourceBuffers();XAUDIO2_BUFFER b={};b.AudioBytes=st->pcmSz;b.pAudioData=st->pcm;b.LoopCount=(st->loopCount==0)?XAUDIO2_LOOP_INFINITE:0;st->voice->SubmitSourceBuffer(&b);SetVoiceVol(st->voice,st->vol);st->voice->Start();st->playing=true;}
void __stdcall AIL_close_stream(HSTREAM h){auto st=(XStream*)h;if(!st)return;if(st->voice){st->voice->Stop();st->voice->DestroyVoice();st->voice=nullptr;}if(st->pcm){free(st->pcm);st->pcm=nullptr;}st->inUse=false;}
void __stdcall AIL_pause_stream(HSTREAM h,int on){auto st=(XStream*)h;if(!st||!st->voice)return;if(on)st->voice->Stop();else st->voice->Start();}
void __stdcall AIL_set_stream_volume(HSTREAM h,int v){auto st=(XStream*)h;if(st){st->vol=v/127.0f;if(st->voice)SetVoiceVol(st->voice,st->vol);}}
int __stdcall AIL_stream_volume(HSTREAM h){auto st=(XStream*)h;return st?(int)(st->vol*127):0;}
void __stdcall AIL_set_stream_pan(HSTREAM,int){} int __stdcall AIL_stream_pan(HSTREAM){return 64;}
void __stdcall AIL_set_stream_playback_rate(HSTREAM,int){} int __stdcall AIL_stream_playback_rate(HSTREAM){return 44100;}
void __stdcall AIL_set_stream_loop_count(HSTREAM h,int c){auto st=(XStream*)h;if(st)st->loopCount=c;}
int __stdcall AIL_stream_loop_count(HSTREAM h){auto st=(XStream*)h;return st?st->loopCount:1;}
void __stdcall AIL_set_stream_loop_block(HSTREAM,int,int){}
void __stdcall AIL_stream_ms_position(HSTREAM h,S32*t,S32*c){auto st=(XStream*)h;if(t)*t=st?(S32)(st->pcmSz*1000ULL/176400):0;if(c)*c=0;}
void __stdcall AIL_set_stream_ms_position(HSTREAM,int){}
void __stdcall AIL_set_stream_volume_pan(HSTREAM h,float v,float){auto st=(XStream*)h;if(st){st->vol=v;if(st->voice)SetVoiceVol(st->voice,st->vol);}}
void __stdcall AIL_stream_volume_pan(HSTREAM h,float*v,float*p){auto st=(XStream*)h;if(v)*v=st?st->vol:0;if(p)*p=0.5f;}
AIL_stream_callback __stdcall AIL_register_stream_callback(HSTREAM h,AIL_stream_callback cb){auto st=(XStream*)h;auto old=st?st->cb:nullptr;if(st)st->cb=cb;return old;}

// ── 3D Providers ───────────────────────────────────────────────────────────
int __stdcall AIL_enumerate_3D_providers(HPROENUM*n,HPROVIDER*d,char**nm){if(!n)return 0;if(*n==HPROENUM_FIRST){if(d)*d=(HPROVIDER)1;if(nm)*nm=(char*)"Miles Fast 2D Positional Audio";*n=1;return 1;}if(*n==1){if(d)*d=(HPROVIDER)2;if(nm)*nm=(char*)"Dolby Surround";*n=2;return 1;}return 0;}
M3DRESULT __stdcall AIL_open_3D_provider(HPROVIDER){return M3D_NOERR;}
void __stdcall AIL_close_3D_provider(HPROVIDER){} void __stdcall AIL_set_3D_speaker_type(HPROVIDER,int){}
static h3DPOBJECT g_listener={};
H3DPOBJECT __stdcall AIL_open_3D_listener(HPROVIDER){return &g_listener;}
void __stdcall AIL_close_3D_listener(H3DPOBJECT){}
void __stdcall AIL_set_3D_position(H3DPOBJECT,float,float,float){}
void __stdcall AIL_set_3D_orientation(H3DPOBJECT,float,float,float,float,float,float){}
void __stdcall AIL_set_3D_velocity_vector(H3DSAMPLE,float,float,float){}
int __stdcall AIL_enumerate_filters(HPROENUM*,HPROVIDER*,char**){return 0;}

// ── 3D Samples ─────────────────────────────────────────────────────────────
H3DSAMPLE __stdcall AIL_allocate_3D_sample_handle(HPROVIDER){if(!g_inited)return nullptr;for(int i=0;i<MAX_3D;i++)if(!g_3d[i].inUse){auto*s3=&g_3d[i];memset(s3,0,sizeof(*s3));new(&s3->s.vcb)VoiceCallback();s3->s.vcb.dispatch=dispatch3D;s3->s.vcb.ctx=s3;s3->inUse=true;s3->s.inUse=true;s3->s.vol=1.0f;s3->s.loopCount=1;s3->minD=1;s3->maxD=1000;return(H3DSAMPLE)s3;}return nullptr;}
void __stdcall AIL_release_3D_sample_handle(H3DSAMPLE h){auto s=(X3DSample*)h;if(!s)return;if(s->s.voice){s->s.voice->DestroyVoice();s->s.voice=nullptr;}s->inUse=false;}
int __stdcall AIL_set_3D_sample_file(H3DSAMPLE h,const void*f){return AIL_set_sample_file((HSAMPLE)&((X3DSample*)h)->s,f,0);}
void __stdcall AIL_start_3D_sample(H3DSAMPLE h){AIL_start_sample((HSAMPLE)&((X3DSample*)h)->s);}
void __stdcall AIL_stop_3D_sample(H3DSAMPLE h){AIL_stop_sample((HSAMPLE)&((X3DSample*)h)->s);}
void __stdcall AIL_end_3D_sample(H3DSAMPLE h){AIL_end_sample((HSAMPLE)&((X3DSample*)h)->s);}
void __stdcall AIL_resume_3D_sample(H3DSAMPLE h){AIL_resume_sample((HSAMPLE)&((X3DSample*)h)->s);}
float __stdcall AIL_3D_sample_volume(H3DSAMPLE h){auto s=(X3DSample*)h;return s?s->s.vol:0;}
void __stdcall AIL_set_3D_sample_volume(H3DSAMPLE h,float v){auto s=(X3DSample*)h;if(s){s->s.vol=v;if(s->s.voice)SetVoiceVol(s->s.voice,v);}}
void __stdcall AIL_set_3D_sample_distances(H3DSAMPLE h,float mx,float mn){auto s=(X3DSample*)h;if(s){s->maxD=mx;s->minD=mn;}}
void __stdcall AIL_set_3D_sample_effects_level(H3DSAMPLE,float){} void __stdcall AIL_set_3D_sample_occlusion(H3DSAMPLE,float){}
void __stdcall AIL_set_3D_sample_loop_count(H3DSAMPLE h,unsigned int c){auto s=(X3DSample*)h;if(s)s->s.loopCount=c;}
unsigned int __stdcall AIL_3D_sample_loop_count(H3DSAMPLE h){auto s=(X3DSample*)h;return s?s->s.loopCount:1;}
void __stdcall AIL_set_3D_sample_offset(H3DSAMPLE,unsigned int){} unsigned int __stdcall AIL_3D_sample_offset(H3DSAMPLE){return 0;} int __stdcall AIL_3D_sample_length(H3DSAMPLE){return 0;}
void __stdcall AIL_set_3D_sample_playback_rate(H3DSAMPLE h,int r){AIL_set_sample_playback_rate((HSAMPLE)&((X3DSample*)h)->s,r);}
int __stdcall AIL_3D_sample_playback_rate(H3DSAMPLE h){auto s=(X3DSample*)h;return s?s->s.rate:22050;}
void __stdcall AIL_set_3D_user_data(H3DPOBJECT h,unsigned int i,void*v){for(int j=0;j<MAX_3D;j++)if((H3DPOBJECT)&g_3d[j]==h&&i<4){g_3d[j].ud[i]=v;return;}}
void* __stdcall AIL_3D_user_data(H3DSAMPLE h,unsigned int i){for(int j=0;j<MAX_3D;j++)if((H3DSAMPLE)&g_3d[j]==h&&i<4)return g_3d[j].ud[i];return nullptr;}
AIL_3dsample_callback __stdcall AIL_register_3D_EOS_callback(H3DSAMPLE h,AIL_3dsample_callback cb){auto s=(X3DSample*)h;auto old=s?s->eosCb3D:nullptr;if(s)s->eosCb3D=cb;return old;}

// ── Cleanup ────────────────────────────────────────────────────────────────
void MSS_cleanup(void){AIL_shutdown();}
int MSS_auto_cleanup(void){atexit(MSS_cleanup);return 0;}

// ── XAudio2 access for video audio ────────────────────────────────────────
// FFmpegVideoPlayer pulls the XAudio2 device from here to create its own
// source voice for movie soundtracks. With the Miles backend in play this
// was set up by MilesAudioManager::initDevice() calling AIL_startup(); under
// the SDL audio backend nobody ever calls AIL_startup(), so lazy-init it
// here on first request. AIL_startup() is idempotent and only allocates a
// master voice + the (unused) Miles sample tables, which is harmless.
IXAudio2* GetXAudio2Device() {
    if (!g_xa) {
        AIL_startup();
    }
    return g_xa.Get();
}
