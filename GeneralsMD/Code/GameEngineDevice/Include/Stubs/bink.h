#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BINKSURFACE24 1
#define BINKSURFACE32 3
#define BINKSURFACE555 9
#define BINKSURFACE565 10
#define BINKPRELOADALL 0x00002000
#define BINKCOPYNOSCALING 0x70000000

typedef struct BINK {
    unsigned int Width, Height, Frames, FrameNum, LastFrameNum, FrameRate, FrameRateDiv;
} BINK, *HBINK;

typedef void *(__stdcall *SndOpenCallback)(uintptr_t param);
typedef unsigned int u32;

HBINK __stdcall BinkOpen(const char *name, unsigned int flags);
void __stdcall BinkSetSoundTrack(unsigned int total_tracks, unsigned int *tracks);
int __stdcall BinkSetSoundSystem(SndOpenCallback open, uintptr_t param);
void *__stdcall BinkOpenDirectSound(uintptr_t param);
void __stdcall BinkClose(HBINK handle);
int __stdcall BinkWait(HBINK handle);
int __stdcall BinkDoFrame(HBINK handle);
int __stdcall BinkCopyToBuffer(HBINK handle, void *dest, int destpitch, unsigned int destheight, unsigned int destx, unsigned int desty, unsigned int flags);
void __stdcall BinkSetVolume(HBINK handle, unsigned int trackid, int volume);
void __stdcall BinkNextFrame(HBINK handle);
void __stdcall BinkGoto(HBINK handle, unsigned int frame, int flags);
#define BinkSoundUseDirectSound(x) BinkSetSoundSystem(BinkOpenDirectSound, (uintptr_t)(x))

#ifdef __cplusplus
}
#endif
