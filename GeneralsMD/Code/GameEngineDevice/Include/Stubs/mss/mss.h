// Miles Sound System stub header — XAudio2 backend
#pragma once
#include <windows.h>
#include <mmsystem.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMPORTS
#define AILCALLBACK __stdcall

typedef struct h3DPOBJECT { unsigned int junk; } h3DPOBJECT;
typedef h3DPOBJECT *H3DPOBJECT;
typedef H3DPOBJECT H3DSAMPLE;
typedef struct _SAMPLE *HSAMPLE;
typedef struct _STREAM *HSTREAM;
typedef struct _DIG_DRIVER { char pad[168]; int emulated_ds; } DIG_DRIVER;
typedef DIG_DRIVER *HDIGDRIVER;
typedef struct _AUDIO *HAUDIO;
typedef struct _HMDIDRIVER *HMDIDRIVER;
typedef struct _HDLSDEVICE *HDLSDEVICE;
typedef void *HPROVIDER;
typedef int HTIMER;
typedef unsigned int HPROENUM;
typedef int M3DRESULT;
typedef void *AILLPDIRECTSOUND;
typedef void *AILLPDIRECTSOUNDBUFFER;
typedef unsigned long U32;
typedef long S32;
typedef float F32;

typedef struct _AILSOUNDINFO {
    int format; const void *data_ptr; unsigned int data_len;
    unsigned int rate; int bits; int channels;
    unsigned int samples; unsigned int block_size; const void *initial_ptr;
} AILSOUNDINFO;

typedef enum { DP_ASI_DECODER=0, DP_FILTER, DP_MERGE, N_SAMPLE_STAGES, SAMPLE_ALL_STAGES } SAMPLESTAGE;

typedef unsigned long(__stdcall *AIL_file_open_callback)(const char*, void**);
typedef void(__stdcall *AIL_file_close_callback)(void*);
typedef long(__stdcall *AIL_file_seek_callback)(void*, long, unsigned long);
typedef unsigned long(__stdcall *AIL_file_read_callback)(void*, void*, unsigned long);
typedef void(__stdcall *AIL_stream_callback)(HSTREAM);
typedef void(__stdcall *AIL_3dsample_callback)(H3DPOBJECT);
typedef void(__stdcall *AIL_sample_callback)(HSAMPLE);

#define HPROENUM_FIRST 0
#define M3D_NOERR 0
#define AIL_NO_ERROR 0
#define AIL_3D_2_SPEAKER 0
#define AIL_3D_HEADPHONE 1
#define AIL_3D_SURROUND 2
#define AIL_3D_4_SPEAKER 3
#define AIL_3D_51_SPEAKER 4
#define AIL_3D_71_SPEAKER 5
#define DIG_USE_WAVEOUT 15
#define AIL_LOCK_PROTECTION 18
#ifndef WAVE_FORMAT_IMA_ADPCM
#define WAVE_FORMAT_IMA_ADPCM 0x11
#endif
#define ENVIRONMENT_GENERIC 0
#define AIL_FILE_SEEK_BEGIN 0
#define AIL_FILE_SEEK_CURRENT 1
#define AIL_FILE_SEEK_END 2
#ifndef YES
#define YES 1
#endif
#ifndef NO
#define NO 0
#endif
#define AIL_MSS_version(a,b)
#define AIL_set_3D_object_user_data AIL_set_3D_user_data
#define AIL_3D_object_user_data AIL_3D_user_data
#define AIL_3D_open_listener AIL_open_3D_listener

// Core
IMPORTS int __stdcall AIL_startup(void);
IMPORTS void __stdcall AIL_shutdown(void);
IMPORTS int __stdcall AIL_set_preference(unsigned int, int);
IMPORTS void __stdcall AIL_lock(void);
IMPORTS void __stdcall AIL_unlock(void);
IMPORTS char* __stdcall AIL_last_error(void);
IMPORTS char* __stdcall AIL_set_redist_directory(const char*);
IMPORTS unsigned long __stdcall AIL_get_timer_highest_delay(void);
IMPORTS void __stdcall AIL_stop_timer(HTIMER);
IMPORTS void __stdcall AIL_release_timer_handle(HTIMER);
IMPORTS void __stdcall AIL_set_file_callbacks(AIL_file_open_callback,AIL_file_close_callback,AIL_file_seek_callback,AIL_file_read_callback);

// Driver
IMPORTS int __stdcall AIL_waveOutOpen(HDIGDRIVER*,LPHWAVEOUT*,int,LPWAVEFORMAT);
IMPORTS void __stdcall AIL_waveOutClose(HDIGDRIVER);
IMPORTS int __stdcall AIL_quick_startup(int,int,unsigned int,int,int);
IMPORTS void __stdcall AIL_quick_handles(HDIGDRIVER*,HMDIDRIVER*,HDLSDEVICE*);
IMPORTS HAUDIO __stdcall AIL_quick_load_and_play(const char*,unsigned int,int);
IMPORTS void __stdcall AIL_quick_unload(HAUDIO);
IMPORTS void __stdcall AIL_quick_set_volume(HAUDIO,float,float);

// 2D Samples
IMPORTS HSAMPLE __stdcall AIL_allocate_sample_handle(HDIGDRIVER);
IMPORTS void __stdcall AIL_release_sample_handle(HSAMPLE);
IMPORTS void __stdcall AIL_init_sample(HSAMPLE);
IMPORTS int __stdcall AIL_set_sample_file(HSAMPLE,const void*,int);
IMPORTS int __stdcall AIL_set_named_sample_file(HSAMPLE,const char*,const void*,int,int);
IMPORTS void __stdcall AIL_start_sample(HSAMPLE);
IMPORTS void __stdcall AIL_stop_sample(HSAMPLE);
IMPORTS void __stdcall AIL_end_sample(HSAMPLE);
IMPORTS void __stdcall AIL_resume_sample(HSAMPLE);
IMPORTS void __stdcall AIL_set_sample_volume(HSAMPLE,int);
IMPORTS int __stdcall AIL_sample_volume(HSAMPLE);
IMPORTS void __stdcall AIL_set_sample_pan(HSAMPLE,int);
IMPORTS int __stdcall AIL_sample_pan(HSAMPLE);
IMPORTS void __stdcall AIL_set_sample_volume_pan(HSAMPLE,float,float);
IMPORTS void __stdcall AIL_sample_volume_pan(HSAMPLE,float*,float*);
IMPORTS void __stdcall AIL_set_sample_playback_rate(HSAMPLE,int);
IMPORTS int __stdcall AIL_sample_playback_rate(HSAMPLE);
IMPORTS void __stdcall AIL_set_sample_loop_count(HSAMPLE,int);
IMPORTS int __stdcall AIL_sample_loop_count(HSAMPLE);
IMPORTS void __stdcall AIL_sample_ms_position(HSAMPLE,long*,long*);
IMPORTS void __stdcall AIL_set_sample_ms_position(HSAMPLE,int);
IMPORTS void __stdcall AIL_set_sample_user_data(HSAMPLE,unsigned int,void*);
IMPORTS void* __stdcall AIL_sample_user_data(HSAMPLE,unsigned int);
IMPORTS AIL_sample_callback __stdcall AIL_register_EOS_callback(HSAMPLE,AIL_sample_callback);
IMPORTS HPROVIDER __stdcall AIL_set_sample_processor(HSAMPLE,SAMPLESTAGE,HPROVIDER);
IMPORTS void __stdcall AIL_set_filter_sample_preference(HSAMPLE,const char*,const void*);
IMPORTS void __stdcall AIL_get_DirectSound_info(HSAMPLE,AILLPDIRECTSOUND*,AILLPDIRECTSOUNDBUFFER*);

// WAV
IMPORTS int __stdcall AIL_WAV_info(const void*,AILSOUNDINFO*);
IMPORTS int __stdcall AIL_decompress_ADPCM(const AILSOUNDINFO*,void**,unsigned long*);
IMPORTS void __stdcall AIL_mem_free_lock(void*);

// Streams
IMPORTS HSTREAM __stdcall AIL_open_stream(HDIGDRIVER,const char*,int);
IMPORTS HSTREAM __stdcall AIL_open_stream_by_sample(HDIGDRIVER,HSAMPLE,const char*,int);
IMPORTS void __stdcall AIL_start_stream(HSTREAM);
IMPORTS void __stdcall AIL_close_stream(HSTREAM);
IMPORTS void __stdcall AIL_pause_stream(HSTREAM,int);
IMPORTS void __stdcall AIL_set_stream_volume(HSTREAM,int);
IMPORTS int __stdcall AIL_stream_volume(HSTREAM);
IMPORTS void __stdcall AIL_set_stream_pan(HSTREAM,int);
IMPORTS int __stdcall AIL_stream_pan(HSTREAM);
IMPORTS void __stdcall AIL_set_stream_playback_rate(HSTREAM,int);
IMPORTS int __stdcall AIL_stream_playback_rate(HSTREAM);
IMPORTS void __stdcall AIL_set_stream_loop_count(HSTREAM,int);
IMPORTS int __stdcall AIL_stream_loop_count(HSTREAM);
IMPORTS void __stdcall AIL_set_stream_loop_block(HSTREAM,int,int);
IMPORTS void __stdcall AIL_stream_ms_position(HSTREAM,S32*,S32*);
IMPORTS void __stdcall AIL_set_stream_ms_position(HSTREAM,int);
IMPORTS void __stdcall AIL_set_stream_volume_pan(HSTREAM,float,float);
IMPORTS void __stdcall AIL_stream_volume_pan(HSTREAM,float*,float*);
IMPORTS AIL_stream_callback __stdcall AIL_register_stream_callback(HSTREAM,AIL_stream_callback);

// 3D Providers
IMPORTS int __stdcall AIL_enumerate_3D_providers(HPROENUM*,HPROVIDER*,char**);
IMPORTS M3DRESULT __stdcall AIL_open_3D_provider(HPROVIDER);
IMPORTS void __stdcall AIL_close_3D_provider(HPROVIDER);
IMPORTS void __stdcall AIL_set_3D_speaker_type(HPROVIDER,int);
IMPORTS H3DPOBJECT __stdcall AIL_open_3D_listener(HPROVIDER);
IMPORTS void __stdcall AIL_close_3D_listener(H3DPOBJECT);
IMPORTS void __stdcall AIL_set_3D_position(H3DPOBJECT,float,float,float);
IMPORTS void __stdcall AIL_set_3D_orientation(H3DPOBJECT,float,float,float,float,float,float);
IMPORTS void __stdcall AIL_set_3D_velocity_vector(H3DSAMPLE,float,float,float);
IMPORTS int __stdcall AIL_enumerate_filters(HPROENUM*,HPROVIDER*,char**);

// 3D Samples
IMPORTS H3DSAMPLE __stdcall AIL_allocate_3D_sample_handle(HPROVIDER);
IMPORTS void __stdcall AIL_release_3D_sample_handle(H3DSAMPLE);
IMPORTS int __stdcall AIL_set_3D_sample_file(H3DSAMPLE,const void*);
IMPORTS void __stdcall AIL_start_3D_sample(H3DSAMPLE);
IMPORTS void __stdcall AIL_stop_3D_sample(H3DSAMPLE);
IMPORTS void __stdcall AIL_end_3D_sample(H3DSAMPLE);
IMPORTS void __stdcall AIL_resume_3D_sample(H3DSAMPLE);
IMPORTS float __stdcall AIL_3D_sample_volume(H3DSAMPLE);
IMPORTS void __stdcall AIL_set_3D_sample_volume(H3DSAMPLE,float);
IMPORTS void __stdcall AIL_set_3D_sample_distances(H3DSAMPLE,float,float);
IMPORTS void __stdcall AIL_set_3D_sample_effects_level(H3DSAMPLE,float);
IMPORTS void __stdcall AIL_set_3D_sample_occlusion(H3DSAMPLE,float);
IMPORTS void __stdcall AIL_set_3D_sample_loop_count(H3DSAMPLE,unsigned int);
IMPORTS unsigned int __stdcall AIL_3D_sample_loop_count(H3DSAMPLE);
IMPORTS void __stdcall AIL_set_3D_sample_offset(H3DSAMPLE,unsigned int);
IMPORTS unsigned int __stdcall AIL_3D_sample_offset(H3DSAMPLE);
IMPORTS int __stdcall AIL_3D_sample_length(H3DSAMPLE);
IMPORTS void __stdcall AIL_set_3D_sample_playback_rate(H3DSAMPLE,int);
IMPORTS int __stdcall AIL_3D_sample_playback_rate(H3DSAMPLE);
IMPORTS void __stdcall AIL_set_3D_user_data(H3DPOBJECT,unsigned int,void*);
IMPORTS void* __stdcall AIL_3D_user_data(H3DSAMPLE,unsigned int);
IMPORTS AIL_3dsample_callback __stdcall AIL_register_3D_EOS_callback(H3DSAMPLE,AIL_3dsample_callback);

int MSS_auto_cleanup(void);

#ifdef __cplusplus
}
#endif
