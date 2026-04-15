// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

static const PoolInitRec DefaultDMA[] =
{
	//          name, allocSize, initialCount, overflowCount
	{   "dmaPool_16",        16,        65536,          1024 },
	{   "dmaPool_32",        32,       150000,          1024 },
	{   "dmaPool_64",        64,        60000,          1024 },
	{  "dmaPool_128",       128,        32768,          1024 },
	{  "dmaPool_256",       256,         8192,          1024 },
	{  "dmaPool_512",       512,         8192,          1024 },
	{ "dmaPool_1024",      1024,        24000,          1024 }
};
