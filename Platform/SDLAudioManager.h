#pragma once

#ifdef USE_SDL

#include "Common/GameAudio.h"
#include <SDL3/SDL.h>
#include <vector>
#include <unordered_map>

// SDLAudioManager - Cross-platform audio backend using SDL3 audio.
// Replaces MilesAudioManager (Windows-only Miles Sound System).
//
// Uses SDL3's audio stream API for mixing and playback:
// - Multiple simultaneous 2D sound effects
// - Streaming music playback
// - Per-category volume control (music, SFX, voice, ambient)
// - WAV file loading via SDL_LoadWAV
class SDLAudioManager : public AudioManager
{
public:
    SDLAudioManager();
    virtual ~SDLAudioManager();

#if defined(RTS_DEBUG)
    void audioDebugDisplay(DebugDisplayInterface* dd, void* userData, FILE* fp = nullptr) override;
#endif

    // SubsystemInterface — we override these so the SDL backend actually
    // opens its audio device on init() and drains the audio request queue
    // each frame on update(). The base AudioManager versions don't.
    void init() override;
    void update() override;
    void reset() override;
    void processRequestList() override;

    void stopAudio(AudioAffect which) override;
    void pauseAudio(AudioAffect which) override;
    void resumeAudio(AudioAffect which) override;
    void pauseAmbient(Bool shouldPause) override;
    void killAudioEventImmediately(AudioHandle audioEvent) override;

    void nextMusicTrack() override;
    void prevMusicTrack() override;
    Bool isMusicPlaying() const override;
    Bool hasMusicTrackCompleted(const AsciiString& trackName, Int numberOfTimes) const override;
    AsciiString getMusicTrackName() const override;

    void openDevice() override;
    void closeDevice() override;
    void* getDevice() override;

    void notifyOfAudioCompletion(uintptr_t audioCompleted, UnsignedInt flags) override;

    UnsignedInt getProviderCount() const override;
    AsciiString getProviderName(UnsignedInt providerNum) const override;
    UnsignedInt getProviderIndex(AsciiString providerName) const override;
    void selectProvider(UnsignedInt providerNdx) override;
    void unselectProvider() override;
    UnsignedInt getSelectedProvider() const override;
    void setSpeakerType(UnsignedInt speakerType) override;
    UnsignedInt getSpeakerType() override;

    UnsignedInt getNum2DSamples() const override;
    UnsignedInt getNum3DSamples() const override;
    UnsignedInt getNumStreams() const override;

    Bool doesViolateLimit(AudioEventRTS* event) const override;
    Bool isPlayingLowerPriority(AudioEventRTS* event) const override;
    Bool isPlayingAlready(AudioEventRTS* event) const override;
    Bool isObjectPlayingVoice(UnsignedInt objID) const override;

    void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume) override;
    void removePlayingAudio(AsciiString eventName) override;
    void removeAllDisabledAudio() override;

    Bool has3DSensitiveStreamsPlaying() const override;
    void* getHandleForBink() override;
    void releaseHandleForBink() override;
    void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay) override;

    void setPreferredProvider(AsciiString providerNdx) override;
    void setPreferredSpeaker(AsciiString speakerType) override;
    Real getFileLengthMS(AsciiString strToLoad) const override;
    void closeAnySamplesUsingFile(const void* fileToClose) override;
    void setDeviceListenerPosition() override;

private:
    // Cached fully-decoded PCM for one audio file (WAV via SDL,
    // MP3/OGG/etc via FFmpeg). Owned by m_audioCache, so PlayingSound
    // can refer back to it for looping without re-decoding.
    struct CachedAudio
    {
        uint8_t* data;
        uint32_t length;
        SDL_AudioSpec spec;
        bool sdlOwned; // true: free with SDL_free, false: free with delete[]
    };

    // Active sound instance
    struct PlayingSound
    {
        AudioHandle handle;
        SDL_AudioStream* stream;
        AudioAffect category;
        Real volume;
        bool looping;
        bool paused;
        bool isMusic;
        bool isPositional;          // 3D positional audio (vs. 2D UI/global)
        bool isVoice;               // ST_VOICE bit — used by isObjectPlayingVoice
        UnsignedInt objectID;       // source object, 0 if none (for voice overlap)
        AsciiString eventName;
        const CachedAudio* source;  // for re-pushing data on loop
    };

    void playAudioEvent(AudioEventRTS* event);
    SDL_AudioStream* createStream(const SDL_AudioSpec& srcSpec, const void* wavData, uint32_t wavLen, bool loop);
    void stopAllInCategory(AudioAffect which);
    void processPlayingList();

    SDL_AudioDeviceID m_device = 0;
    SDL_AudioSpec m_deviceSpec = {};
    bool m_deviceOpen = false;

    std::vector<PlayingSound> m_playing;
    AudioHandle m_nextHandle = 1;

    std::unordered_map<std::string, CachedAudio> m_audioCache;

    CachedAudio* loadAudio(const char* filename);
};

#endif // USE_SDL
