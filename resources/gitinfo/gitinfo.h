// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

extern const char GitSHA1[];
extern const char GitShortSHA1[];
extern const char GitCommitDate[];
extern const char GitCommitAuthorName[];
extern const char GitTag[];
extern time_t GitCommitTimeStamp;
extern bool GitUncommittedChanges;
extern bool GitHaveInfo;
extern int GitRevision;

#ifdef __cplusplus
} // extern "C"
#endif
