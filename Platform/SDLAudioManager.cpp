#ifdef USE_SDL

#include "SDLAudioManager.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioEventInfo.h"
#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/file.h"
#include "GameClient/View.h"
#include <cstring>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

SDLAudioManager::SDLAudioManager()
{
}

SDLAudioManager::~SDLAudioManager()
{
    closeDevice();

    // Free cached audio data — different ownership for SDL-loaded vs
    // FFmpeg-loaded buffers.
    for (auto& pair : m_audioCache)
    {
        if (pair.second.data)
        {
            if (pair.second.sdlOwned)
                SDL_free(pair.second.data);
            else
                delete[] pair.second.data;
        }
    }
    m_audioCache.clear();
}

// --- Subsystem lifecycle ---

void SDLAudioManager::init()
{
    // Load all audio INI data via the base class
    AudioManager::init();
    // Open the actual SDL audio device — base class doesn't do this
    openDevice();
}

void SDLAudioManager::update()
{
    // Update the 3D audio listener position from the tactical view. This is
    // what AudioManager::update() does — it reads TheTacticalView's camera
    // and calls setListenerPosition(). Without it, m_listenerPosition stays
    // at the origin (0,0,0), and SoundManager::canPlayNow's distance check
    // (GameSounds.cpp:224) culls every positional (ST_WORLD) event whose
    // unit is farther than maxDistance from the map origin — i.e. almost
    // every enemy unit engine sound / move SFX on a normal map.
    //
    // Skip only when TheTacticalView isn't up yet (menu shell map, early
    // boot). Terrain logic is always in place by the time the tactical
    // view exists, so the single guard is sufficient.
    if (TheTacticalView != nullptr)
        AudioManager::update();

    // Drain pending audio requests and clean up finished streams.
    processRequestList();
    processPlayingList();
}

// --- Device management ---

void SDLAudioManager::openDevice()
{
    if (!TheGlobalData || !TheGlobalData->m_audioOn)
        return;

    // The audio subsystem is not initialized by SDLPlatform (which only
    // brings up video + events), so do it here on demand.
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            fprintf(stderr, "[SDLAudio] SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
            setOn(false, (AudioAffect)0xFF);
            return;
        }
    }

    // Open the default audio device with a reasonable format
    SDL_AudioSpec desired = {};
    desired.freq = 44100;
    desired.format = SDL_AUDIO_S16;
    desired.channels = 2;

    m_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired);
    if (m_device == 0)
    {
        fprintf(stderr, "[SDLAudio] Failed to open audio device: %s\n", SDL_GetError());
        setOn(false, (AudioAffect)0xFF);
        return;
    }

    // SDL3 audio devices start paused — must explicitly resume.
    SDL_ResumeAudioDevice(m_device);

    m_deviceSpec = desired;
    m_deviceOpen = true;

    refreshCachedVariables();
}

void SDLAudioManager::closeDevice()
{
    // Stop and destroy all playing sounds
    for (auto& ps : m_playing)
    {
        if (ps.stream)
            SDL_DestroyAudioStream(ps.stream);
    }
    m_playing.clear();

    if (m_device)
    {
        SDL_CloseAudioDevice(m_device);
        m_device = 0;
    }
    m_deviceOpen = false;
}

void* SDLAudioManager::getDevice()
{
    return (void*)(uintptr_t)m_device;
}

// --- Audio loading ---

// Load a WAV through the game's FileSystem so .big archives work.
// SDL_LoadWAV() goes straight to fopen() which can't see archive contents.
static bool LoadWAVViaFileSystem(const char* filename, SDL_AudioSpec* spec, uint8_t** data, uint32_t* length)
{
    if (!TheFileSystem)
        return false;

    File* f = TheFileSystem->openFile(filename, File::READ | File::BINARY);
    if (!f)
        return false;

    Int sz = f->size();
    char* buf = f->readEntireAndClose(); // f auto-deletes
    if (!buf || sz <= 0)
    {
        if (buf) delete[] buf;
        return false;
    }

    SDL_IOStream* io = SDL_IOFromConstMem(buf, (size_t)sz);
    if (!io)
    {
        delete[] buf;
        return false;
    }

    // SDL_LoadWAV_IO copies the data into a fresh SDL-owned buffer,
    // so we can free our copy regardless of success.
    bool ok = SDL_LoadWAV_IO(io, true /* closeio */, spec, data, length);
    delete[] buf;
    return ok;
}

// FFmpeg AVIO callbacks that read from a game File* (so .big archives work).
namespace {
    int FFReadPacket(void* opaque, uint8_t* buf, int buf_size)
    {
        File* file = static_cast<File*>(opaque);
        const int read = file->read(buf, buf_size);
        if (read <= 0)
            return AVERROR_EOF;
        return read;
    }
    int64_t FFSeekPacket(void* opaque, int64_t offset, int whence)
    {
        File* file = static_cast<File*>(opaque);
        if (whence == AVSEEK_SIZE)
            return static_cast<int64_t>(file->size());
        File::seekMode mode;
        switch (whence & ~AVSEEK_FORCE) {
            case SEEK_SET: mode = File::START; break;
            case SEEK_CUR: mode = File::CURRENT; break;
            case SEEK_END: mode = File::END; break;
            default: return AVERROR(EINVAL);
        }
        Int r = file->seek(static_cast<Int>(offset), mode);
        return (r < 0) ? AVERROR(EIO) : static_cast<int64_t>(r);
    }
}

// Decode any FFmpeg-supported audio file (MP3/OGG/WAV/...) into a single
// interleaved S16 stereo PCM buffer. Memory is owned by the caller and must
// be freed with delete[].
static bool DecodeAudioFileFFmpeg(const char* filename, SDL_AudioSpec* outSpec, uint8_t** outData, uint32_t* outLength)
{
    *outData = nullptr;
    *outLength = 0;

    if (!TheFileSystem)
        return false;

    File* file = TheFileSystem->openFile(filename, File::READ | File::BINARY | File::STREAMING);
    if (!file)
        return false;

    AVFormatContext* fmtCtx = avformat_alloc_context();
    AVIOContext* avioCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    uint8_t* avioBuf = nullptr;
    bool ok = false;
    int audioStreamIdx = -1;

    auto cleanup = [&]() {
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codecCtx) avcodec_free_context(&codecCtx);
        if (swr) swr_free(&swr);
        if (fmtCtx) avformat_close_input(&fmtCtx);
        if (avioCtx) {
            av_freep(&avioCtx->buffer); // ffmpeg may have replaced our buffer
            avio_context_free(&avioCtx);
        }
        if (file) file->close();
    };

    if (!fmtCtx) { cleanup(); return false; }

    constexpr size_t kAvioBufSize = 0x10000;
    avioBuf = static_cast<uint8_t*>(av_malloc(kAvioBufSize));
    if (!avioBuf) { cleanup(); return false; }

    avioCtx = avio_alloc_context(avioBuf, kAvioBufSize, 0, file, &FFReadPacket, nullptr, &FFSeekPacket);
    if (!avioCtx) { av_free(avioBuf); cleanup(); return false; }
    fmtCtx->pb = avioCtx;
    fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&fmtCtx, nullptr, nullptr, nullptr) < 0) { cleanup(); return false; }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) { cleanup(); return false; }

    for (unsigned int i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIdx = (int)i;
            break;
        }
    }
    if (audioStreamIdx < 0) { cleanup(); return false; }

    AVStream* stream = fmtCtx->streams[audioStreamIdx];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) { cleanup(); return false; }

    codecCtx = avcodec_alloc_context3(decoder);
    if (!codecCtx) { cleanup(); return false; }
    if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) { cleanup(); return false; }
    if (avcodec_open2(codecCtx, decoder, nullptr) < 0) { cleanup(); return false; }

    // Set up resampler: source = decoder format, dest = S16 interleaved stereo
    // at the source sample rate. SDL will up/downsample to the device rate.
    const int outSampleRate = codecCtx->sample_rate;
    const AVSampleFormat outFmt = AV_SAMPLE_FMT_S16;
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, 2);

    if (swr_alloc_set_opts2(&swr,
            &outLayout, outFmt, outSampleRate,
            &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,
            0, nullptr) < 0) { av_channel_layout_uninit(&outLayout); cleanup(); return false; }
    if (swr_init(swr) < 0) { av_channel_layout_uninit(&outLayout); cleanup(); return false; }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) { av_channel_layout_uninit(&outLayout); cleanup(); return false; }

    // Grow a single contiguous PCM buffer as we decode.
    size_t pcmCapacity = 1 << 20; // 1MB
    size_t pcmSize = 0;
    uint8_t* pcm = new uint8_t[pcmCapacity];

    auto appendPcm = [&](const uint8_t* src, size_t bytes) {
        if (pcmSize + bytes > pcmCapacity) {
            size_t newCap = pcmCapacity * 2;
            while (newCap < pcmSize + bytes) newCap *= 2;
            uint8_t* grown = new uint8_t[newCap];
            memcpy(grown, pcm, pcmSize);
            delete[] pcm;
            pcm = grown;
            pcmCapacity = newCap;
        }
        memcpy(pcm + pcmSize, src, bytes);
        pcmSize += bytes;
    };

    while (av_read_frame(fmtCtx, packet) >= 0) {
        if (packet->stream_index != audioStreamIdx) {
            av_packet_unref(packet);
            continue;
        }
        if (avcodec_send_packet(codecCtx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            // swr_convert can need extra space due to internal buffering;
            // ask for swr's available count + this frame's nb_samples.
            int dstSamples = swr_get_out_samples(swr, frame->nb_samples);
            if (dstSamples <= 0) dstSamples = frame->nb_samples;
            int dstLineSize = 0;
            uint8_t* dstBuf = nullptr;
            av_samples_alloc(&dstBuf, &dstLineSize, 2, dstSamples, outFmt, 0);
            int converted = swr_convert(swr, &dstBuf, dstSamples,
                                        (const uint8_t**)frame->data, frame->nb_samples);
            if (converted > 0) {
                int bytes = converted * 2 * av_get_bytes_per_sample(outFmt);
                appendPcm(dstBuf, (size_t)bytes);
            }
            av_freep(&dstBuf);
            av_frame_unref(frame);
        }
    }

    // Drain any remaining swr-buffered samples
    {
        int remaining = swr_get_out_samples(swr, 0);
        if (remaining > 0) {
            int dstLineSize = 0;
            uint8_t* dstBuf = nullptr;
            av_samples_alloc(&dstBuf, &dstLineSize, 2, remaining, outFmt, 0);
            int converted = swr_convert(swr, &dstBuf, remaining, nullptr, 0);
            if (converted > 0) {
                int bytes = converted * 2 * av_get_bytes_per_sample(outFmt);
                appendPcm(dstBuf, (size_t)bytes);
            }
            av_freep(&dstBuf);
        }
    }

    av_channel_layout_uninit(&outLayout);

    if (pcmSize > 0) {
        outSpec->freq = outSampleRate;
        outSpec->format = SDL_AUDIO_S16;
        outSpec->channels = 2;
        *outData = pcm;
        *outLength = (uint32_t)pcmSize;
        ok = true;
    } else {
        delete[] pcm;
    }

    cleanup();
    return ok;
}

SDLAudioManager::CachedAudio* SDLAudioManager::loadAudio(const char* filename)
{
    auto it = m_audioCache.find(filename);
    if (it != m_audioCache.end())
        return &it->second;

    CachedAudio entry = {};

    // Try SDL's WAV loader first (fastest, no dep on FFmpeg path).
    if (LoadWAVViaFileSystem(filename, &entry.spec, &entry.data, &entry.length))
    {
        entry.sdlOwned = true;
    }
    else
    {
        // Fall back to FFmpeg for MP3/OGG/etc, or filename without extension.
        if (!DecodeAudioFileFFmpeg(filename, &entry.spec, &entry.data, &entry.length))
        {
            // Final attempt: append .wav and try SDL again (some events
            // store the bare event name without an extension).
            std::string altPath = std::string(filename);
            if (altPath.find('.') == std::string::npos)
            {
                altPath += ".wav";
                if (LoadWAVViaFileSystem(altPath.c_str(), &entry.spec, &entry.data, &entry.length))
                    entry.sdlOwned = true;
                else
                    return nullptr;
            }
            else
            {
                return nullptr;
            }
        }
        else
        {
            entry.sdlOwned = false;
        }
    }

    auto [inserted, _] = m_audioCache.emplace(filename, entry);
    return &inserted->second;
}

// --- Playback ---

SDL_AudioStream* SDLAudioManager::createStream(const SDL_AudioSpec& srcSpec, const void* wavData, uint32_t wavLen, bool /*loop*/)
{
    if (!m_deviceOpen || !wavData || wavLen == 0)
        return nullptr;

    // Open a logical device stream that converts from the WAV's format
    // to the physical device's format automatically.
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(m_device, &srcSpec, nullptr, nullptr);
    if (!stream)
    {
        fprintf(stderr, "[SDLAudio] SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        return nullptr;
    }

    // Push the WAV's PCM data into the stream
    if (!SDL_PutAudioStreamData(stream, wavData, (int)wavLen))
    {
        fprintf(stderr, "[SDLAudio] SDL_PutAudioStreamData failed: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }

    // Logical devices created by SDL_OpenAudioDeviceStream start paused —
    // resume so the data we just queued actually plays.
    SDL_ResumeAudioStreamDevice(stream);
    return stream;
}

void SDLAudioManager::playAudioEvent(AudioEventRTS* event)
{
    if (!m_deviceOpen || !event)
        return;

    AsciiString filename = event->getFilename();
    if (filename.isEmpty())
        return;

    // If canPlayNow/doesViolateLimit picked an "oldest duplicate" to kill on
    // an AC_INTERRUPT event, honor it here BEFORE allocating the new stream.
    // Without this, AC_INTERRUPT events at the limit stack a new copy on top
    // of the existing ones (loud + comb-filtered), instead of replacing the
    // oldest as the original engine does.
    AudioHandle handleToKill = event->getHandleToKill();
    if (handleToKill)
    {
        for (auto it = m_playing.begin(); it != m_playing.end(); ++it)
        {
            if (it->handle == handleToKill)
            {
                if (it->stream)
                    SDL_DestroyAudioStream(it->stream);
                m_playing.erase(it);
                break;
            }
        }
    }

    CachedAudio* audio = loadAudio(filename.str());
    if (!audio)
    {
        fprintf(stderr, "[SDLAudio] Failed to load '%s'\n", filename.str());
        return;
    }

    const AudioEventInfo* info = event->getAudioEventInfo();
    const bool isMusic = (info && info->m_soundType == AT_Music);
    // Music tracks loop until something stops them. Sound effects play once.
    const bool loop = isMusic;

    SDL_AudioStream* stream = createStream(audio->spec, audio->data, audio->length, loop);
    if (!stream)
        return;

    // Per-stream gain combines event volume with the appropriate category
    // volume. SDL stream gain is linear (1.0 = unity).
    Real catVol = isMusic ? m_musicVolume : m_soundVolume;
    SDL_SetAudioStreamGain(stream, event->getVolume() * catVol);

    PlayingSound ps = {};
    ps.handle = m_nextHandle++;
    ps.stream = stream;
    // Tag the playing sound with its category so stopAllInCategory() can
    // filter properly. We don't currently distinguish 2D vs 3D vs speech in
    // this backend, so non-music events are bucketed as AudioAffect_Sound.
    ps.category = isMusic ? AudioAffect_Music : AudioAffect_Sound;
    ps.volume = event->getVolume();
    ps.looping = loop;
    ps.paused = false;
    ps.isMusic = isMusic;
    // Overlap-gate state (populated so doesViolateLimit / isPlayingAlready /
    // isObjectPlayingVoice can count against THIS instance next frame).
    ps.isPositional = event->isPositionalAudio();
    ps.isVoice = info && ((info->m_type & ST_VOICE) != 0);
    ps.objectID = (UnsignedInt)event->getObjectID();
    ps.eventName = event->getEventName();
    ps.source = audio;

    m_playing.push_back(ps);

    event->setPlayingHandle(ps.handle);
}

// --- Request and playing list processing (called from update()) ---

void SDLAudioManager::processRequestList()
{
    for (auto it = m_audioRequests.begin(); it != m_audioRequests.end(); /**/)
    {
        AudioRequest* req = *it;
        if (!req)
        {
            it = m_audioRequests.erase(it);
            continue;
        }

        switch (req->m_request)
        {
        case AR_Play:
            if (req->m_pendingEvent)
                playAudioEvent(req->m_pendingEvent);
            break;
        case AR_Pause:
        case AR_Stop:
            killAudioEventImmediately(req->m_handleToInteractOn);
            break;
        }

        // The request owns m_pendingEvent only when m_usePendingEvent is set.
        // Either way the request itself is a MemoryPoolObject that we release
        // through the standard delete-instance helper.
        releaseAudioRequest(req);
        it = m_audioRequests.erase(it);
    }
}

void SDLAudioManager::processPlayingList()
{
    // Sweep one-shot streams that have finished, and re-queue music streams
    // whose buffer is running low so they loop seamlessly.
    for (auto it = m_playing.begin(); it != m_playing.end(); /**/)
    {
        if (!it->stream)
        {
            it = m_playing.erase(it);
            continue;
        }

        const int queued = SDL_GetAudioStreamQueued(it->stream);

        if (it->looping)
        {
            // Refill before the queue empties so playback never gaps.
            // ~250ms of audio at 16-bit stereo @ source rate, computed
            // from the cached source spec.
            const int bytesPerSecond = it->source ?
                (it->source->spec.freq * it->source->spec.channels * 2) : 176400;
            const int lowWatermark = bytesPerSecond / 4;
            if (queued < lowWatermark && it->source && it->source->data && it->source->length > 0)
            {
                SDL_PutAudioStreamData(it->stream, it->source->data, (int)it->source->length);
            }
            ++it;
            continue;
        }

        // Non-looping: destroy once both queue and post-conversion buffer
        // are empty.
        if (queued <= 0 && SDL_GetAudioStreamAvailable(it->stream) <= 0)
        {
            SDL_DestroyAudioStream(it->stream);
            it = m_playing.erase(it);
            continue;
        }
        ++it;
    }
}

void SDLAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay)
{
    if (!eventToPlay) return;
    // Play directly, bypassing the request queue
    AudioEventRTS* mutableEvent = const_cast<AudioEventRTS*>(eventToPlay);
    playAudioEvent(mutableEvent);
}

// --- Stop/Pause/Resume ---

void SDLAudioManager::stopAllInCategory(AudioAffect which)
{
    // Filter by the category bitmask the caller passed (Music / Sound /
    // Sound3D / Speech). Without this filter, stopAudio(AudioAffect_Music)
    // would also kill SFX, and conversely stopAudio(AudioAffect_Sound) would
    // silence the music — both regressions versus the Miles backend.
    for (auto it = m_playing.begin(); it != m_playing.end(); /**/)
    {
        if ((it->category & which) != 0)
        {
            if (it->stream)
                SDL_DestroyAudioStream(it->stream);
            it = m_playing.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void SDLAudioManager::stopAudio(AudioAffect which)
{
    stopAllInCategory(which);
}

// SubsystemInterface::reset is called via TheGameEngine->reset() during the
// shell-to-game transition (e.g. when loading a saved game from the main
// menu). The base AudioManager::reset only resets script volume scalars; it
// does NOT touch playing streams. Without this override the shell map music
// keeps playing through the load and into the new mission. Mirrors
// MilesAudioManager::reset (Miles backend in the original ZH source).
void SDLAudioManager::reset()
{
    AudioManager::reset();

    // Stop and free every active stream regardless of category. Music, SFX,
    // ambient — all of it should be silent after a reset.
    for (auto& ps : m_playing)
    {
        if (ps.stream)
        {
            SDL_DestroyAudioStream(ps.stream);
            ps.stream = nullptr;
        }
    }
    m_playing.clear();

    // Drain any pending requests that haven't been serviced yet, otherwise a
    // queued AR_Play from the previous game state would fire on the next
    // update() and re-start audio we just stopped.
    for (auto it = m_audioRequests.begin(); it != m_audioRequests.end(); ++it)
    {
        if (*it)
            releaseAudioRequest(*it);
    }
    m_audioRequests.clear();
}

void SDLAudioManager::pauseAudio(AudioAffect /*which*/)
{
    if (m_device)
        SDL_PauseAudioDevice(m_device);
}

void SDLAudioManager::resumeAudio(AudioAffect /*which*/)
{
    if (m_device)
        SDL_ResumeAudioDevice(m_device);
}

void SDLAudioManager::pauseAmbient(Bool /*shouldPause*/)
{
}

void SDLAudioManager::killAudioEventImmediately(AudioHandle audioEvent)
{
    // The "stop the music" sentinels are not real handles — they ask the
    // backend to find whichever music track is currently playing and stop
    // it. AudioManager::removeAudioEvent routes them through MusicManager,
    // which then queues an AR_Stop with this sentinel as the target handle.
    if (audioEvent == AHSV_StopTheMusic || audioEvent == AHSV_StopTheMusicFade)
    {
        // SDL streams have no built-in fade; treat the fade variant as
        // an instant stop. The menu→loadscreen transition is brief enough
        // that the missing crossfade isn't noticeable.
        for (auto it = m_playing.begin(); it != m_playing.end(); /**/)
        {
            if (it->isMusic)
            {
                if (it->stream)
                    SDL_DestroyAudioStream(it->stream);
                it = m_playing.erase(it);
            }
            else
                ++it;
        }
        return;
    }

    for (auto it = m_playing.begin(); it != m_playing.end(); ++it)
    {
        if (it->handle == audioEvent)
        {
            if (it->stream)
                SDL_DestroyAudioStream(it->stream);
            m_playing.erase(it);
            return;
        }
    }
}

// --- Music ---

void SDLAudioManager::nextMusicTrack()
{
    // Find current music track name
    AsciiString currentName;
    for (auto& s : m_playing)
    {
        if (s.isMusic)
        {
            currentName = s.eventName;
            break;
        }
    }

    // Stop current music
    if (TheAudio)
        TheAudio->removeAudioEvent(AHSV_StopTheMusic);

    // Queue next track
    AsciiString nextName = nextTrackName(currentName);
    if (!nextName.isEmpty())
    {
        AudioEventRTS newTrack(nextName);
        if (TheAudio)
            TheAudio->addAudioEvent(&newTrack);
    }
}

void SDLAudioManager::prevMusicTrack()
{
    AsciiString currentName;
    for (auto& s : m_playing)
    {
        if (s.isMusic)
        {
            currentName = s.eventName;
            break;
        }
    }

    if (TheAudio)
        TheAudio->removeAudioEvent(AHSV_StopTheMusic);

    AsciiString prevName = prevTrackName(currentName);
    if (!prevName.isEmpty())
    {
        AudioEventRTS newTrack(prevName);
        if (TheAudio)
            TheAudio->addAudioEvent(&newTrack);
    }
}

Bool SDLAudioManager::isMusicPlaying() const
{
    for (auto& s : m_playing)
    {
        if (s.isMusic)
            return TRUE;
    }
    return FALSE;
}

Bool SDLAudioManager::hasMusicTrackCompleted(const AsciiString& /*trackName*/, Int /*numberOfTimes*/) const
{
    // Track completion tracking not implemented — always returns FALSE
    return FALSE;
}

AsciiString SDLAudioManager::getMusicTrackName() const
{
    for (auto& s : m_playing)
    {
        if (s.isMusic)
            return s.eventName;
    }
    return "";
}

// --- Provider enumeration (SDL has one "provider" — the default device) ---

UnsignedInt SDLAudioManager::getProviderCount() const { return 1; }
AsciiString SDLAudioManager::getProviderName(UnsignedInt /*providerNum*/) const { return "SDL3 Audio"; }
UnsignedInt SDLAudioManager::getProviderIndex(AsciiString /*providerName*/) const { return 0; }
void SDLAudioManager::selectProvider(UnsignedInt /*providerNdx*/) {}
void SDLAudioManager::unselectProvider() {}
UnsignedInt SDLAudioManager::getSelectedProvider() const { return 0; }
void SDLAudioManager::setSpeakerType(UnsignedInt /*speakerType*/) {}
UnsignedInt SDLAudioManager::getSpeakerType() { return 0; }

// --- Channel info ---

UnsignedInt SDLAudioManager::getNum2DSamples() const { return 32; }
UnsignedInt SDLAudioManager::getNum3DSamples() const { return 16; }
UnsignedInt SDLAudioManager::getNumStreams() const { return 4; }

// --- Priority / limit checks ---
//
// These three gates (+ isPlayingLowerPriority) are called by
// SoundManager::canPlayNow before a new AudioRequest is even queued. With
// all four stubbed to FALSE the engine could not stop identical samples
// from stacking — selection voices, weapon impacts, and world SFX
// phase-summed into the familiar "too loud + comb-filter reverb" artifact.
// Mirrors MilesAudioManager.cpp:1826-1955 in the original Zero Hour source.

Bool SDLAudioManager::doesViolateLimit(AudioEventRTS* event) const
{
    if (!event || !event->getAudioEventInfo())
        return FALSE;

    const Int limit = event->getAudioEventInfo()->m_limit;
    if (limit == 0)
        return FALSE; // 0 == "no limit" per the INI schema

    const AsciiString& name = event->getEventName();
    const Bool positional = event->isPositionalAudio();
    const Bool interrupting = (event->getAudioEventInfo()->m_control & AC_INTERRUPT) != 0;

    Int totalCount = 0;
    Int totalRequestCount = 0;
    AudioHandle oldestHandle = 0;

    // Count same-named instances already playing in the matching 2D/3D
    // bucket. Also remember the oldest one — on AC_INTERRUPT events this
    // is what we stamp as the "kill me first" handle.
    for (const PlayingSound& ps : m_playing)
    {
        if (ps.isMusic) continue;
        if (ps.isPositional != (positional != FALSE)) continue;
        if (ps.eventName != name) continue;

        if (totalCount == 0)
            oldestHandle = ps.handle;
        ++totalCount;
    }

    // Also count same-named requests queued this frame but not yet started.
    // Without this, a single logic tick can enqueue many duplicates (e.g.
    // every unit in a selected group shouting the same VOICE_SELECT), all
    // of which would pass the "playing" check and fire together next update.
    for (const AudioRequest* req : m_audioRequests)
    {
        if (!req || !req->m_usePendingEvent || !req->m_pendingEvent)
            continue;
        if (req->m_request != AR_Play)
            continue;
        if (req->m_pendingEvent->getEventName() == name)
        {
            ++totalRequestCount;
            ++totalCount;
        }
    }

    if (interrupting)
    {
        // Match Miles: if queued-same-frame already hits limit, drop this
        // one (we have no "old" playing sound to kill).
        if (totalRequestCount >= limit)
            return TRUE;

        if (totalCount < limit)
        {
            event->setHandleToKill(0);
            return FALSE;
        }
        // At/over the limit and there IS an older playing instance —
        // let playAudioEvent replace it.
        event->setHandleToKill(oldestHandle);
        return FALSE;
    }

    if (totalCount < limit)
    {
        event->setHandleToKill(0);
        return FALSE;
    }

    return TRUE;
}

Bool SDLAudioManager::isPlayingLowerPriority(AudioEventRTS* event) const
{
    // No evictions of lower-priority streams implemented yet; leaving this
    // FALSE just means canPlayNow won't force-steal a channel. Limits +
    // interrupt kill are the primary overlap defense, so this is OK.
    (void)event;
    return FALSE;
}

Bool SDLAudioManager::isPlayingAlready(AudioEventRTS* event) const
{
    if (!event)
        return FALSE;
    const AsciiString& name = event->getEventName();
    const Bool positional = event->isPositionalAudio();
    for (const PlayingSound& ps : m_playing)
    {
        if (ps.isMusic) continue;
        if (ps.isPositional != (positional != FALSE)) continue;
        if (ps.eventName == name)
            return TRUE;
    }
    return FALSE;
}

Bool SDLAudioManager::isObjectPlayingVoice(UnsignedInt objID) const
{
    if (objID == 0)
        return FALSE;
    for (const PlayingSound& ps : m_playing)
    {
        if (ps.isVoice && ps.objectID == objID)
            return TRUE;
    }
    return FALSE;
}

void SDLAudioManager::adjustVolumeOfPlayingAudio(AsciiString /*eventName*/, Real /*newVolume*/) {}
void SDLAudioManager::removePlayingAudio(AsciiString /*eventName*/) {}
void SDLAudioManager::removeAllDisabledAudio() {}

Bool SDLAudioManager::has3DSensitiveStreamsPlaying() const { return FALSE; }
void* SDLAudioManager::getHandleForBink() { return nullptr; }
void SDLAudioManager::releaseHandleForBink() {}

void SDLAudioManager::setPreferredProvider(AsciiString /*providerNdx*/) {}
void SDLAudioManager::setPreferredSpeaker(AsciiString /*speakerType*/) {}

Real SDLAudioManager::getFileLengthMS(AsciiString strToLoad) const
{
    // Load file to determine length
    SDL_AudioSpec spec;
    uint8_t* data = nullptr;
    uint32_t len = 0;
    if (SDL_LoadWAV(strToLoad.str(), &spec, &data, &len))
    {
        Real bytesPerSample = (Real)(SDL_AUDIO_BYTESIZE(spec.format) * spec.channels);
        Real totalSamples = (Real)len / bytesPerSample;
        Real lengthMS = (totalSamples / (Real)spec.freq) * 1000.0f;
        SDL_free(data);
        return lengthMS;
    }
    return -1.0f;
}

void SDLAudioManager::closeAnySamplesUsingFile(const void* /*fileToClose*/) {}
void SDLAudioManager::setDeviceListenerPosition() {}

#if defined(RTS_DEBUG)
void SDLAudioManager::audioDebugDisplay(DebugDisplayInterface* /*dd*/, void* /*userData*/, FILE* /*fp*/)
{
}
#endif

// --- Notification ---

void SDLAudioManager::notifyOfAudioCompletion(uintptr_t /*audioCompleted*/, UnsignedInt /*flags*/)
{
}

#endif // USE_SDL
