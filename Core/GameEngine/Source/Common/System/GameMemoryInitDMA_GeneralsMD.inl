// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

static const PoolInitRec DefaultDMA[] =
{
	//          name, allocSize, initialCount, overflowCount
	{   "dmaPool_16",        16,       130000,         10000 },
	{   "dmaPool_32",        32,       250000,         10000 },
	{   "dmaPool_64",        64,       100000,         10000 },
	{  "dmaPool_128",       128,        80000,         10000 },
	{  "dmaPool_256",       256,        20000,          5000 },
	{  "dmaPool_512",       512,        16000,          5000 },
	{ "dmaPool_1024",      1024,         6000,          1024 }
};
