/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***************************************************************************
 ***    C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S     ***
 ***************************************************************************
 *                                                                         *
 *                 Project Name : G                                        *
 *                                                                         *
 *                     $Archive:: /VSS_Sync/ww3d2/part_buf.cpp            $*
 *                                                                         *
 *                      $Author:: Vss_sync                                $*
 *                                                                         *
 *                     $Modtime:: 8/29/01 7:29p                           $*
 *                                                                         *
 *                    $Revision:: 9                                        $*
 *                                                                         *
 *-------------------------------------------------------------------------*
 * Functions:                                                              *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "part_buf.h"
#include "part_emt.h"
#include "pointgr.h"
#include "seglinerenderer.h"
#include "linegrp.h"
#include "ww3d.h"
#include "camera.h"
#include "scene.h"
#include "wwmath.h"
#include "pot.h"
#include "wwdebug.h"
#include "texture.h"
#include "refcount.h"
#include "w3d_file.h"
#include <cstring>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Maximum number of random entries in each property's random table.
// MUST be a power of two.
static const unsigned int MAX_RANDOM_ENTRIES = 64;

// A large default max screen size (effectively no LOD clamping by default).
// Note: NO_MAX_SCREEN_SIZE is already a macro in w3d_file.h (= WWMATH_FLOAT_MAX),
// so we just use that macro directly in the LOD array initializer below.

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

unsigned int ParticleBufferClass::TotalActiveCount = 0;

// Pseudo-random permutation of 0..15 used for LOD decimation.
const unsigned int ParticleBufferClass::PermutationArray[16] = {
	 7, 13,  2, 11,  0,  9,  4, 15,
	 6,  1, 14,  3, 12,  5, 10,  8
};

// Default per-LOD screen-size thresholds.  Filled with NO_MAX_SCREEN_SIZE so
// that by default no screen-size clamping is applied.
float ParticleBufferClass::LODMaxScreenSizes[17] = {
	NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE,
	NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE,
	NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE,
	NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE, NO_MAX_SCREEN_SIZE,
	NO_MAX_SCREEN_SIZE
};

// ---------------------------------------------------------------------------
// Helper: build a power-of-two random table of float values centered on 0.
// Returns the number of entries - 1 (for use as bitmask).
// If randomness is zero the table gets one zero entry and 0 is returned.
// ---------------------------------------------------------------------------
static unsigned int Build_Float_Random_Table(float ** out_table, unsigned int desired_count,
	float range)
{
	unsigned int entry_count;
	if (range == 0.0f) {
		entry_count = 1;
	} else {
		entry_count = Find_POT(desired_count);
		if (entry_count > MAX_RANDOM_ENTRIES) {
			entry_count = MAX_RANDOM_ENTRIES;
		}
		if (entry_count < 1) {
			entry_count = 1;
		}
	}

	*out_table = W3DNEWARRAY float[entry_count];
	if (range == 0.0f) {
		(*out_table)[0] = 0.0f;
	} else {
		for (unsigned int i = 0; i < entry_count; ++i) {
			(*out_table)[i] = ((float)rand() / (float)RAND_MAX) * range * 2.0f - range;
		}
	}
	return entry_count - 1;
}

// ---------------------------------------------------------------------------
// Helper: build a power-of-two random table of Vector3 values.
// ---------------------------------------------------------------------------
static unsigned int Build_Vector3_Random_Table(Vector3 ** out_table, unsigned int desired_count,
	const Vector3 & range)
{
	unsigned int entry_count;
	bool has_random = (range.X != 0.0f || range.Y != 0.0f || range.Z != 0.0f);
	if (!has_random) {
		entry_count = 1;
	} else {
		entry_count = Find_POT(desired_count);
		if (entry_count > MAX_RANDOM_ENTRIES) {
			entry_count = MAX_RANDOM_ENTRIES;
		}
		if (entry_count < 1) {
			entry_count = 1;
		}
	}

	*out_table = W3DNEWARRAY Vector3[entry_count];
	if (!has_random) {
		(*out_table)[0].Set(0.0f, 0.0f, 0.0f);
	} else {
		for (unsigned int i = 0; i < entry_count; ++i) {
			float rx = ((float)rand() / (float)RAND_MAX) * range.X * 2.0f - range.X;
			float ry = ((float)rand() / (float)RAND_MAX) * range.Y * 2.0f - range.Y;
			float rz = ((float)rand() / (float)RAND_MAX) * range.Z * 2.0f - range.Z;
			(*out_table)[i].Set(rx, ry, rz);
		}
	}
	return entry_count - 1;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
ParticleBufferClass::ParticleBufferClass(
	ParticleEmitterClass *emitter,
	unsigned int buffer_size,
	ParticlePropertyStruct<Vector3> &color,
	ParticlePropertyStruct<float> &opacity,
	ParticlePropertyStruct<float> &size,
	ParticlePropertyStruct<float> &rotation,
	float orient_rnd,
	ParticlePropertyStruct<float> &frame,
	ParticlePropertyStruct<float> &blurtime,
	Vector3 accel,
	float max_age,
	float future_start,
	TextureClass *tex,
	ShaderClass shader,
	bool pingpong,
	int render_mode,
	int frame_mode,
	const W3dEmitterLinePropertiesStruct *line_props)
	:
	RenderObjClass(),
	RenderMode(render_mode),
	FrameMode(frame_mode),
	Accel(accel * (1.0f / 1000000.0f)),		// convert from units/s^2 to units/ms^2
	HasAccel(accel.X != 0.0f || accel.Y != 0.0f || accel.Z != 0.0f),
	MaxAge((unsigned int)(max_age * 1000.0f)),
	FutureStartTime((unsigned int)(future_start * 1000.0f)),
	LastUpdateTime(WW3D::Get_Sync_Time()),
	IsEmitterDead(false),
	MaxSize(0.0f),
	MaxNum(buffer_size),
	Start(0),
	End(0),
	NewEnd(0),
	NonNewNum(0),
	NewNum(0),
	BoundingBoxDirty(true),
	NumColorKeyFrames(0),
	ColorKeyFrameTimes(nullptr),
	ColorKeyFrameValues(nullptr),
	ColorKeyFrameDeltas(nullptr),
	NumAlphaKeyFrames(0),
	AlphaKeyFrameTimes(nullptr),
	AlphaKeyFrameValues(nullptr),
	AlphaKeyFrameDeltas(nullptr),
	NumSizeKeyFrames(0),
	SizeKeyFrameTimes(nullptr),
	SizeKeyFrameValues(nullptr),
	SizeKeyFrameDeltas(nullptr),
	NumRotationKeyFrames(0),
	RotationKeyFrameTimes(nullptr),
	RotationKeyFrameValues(nullptr),
	HalfRotationKeyFrameDeltas(nullptr),
	OrientationKeyFrameValues(nullptr),
	NumFrameKeyFrames(0),
	FrameKeyFrameTimes(nullptr),
	FrameKeyFrameValues(nullptr),
	FrameKeyFrameDeltas(nullptr),
	NumBlurTimeKeyFrames(0),
	BlurTimeKeyFrameTimes(nullptr),
	BlurTimeKeyFrameValues(nullptr),
	BlurTimeKeyFrameDeltas(nullptr),
	DefaultTailDiffuse(0.0f, 0.0f, 0.0f, 1.0f),
	NumRandomColorEntriesMinus1(0),
	RandomColorEntries(nullptr),
	NumRandomAlphaEntriesMinus1(0),
	RandomAlphaEntries(nullptr),
	NumRandomSizeEntriesMinus1(0),
	RandomSizeEntries(nullptr),
	NumRandomRotationEntriesMinus1(0),
	RandomRotationEntries(nullptr),
	NumRandomOrientationEntriesMinus1(0),
	RandomOrientationEntries(nullptr),
	NumRandomFrameEntriesMinus1(0),
	RandomFrameEntries(nullptr),
	NumRandomBlurTimeEntriesMinus1(0),
	RandomBlurTimeEntries(nullptr),
	ColorRandom(color.Rand),
	OpacityRandom(opacity.Rand),
	SizeRandom(size.Rand),
	RotationRandom(rotation.Rand),
	FrameRandom(frame.Rand),
	BlurTimeRandom(blurtime.Rand),
	InitialOrientationRandom(orient_rnd),
	PointGroup(nullptr),
	LineRenderer(nullptr),
	LineGroup(nullptr),
	Diffuse(nullptr),
	Color(nullptr),
	Alpha(nullptr),
	Size(nullptr),
	Frame(nullptr),
	UCoord(nullptr),
	TailPosition(nullptr),
	TailDiffuse(nullptr),
	Orientation(nullptr),
	APT(nullptr),
	GroupID(nullptr),
	PingPongPosition(pingpong),
	Velocity(nullptr),
	TimeStamp(nullptr),
	Emitter(emitter),
	DecimationThreshold(0),
	LodCount(1),
	LodBias(1.0f),
	ProjectedArea(0.0f),
	CurrentGroupID(0),
	NewParticleQueue(nullptr),
	NewParticleQueueStart(0),
	NewParticleQueueEnd(0),
	NewParticleQueueCount(0)
{
	WWASSERT(MaxNum > 0);

	// Allocate per-particle state arrays.
	Position[0] = W3DNEW ShareBufferClass<Vector3>(MaxNum, "ParticleBuf::Position[0]");
	Position[1] = nullptr;
	if (PingPongPosition) {
		Position[1] = W3DNEW ShareBufferClass<Vector3>(MaxNum, "ParticleBuf::Position[1]");
	}

	Velocity  = W3DNEWARRAY Vector3[MaxNum];
	TimeStamp = W3DNEWARRAY unsigned int[MaxNum];

	// Allocate combined RGBA buffer used for rendering.
	Diffuse = W3DNEW ShareBufferClass<Vector4>(MaxNum, "ParticleBuf::Diffuse");

	// New-particle queue (same capacity as the main buffer).
	NewParticleQueue = W3DNEWARRAY NewParticleStruct[MaxNum];

	// Set up visual property keyframes.
	Reset_Colors(color);
	Reset_Opacity(opacity);
	Reset_Size(size);
	Reset_Rotations(rotation, orient_rnd);
	Reset_Frames(frame);
	Reset_Blur_Times(blurtime);

	// Create the rendering sub-objects based on the requested mode.
	if (render_mode == W3D_EMITTER_RENDER_MODE_TRI_PARTICLES ||
		render_mode == W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES) {

		PointGroup = W3DNEW PointGroupClass();
		PointGroup->Set_Texture(tex);
		PointGroup->Set_Shader(shader);
		PointGroup->Set_Flag(PointGroupClass::TRANSFORM, true);
		if (render_mode == W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES) {
			PointGroup->Set_Point_Mode(PointGroupClass::QUADS);
		} else {
			PointGroup->Set_Point_Mode(PointGroupClass::TRIS);
		}

		// Frame layout (texture atlas rows/cols).
		unsigned char frame_log2 = 0;
		switch (frame_mode) {
			case W3D_EMITTER_FRAME_MODE_2x2:   frame_log2 = 1; break;
			case W3D_EMITTER_FRAME_MODE_4x4:   frame_log2 = 2; break;
			case W3D_EMITTER_FRAME_MODE_8x8:   frame_log2 = 3; break;
			case W3D_EMITTER_FRAME_MODE_16x16: frame_log2 = 4; break;
			default:                           frame_log2 = 0; break;
		}
		PointGroup->Set_Frame_Row_Column_Count_Log2(frame_log2);

	} else if (render_mode == W3D_EMITTER_RENDER_MODE_LINE) {
		LineRenderer = W3DNEW SegLineRendererClass();
		if (line_props) {
			LineRenderer->Init(*line_props);
		}
		LineRenderer->Set_Texture(tex);
		LineRenderer->Set_Shader(shader);

	} else if (render_mode == W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA ||
	           render_mode == W3D_EMITTER_RENDER_MODE_LINEGRP_PRISM) {
		LineGroup = W3DNEW LineGroupClass();
		LineGroup->Set_Texture(tex);
		LineGroup->Set_Shader(shader);
		LineGroup->Set_Flag(LineGroupClass::TRANSFORM, true);
		if (render_mode == W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA) {
			LineGroup->Set_Line_Mode(LineGroupClass::TETRAHEDRON);
		} else {
			LineGroup->Set_Line_Mode(LineGroupClass::PRISM);
		}
		// Allocate tail arrays for line group mode.
		TailPosition = W3DNEW ShareBufferClass<Vector3>(MaxNum, "ParticleBuf::TailPosition");
		TailDiffuse  = W3DNEW ShareBufferClass<Vector4>(MaxNum, "ParticleBuf::TailDiffuse");
		UCoord       = W3DNEW ShareBufferClass<float>(MaxNum, "ParticleBuf::UCoord");
		GroupID      = W3DNEW ShareBufferClass<unsigned char>(MaxNum, "ParticleBuf::GroupID");
	}

	// Cost/value arrays for predictive LOD — default single-level setup.
	for (int i = 0; i < 17; ++i) { Cost[i] = 0.0f; }
	for (int i = 0; i < 18; ++i) { Value[i] = RenderObjClass::AT_MIN_LOD; }
	Value[0] = RenderObjClass::AT_MIN_LOD;

	TotalActiveCount++;
}

// ---------------------------------------------------------------------------
// Copy constructor
// ---------------------------------------------------------------------------
ParticleBufferClass::ParticleBufferClass(const ParticleBufferClass & src)
	: RenderObjClass(src)
{
	// Copy all POD/scalar state.
	RenderMode         = src.RenderMode;
	FrameMode          = src.FrameMode;
	Accel              = src.Accel;
	HasAccel           = src.HasAccel;
	MaxAge             = src.MaxAge;
	FutureStartTime    = src.FutureStartTime;
	LastUpdateTime     = src.LastUpdateTime;
	IsEmitterDead      = src.IsEmitterDead;
	MaxSize            = src.MaxSize;
	MaxNum             = src.MaxNum;
	Start              = src.Start;
	End                = src.End;
	NewEnd             = src.NewEnd;
	NonNewNum          = src.NonNewNum;
	NewNum             = src.NewNum;
	BoundingBox        = src.BoundingBox;
	BoundingBoxDirty   = src.BoundingBoxDirty;
	PingPongPosition   = src.PingPongPosition;
	DecimationThreshold = src.DecimationThreshold;
	LodCount           = src.LodCount;
	LodBias            = src.LodBias;
	ProjectedArea      = src.ProjectedArea;
	ColorRandom        = src.ColorRandom;
	OpacityRandom      = src.OpacityRandom;
	SizeRandom         = src.SizeRandom;
	RotationRandom     = src.RotationRandom;
	FrameRandom        = src.FrameRandom;
	BlurTimeRandom     = src.BlurTimeRandom;
	InitialOrientationRandom = src.InitialOrientationRandom;
	DefaultTailDiffuse = src.DefaultTailDiffuse;
	CurrentGroupID     = src.CurrentGroupID;
	Emitter            = nullptr; // caller must call Set_Emitter after cloning

	::memcpy(Cost,  src.Cost,  sizeof(Cost));
	::memcpy(Value, src.Value, sizeof(Value));

	// Per-particle state arrays.
	Position[0] = W3DNEW ShareBufferClass<Vector3>(*src.Position[0]);
	Position[1] = src.Position[1] ? W3DNEW ShareBufferClass<Vector3>(*src.Position[1]) : nullptr;
	Velocity    = W3DNEWARRAY Vector3[MaxNum];
	TimeStamp   = W3DNEWARRAY unsigned int[MaxNum];
	::memcpy(Velocity,  src.Velocity,  MaxNum * sizeof(Vector3));
	::memcpy(TimeStamp, src.TimeStamp, MaxNum * sizeof(unsigned int));

	Diffuse = W3DNEW ShareBufferClass<Vector4>(*src.Diffuse);

	// New-particle queue.
	NewParticleQueue      = W3DNEWARRAY NewParticleStruct[MaxNum];
	NewParticleQueueStart = src.NewParticleQueueStart;
	NewParticleQueueEnd   = src.NewParticleQueueEnd;
	NewParticleQueueCount = src.NewParticleQueueCount;
	::memcpy(NewParticleQueue, src.NewParticleQueue, MaxNum * sizeof(NewParticleStruct));

	// Visual arrays — copy if source is non-null.
	Color       = src.Color       ? W3DNEW ShareBufferClass<Vector3>(*src.Color)       : nullptr;
	Alpha       = src.Alpha       ? W3DNEW ShareBufferClass<float>(*src.Alpha)         : nullptr;
	Size        = src.Size        ? W3DNEW ShareBufferClass<float>(*src.Size)          : nullptr;
	Frame       = src.Frame       ? W3DNEW ShareBufferClass<uint8>(*src.Frame)         : nullptr;
	Orientation = src.Orientation ? W3DNEW ShareBufferClass<uint8>(*src.Orientation)   : nullptr;
	APT         = src.APT         ? W3DNEW ShareBufferClass<unsigned int>(*src.APT)    : nullptr;
	TailPosition = src.TailPosition ? W3DNEW ShareBufferClass<Vector3>(*src.TailPosition) : nullptr;
	TailDiffuse  = src.TailDiffuse  ? W3DNEW ShareBufferClass<Vector4>(*src.TailDiffuse)  : nullptr;
	UCoord       = src.UCoord       ? W3DNEW ShareBufferClass<float>(*src.UCoord)         : nullptr;
	GroupID      = src.GroupID      ? W3DNEW ShareBufferClass<unsigned char>(*src.GroupID) : nullptr;

	// ---- Color keyframes ----
	NumColorKeyFrames   = src.NumColorKeyFrames;
	ColorKeyFrameTimes  = nullptr;
	ColorKeyFrameValues = nullptr;
	ColorKeyFrameDeltas = nullptr;
	if (NumColorKeyFrames > 0) {
		ColorKeyFrameTimes  = W3DNEWARRAY unsigned int[NumColorKeyFrames];
		ColorKeyFrameValues = W3DNEWARRAY Vector3[NumColorKeyFrames];
		::memcpy(ColorKeyFrameTimes,  src.ColorKeyFrameTimes,  NumColorKeyFrames * sizeof(unsigned int));
		::memcpy(ColorKeyFrameValues, src.ColorKeyFrameValues, NumColorKeyFrames * sizeof(Vector3));
		if (NumColorKeyFrames > 1) {
			ColorKeyFrameDeltas = W3DNEWARRAY Vector3[NumColorKeyFrames - 1];
			::memcpy(ColorKeyFrameDeltas, src.ColorKeyFrameDeltas, (NumColorKeyFrames - 1) * sizeof(Vector3));
		}
	}
	NumRandomColorEntriesMinus1 = src.NumRandomColorEntriesMinus1;
	RandomColorEntries = nullptr;
	if (src.RandomColorEntries) {
		unsigned int count = NumRandomColorEntriesMinus1 + 1;
		RandomColorEntries = W3DNEWARRAY Vector3[count];
		::memcpy(RandomColorEntries, src.RandomColorEntries, count * sizeof(Vector3));
	}

	// ---- Alpha keyframes ----
	NumAlphaKeyFrames   = src.NumAlphaKeyFrames;
	AlphaKeyFrameTimes  = nullptr;
	AlphaKeyFrameValues = nullptr;
	AlphaKeyFrameDeltas = nullptr;
	if (NumAlphaKeyFrames > 0) {
		AlphaKeyFrameTimes  = W3DNEWARRAY unsigned int[NumAlphaKeyFrames];
		AlphaKeyFrameValues = W3DNEWARRAY float[NumAlphaKeyFrames];
		::memcpy(AlphaKeyFrameTimes,  src.AlphaKeyFrameTimes,  NumAlphaKeyFrames * sizeof(unsigned int));
		::memcpy(AlphaKeyFrameValues, src.AlphaKeyFrameValues, NumAlphaKeyFrames * sizeof(float));
		if (NumAlphaKeyFrames > 1) {
			AlphaKeyFrameDeltas = W3DNEWARRAY float[NumAlphaKeyFrames - 1];
			::memcpy(AlphaKeyFrameDeltas, src.AlphaKeyFrameDeltas, (NumAlphaKeyFrames - 1) * sizeof(float));
		}
	}
	NumRandomAlphaEntriesMinus1 = src.NumRandomAlphaEntriesMinus1;
	RandomAlphaEntries = nullptr;
	if (src.RandomAlphaEntries) {
		unsigned int count = NumRandomAlphaEntriesMinus1 + 1;
		RandomAlphaEntries = W3DNEWARRAY float[count];
		::memcpy(RandomAlphaEntries, src.RandomAlphaEntries, count * sizeof(float));
	}

	// ---- Size keyframes ----
	NumSizeKeyFrames   = src.NumSizeKeyFrames;
	SizeKeyFrameTimes  = nullptr;
	SizeKeyFrameValues = nullptr;
	SizeKeyFrameDeltas = nullptr;
	if (NumSizeKeyFrames > 0) {
		SizeKeyFrameTimes  = W3DNEWARRAY unsigned int[NumSizeKeyFrames];
		SizeKeyFrameValues = W3DNEWARRAY float[NumSizeKeyFrames];
		::memcpy(SizeKeyFrameTimes,  src.SizeKeyFrameTimes,  NumSizeKeyFrames * sizeof(unsigned int));
		::memcpy(SizeKeyFrameValues, src.SizeKeyFrameValues, NumSizeKeyFrames * sizeof(float));
		if (NumSizeKeyFrames > 1) {
			SizeKeyFrameDeltas = W3DNEWARRAY float[NumSizeKeyFrames - 1];
			::memcpy(SizeKeyFrameDeltas, src.SizeKeyFrameDeltas, (NumSizeKeyFrames - 1) * sizeof(float));
		}
	}
	NumRandomSizeEntriesMinus1 = src.NumRandomSizeEntriesMinus1;
	RandomSizeEntries = nullptr;
	if (src.RandomSizeEntries) {
		unsigned int count = NumRandomSizeEntriesMinus1 + 1;
		RandomSizeEntries = W3DNEWARRAY float[count];
		::memcpy(RandomSizeEntries, src.RandomSizeEntries, count * sizeof(float));
	}

	// ---- Rotation keyframes ----
	NumRotationKeyFrames       = src.NumRotationKeyFrames;
	RotationKeyFrameTimes      = nullptr;
	RotationKeyFrameValues     = nullptr;
	HalfRotationKeyFrameDeltas = nullptr;
	OrientationKeyFrameValues  = nullptr;
	if (NumRotationKeyFrames > 0) {
		RotationKeyFrameTimes  = W3DNEWARRAY unsigned int[NumRotationKeyFrames];
		RotationKeyFrameValues = W3DNEWARRAY float[NumRotationKeyFrames];
		OrientationKeyFrameValues = W3DNEWARRAY float[NumRotationKeyFrames];
		::memcpy(RotationKeyFrameTimes,     src.RotationKeyFrameTimes,     NumRotationKeyFrames * sizeof(unsigned int));
		::memcpy(RotationKeyFrameValues,    src.RotationKeyFrameValues,    NumRotationKeyFrames * sizeof(float));
		::memcpy(OrientationKeyFrameValues, src.OrientationKeyFrameValues, NumRotationKeyFrames * sizeof(float));
		if (NumRotationKeyFrames > 1) {
			HalfRotationKeyFrameDeltas = W3DNEWARRAY float[NumRotationKeyFrames - 1];
			::memcpy(HalfRotationKeyFrameDeltas, src.HalfRotationKeyFrameDeltas, (NumRotationKeyFrames - 1) * sizeof(float));
		}
	}
	NumRandomRotationEntriesMinus1 = src.NumRandomRotationEntriesMinus1;
	RandomRotationEntries = nullptr;
	if (src.RandomRotationEntries) {
		unsigned int count = NumRandomRotationEntriesMinus1 + 1;
		RandomRotationEntries = W3DNEWARRAY float[count];
		::memcpy(RandomRotationEntries, src.RandomRotationEntries, count * sizeof(float));
	}
	NumRandomOrientationEntriesMinus1 = src.NumRandomOrientationEntriesMinus1;
	RandomOrientationEntries = nullptr;
	if (src.RandomOrientationEntries) {
		unsigned int count = NumRandomOrientationEntriesMinus1 + 1;
		RandomOrientationEntries = W3DNEWARRAY float[count];
		::memcpy(RandomOrientationEntries, src.RandomOrientationEntries, count * sizeof(float));
	}

	// ---- Frame keyframes ----
	NumFrameKeyFrames   = src.NumFrameKeyFrames;
	FrameKeyFrameTimes  = nullptr;
	FrameKeyFrameValues = nullptr;
	FrameKeyFrameDeltas = nullptr;
	if (NumFrameKeyFrames > 0) {
		FrameKeyFrameTimes  = W3DNEWARRAY unsigned int[NumFrameKeyFrames];
		FrameKeyFrameValues = W3DNEWARRAY float[NumFrameKeyFrames];
		::memcpy(FrameKeyFrameTimes,  src.FrameKeyFrameTimes,  NumFrameKeyFrames * sizeof(unsigned int));
		::memcpy(FrameKeyFrameValues, src.FrameKeyFrameValues, NumFrameKeyFrames * sizeof(float));
		if (NumFrameKeyFrames > 1) {
			FrameKeyFrameDeltas = W3DNEWARRAY float[NumFrameKeyFrames - 1];
			::memcpy(FrameKeyFrameDeltas, src.FrameKeyFrameDeltas, (NumFrameKeyFrames - 1) * sizeof(float));
		}
	}
	NumRandomFrameEntriesMinus1 = src.NumRandomFrameEntriesMinus1;
	RandomFrameEntries = nullptr;
	if (src.RandomFrameEntries) {
		unsigned int count = NumRandomFrameEntriesMinus1 + 1;
		RandomFrameEntries = W3DNEWARRAY float[count];
		::memcpy(RandomFrameEntries, src.RandomFrameEntries, count * sizeof(float));
	}

	// ---- Blur time keyframes ----
	NumBlurTimeKeyFrames   = src.NumBlurTimeKeyFrames;
	BlurTimeKeyFrameTimes  = nullptr;
	BlurTimeKeyFrameValues = nullptr;
	BlurTimeKeyFrameDeltas = nullptr;
	if (NumBlurTimeKeyFrames > 0) {
		BlurTimeKeyFrameTimes  = W3DNEWARRAY unsigned int[NumBlurTimeKeyFrames];
		BlurTimeKeyFrameValues = W3DNEWARRAY float[NumBlurTimeKeyFrames];
		::memcpy(BlurTimeKeyFrameTimes,  src.BlurTimeKeyFrameTimes,  NumBlurTimeKeyFrames * sizeof(unsigned int));
		::memcpy(BlurTimeKeyFrameValues, src.BlurTimeKeyFrameValues, NumBlurTimeKeyFrames * sizeof(float));
		if (NumBlurTimeKeyFrames > 1) {
			BlurTimeKeyFrameDeltas = W3DNEWARRAY float[NumBlurTimeKeyFrames - 1];
			::memcpy(BlurTimeKeyFrameDeltas, src.BlurTimeKeyFrameDeltas, (NumBlurTimeKeyFrames - 1) * sizeof(float));
		}
	}
	NumRandomBlurTimeEntriesMinus1 = src.NumRandomBlurTimeEntriesMinus1;
	RandomBlurTimeEntries = nullptr;
	if (src.RandomBlurTimeEntries) {
		unsigned int count = NumRandomBlurTimeEntriesMinus1 + 1;
		RandomBlurTimeEntries = W3DNEWARRAY float[count];
		::memcpy(RandomBlurTimeEntries, src.RandomBlurTimeEntries, count * sizeof(float));
	}

	// Rendering sub-objects.
	PointGroup   = nullptr;
	LineRenderer = nullptr;
	LineGroup    = nullptr;
	if (src.PointGroup) {
		PointGroup = W3DNEW PointGroupClass();
		*PointGroup = *src.PointGroup;
	}
	if (src.LineRenderer) {
		LineRenderer = W3DNEW SegLineRendererClass(*src.LineRenderer);
	}
	if (src.LineGroup) {
		LineGroup = W3DNEW LineGroupClass();
	}

	TotalActiveCount++;
}

// ---------------------------------------------------------------------------
// Assignment operator
// ---------------------------------------------------------------------------
ParticleBufferClass & ParticleBufferClass::operator = (const ParticleBufferClass &)
{
	// Assignment of particle buffers is not supported.
	WWASSERT(0);
	return *this;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
ParticleBufferClass::~ParticleBufferClass()
{
	// Release per-particle arrays.
	REF_PTR_RELEASE(Position[0]);
	REF_PTR_RELEASE(Position[1]);
	delete[] Velocity;
	delete[] TimeStamp;

	REF_PTR_RELEASE(Diffuse);
	REF_PTR_RELEASE(Color);
	REF_PTR_RELEASE(Alpha);
	REF_PTR_RELEASE(Size);
	REF_PTR_RELEASE(Frame);
	REF_PTR_RELEASE(Orientation);
	REF_PTR_RELEASE(APT);
	REF_PTR_RELEASE(TailPosition);
	REF_PTR_RELEASE(TailDiffuse);
	REF_PTR_RELEASE(UCoord);
	REF_PTR_RELEASE(GroupID);

	// New-particle queue.
	delete[] NewParticleQueue;

	// Color keyframes.
	delete[] ColorKeyFrameTimes;
	delete[] ColorKeyFrameValues;
	delete[] ColorKeyFrameDeltas;
	delete[] RandomColorEntries;

	// Alpha keyframes.
	delete[] AlphaKeyFrameTimes;
	delete[] AlphaKeyFrameValues;
	delete[] AlphaKeyFrameDeltas;
	delete[] RandomAlphaEntries;

	// Size keyframes.
	delete[] SizeKeyFrameTimes;
	delete[] SizeKeyFrameValues;
	delete[] SizeKeyFrameDeltas;
	delete[] RandomSizeEntries;

	// Rotation / orientation keyframes.
	delete[] RotationKeyFrameTimes;
	delete[] RotationKeyFrameValues;
	delete[] HalfRotationKeyFrameDeltas;
	delete[] OrientationKeyFrameValues;
	delete[] RandomRotationEntries;
	delete[] RandomOrientationEntries;

	// Frame keyframes.
	delete[] FrameKeyFrameTimes;
	delete[] FrameKeyFrameValues;
	delete[] FrameKeyFrameDeltas;
	delete[] RandomFrameEntries;

	// Blur time keyframes.
	delete[] BlurTimeKeyFrameTimes;
	delete[] BlurTimeKeyFrameValues;
	delete[] BlurTimeKeyFrameDeltas;
	delete[] RandomBlurTimeEntries;

	// Rendering sub-objects.
	delete PointGroup;
	delete LineRenderer;
	delete LineGroup;

	TotalActiveCount--;
}

// ---------------------------------------------------------------------------
// Clone
// ---------------------------------------------------------------------------
RenderObjClass * ParticleBufferClass::Clone() const
{
	return W3DNEW ParticleBufferClass(*this);
}

// ---------------------------------------------------------------------------
// Get_Num_Polys
// ---------------------------------------------------------------------------
int ParticleBufferClass::Get_Num_Polys() const
{
	if (PointGroup) {
		return PointGroup->Get_Polygon_Count();
	}
	if (LineGroup) {
		return LineGroup->Get_Polygon_Count();
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Get_Particle_Count
// ---------------------------------------------------------------------------
int ParticleBufferClass::Get_Particle_Count() const
{
	return NonNewNum + NewNum;
}

// ---------------------------------------------------------------------------
// Render — update state and draw particles.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Render(RenderInfoClass & rinfo)
{
	Update_Kinematic_Particle_State();
	Update_Visual_Particle_State();

	if (RenderMode == W3D_EMITTER_RENDER_MODE_LINE) {
		Render_Line(rinfo);
	} else if (RenderMode == W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA ||
	           RenderMode == W3D_EMITTER_RENDER_MODE_LINEGRP_PRISM) {
		Render_Line_Group(rinfo);
	} else {
		Render_Particles(rinfo);
	}
}

// ---------------------------------------------------------------------------
// Scale — scale particle sizes.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Scale(float scale)
{
	for (unsigned int kf = 0; kf < NumSizeKeyFrames; ++kf) {
		SizeKeyFrameValues[kf] *= scale;
	}
	if (NumSizeKeyFrames > 1) {
		// Recompute deltas after scaling.
		for (unsigned int kf = 0; kf < NumSizeKeyFrames - 1; ++kf) {
			unsigned int dt = SizeKeyFrameTimes[kf + 1] - SizeKeyFrameTimes[kf];
			if (dt > 0) {
				SizeKeyFrameDeltas[kf] = (SizeKeyFrameValues[kf + 1] - SizeKeyFrameValues[kf]) / (float)dt;
			}
		}
	}
	MaxSize *= scale;

	// Scale the per-particle size array if present.
	if (Size) {
		float * arr = Size->Get_Array();
		for (int i = 0; i < Size->Get_Count(); ++i) {
			arr[i] *= scale;
		}
	}
}

// ---------------------------------------------------------------------------
// On_Frame_Update
// ---------------------------------------------------------------------------
void ParticleBufferClass::On_Frame_Update()
{
	Invalidate_Cached_Bounding_Volumes();

	if (Emitter) {
		Emitter->Emit();
	}

	if (Is_Complete()) {
		Remove();
	}
}

// ---------------------------------------------------------------------------
// Notify_Added / Notify_Removed
// ---------------------------------------------------------------------------
void ParticleBufferClass::Notify_Added(SceneClass * scene)
{
	RenderObjClass::Notify_Added(scene);
}

void ParticleBufferClass::Notify_Removed(SceneClass * scene)
{
	RenderObjClass::Notify_Removed(scene);
}

// ---------------------------------------------------------------------------
// Bounding volumes
// ---------------------------------------------------------------------------
void ParticleBufferClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	// Force update so the bounding box is current.
	const_cast<ParticleBufferClass *>(this)->Update_Bounding_Box();

	Vector3 center = BoundingBox.Center;
	float radius = BoundingBox.Extent.Length();
	sphere.Center = center;
	sphere.Radius = radius;
}

void ParticleBufferClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	const_cast<ParticleBufferClass *>(this)->Update_Bounding_Box();
	box = BoundingBox;
}

void ParticleBufferClass::Update_Cached_Bounding_Volumes() const
{
	const_cast<ParticleBufferClass *>(this)->Update_Bounding_Box();

	CachedBoundingBox    = BoundingBox;
	CachedBoundingSphere.Center = BoundingBox.Center;
	CachedBoundingSphere.Radius = BoundingBox.Extent.Length();

	Validate_Cached_Bounding_Volumes();
}

// ---------------------------------------------------------------------------
// LOD interface
// ---------------------------------------------------------------------------
void ParticleBufferClass::Prepare_LOD(CameraClass & camera)
{
	ProjectedArea = Get_Screen_Size(camera);

	// Build cost/value arrays for the predictive LOD optimizer.  The number
	// of LOD levels is the number of valid decimation steps (0..16 threshold).
	// We populate them lazily here.
	int total_particles = NonNewNum + NewNum;
	if (total_particles <= 0) {
		LodCount = 1;
		Cost[0]  = 0.0f;
		Value[0] = RenderObjClass::AT_MIN_LOD;
		Value[1] = RenderObjClass::AT_MAX_LOD;
		return;
	}

	LodCount = 17;
	float full_cost = (float)total_particles * ProjectedArea;
	for (int i = 0; i < 17; ++i) {
		float fraction = 1.0f - (float)i / 16.0f;
		Cost[i]  = full_cost * fraction;
		Value[i] = (i < 16) ? (Cost[i] - Cost[i + 1]) : 0.0f;
	}
	Value[17] = RenderObjClass::AT_MAX_LOD;
}

void ParticleBufferClass::Increment_LOD()
{
	if (DecimationThreshold > 0) {
		--DecimationThreshold;
	}
}

void ParticleBufferClass::Decrement_LOD()
{
	if (DecimationThreshold < 16) {
		++DecimationThreshold;
	}
}

float ParticleBufferClass::Get_Cost() const
{
	int idx = (int)DecimationThreshold;
	if (idx < 0) idx = 0;
	if (idx > 16) idx = 16;
	return Cost[idx];
}

float ParticleBufferClass::Get_Value() const
{
	int idx = (int)DecimationThreshold;
	if (idx < 0) idx = 0;
	if (idx > 17) idx = 17;
	return Value[idx];
}

float ParticleBufferClass::Get_Post_Increment_Value() const
{
	int idx = (int)DecimationThreshold - 1;
	if (idx < 0) return RenderObjClass::AT_MAX_LOD;
	if (idx > 17) idx = 17;
	return Value[idx];
}

void ParticleBufferClass::Set_LOD_Level(int lod)
{
	if (lod < 0)  lod = 0;
	if (lod > 16) lod = 16;
	DecimationThreshold = (unsigned int)lod;
}

int ParticleBufferClass::Get_LOD_Level() const
{
	return (int)DecimationThreshold;
}

int ParticleBufferClass::Get_LOD_Count() const
{
	return (int)LodCount;
}

int ParticleBufferClass::Calculate_Cost_Value_Arrays(float screen_area, float * values, float * costs) const
{
	int total_particles = NonNewNum + NewNum;
	int levels = 17;
	float full_cost = (float)total_particles * screen_area;
	for (int i = 0; i < levels; ++i) {
		float fraction = 1.0f - (float)i / 16.0f;
		costs[i]  = full_cost * fraction;
		values[i] = (i < levels - 1) ? (costs[i] - costs[i + 1]) : 0.0f;
	}
	values[levels] = RenderObjClass::AT_MAX_LOD;
	return levels;
}

// ---------------------------------------------------------------------------
// Static LOD max screen size
// ---------------------------------------------------------------------------
void ParticleBufferClass::Set_LOD_Max_Screen_Size(int lod_level, float max_screen_size)
{
	if (lod_level >= 0 && lod_level < 17) {
		LODMaxScreenSizes[lod_level] = max_screen_size;
	}
}

float ParticleBufferClass::Get_LOD_Max_Screen_Size(int lod_level)
{
	if (lod_level >= 0 && lod_level < 17) {
		return LODMaxScreenSizes[lod_level];
	}
	return NO_MAX_SCREEN_SIZE;
}

// ---------------------------------------------------------------------------
// Texture / shader accessors
// ---------------------------------------------------------------------------
TextureClass * ParticleBufferClass::Get_Texture() const
{
	if (PointGroup) {
		return PointGroup->Get_Texture();
	}
	if (LineRenderer) {
		return LineRenderer->Get_Texture();
	}
	if (LineGroup) {
		return LineGroup->Get_Texture();
	}
	return nullptr;
}

void ParticleBufferClass::Set_Texture(TextureClass * tex)
{
	if (PointGroup) {
		PointGroup->Set_Texture(tex);
	}
	if (LineRenderer) {
		LineRenderer->Set_Texture(tex);
	}
	if (LineGroup) {
		LineGroup->Set_Texture(tex);
	}
}

ShaderClass ParticleBufferClass::Get_Shader() const
{
	if (PointGroup) {
		return PointGroup->Get_Shader();
	}
	if (LineRenderer) {
		return LineRenderer->Get_Shader();
	}
	if (LineGroup) {
		return LineGroup->Get_Shader();
	}
	return ShaderClass::_PresetAdditiveSpriteShader;
}

// ---------------------------------------------------------------------------
// Line rendering property accessors — forward to LineRenderer when present.
// ---------------------------------------------------------------------------
int ParticleBufferClass::Get_Line_Texture_Mapping_Mode() const
{
	return LineRenderer ? (int)LineRenderer->Get_Texture_Mapping_Mode() : 0;
}

int ParticleBufferClass::Is_Merge_Intersections() const
{
	return LineRenderer ? LineRenderer->Is_Merge_Intersections() : 0;
}

int ParticleBufferClass::Is_Freeze_Random() const
{
	return LineRenderer ? LineRenderer->Is_Freeze_Random() : 0;
}

int ParticleBufferClass::Is_Sorting_Disabled() const
{
	return LineRenderer ? LineRenderer->Is_Sorting_Disabled() : 0;
}

int ParticleBufferClass::Are_End_Caps_Enabled() const
{
	return LineRenderer ? LineRenderer->Are_End_Caps_Enabled() : 0;
}

int ParticleBufferClass::Get_Subdivision_Level() const
{
	return LineRenderer ? (int)LineRenderer->Get_Current_Subdivision_Level() : 0;
}

float ParticleBufferClass::Get_Noise_Amplitude() const
{
	return LineRenderer ? LineRenderer->Get_Noise_Amplitude() : 0.0f;
}

float ParticleBufferClass::Get_Merge_Abort_Factor() const
{
	return LineRenderer ? LineRenderer->Get_Merge_Abort_Factor() : 0.0f;
}

float ParticleBufferClass::Get_Texture_Tile_Factor() const
{
	return LineRenderer ? LineRenderer->Get_Texture_Tile_Factor() : 1.0f;
}

Vector2 ParticleBufferClass::Get_UV_Offset_Rate() const
{
	return LineRenderer ? LineRenderer->Get_UV_Offset_Rate() : Vector2(0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// Emitter link management
// ---------------------------------------------------------------------------
void ParticleBufferClass::Emitter_Is_Dead()
{
	IsEmitterDead = true;
	Emitter = nullptr;
}

void ParticleBufferClass::Set_Emitter(ParticleEmitterClass * emitter)
{
	Emitter = emitter;
}

// ---------------------------------------------------------------------------
// Add_Uninitialized_New_Particle
// Returns a pointer to the next slot in the new-particle queue for the
// emitter to fill. If the queue is full we drop the oldest entry.
// ---------------------------------------------------------------------------
NewParticleStruct * ParticleBufferClass::Add_Uninitialized_New_Particle()
{
	NewParticleStruct * slot = &NewParticleQueue[NewParticleQueueEnd];

	if (NewParticleQueueCount < (int)MaxNum) {
		++NewParticleQueueCount;
	} else {
		// Queue is full — advance start to discard the oldest.
		NewParticleQueueStart = (NewParticleQueueStart + 1) % MaxNum;
	}

	NewParticleQueueEnd = (NewParticleQueueEnd + 1) % MaxNum;
	return slot;
}

// ---------------------------------------------------------------------------
// Get_New_Particles — consume the new-particle queue and write into the
// circular particle buffer.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Get_New_Particles()
{
	if (NewParticleQueueCount <= 0) {
		return;
	}

	unsigned int now = WW3D::Get_Sync_Time();

	while (NewParticleQueueCount > 0) {
		NewParticleStruct & np = NewParticleQueue[NewParticleQueueStart];
		NewParticleQueueStart = (NewParticleQueueStart + 1) % MaxNum;
		--NewParticleQueueCount;

		// Check that this particle's future start time has passed.
		if (now < np.TimeStamp + FutureStartTime) {
			continue;
		}

		// If the main particle buffer is full, evict the oldest particle to
		// make room.  The oldest is at Start.
		if (NonNewNum + NewNum >= (int)MaxNum) {
			if (NonNewNum > 0) {
				Start = (Start + 1) % MaxNum;
				--NonNewNum;
			} else {
				// All particles are new — discard the oldest new one.
				End = (End + 1) % MaxNum;
				--NewNum;
			}
		}

		// Write into the new-particle section [End .. NewEnd).
		unsigned int slot = NewEnd;
		NewEnd = (NewEnd + 1) % MaxNum;
		++NewNum;

		Position[0]->Get_Array()[slot] = np.Position;
		if (PingPongPosition && Position[1]) {
			Position[1]->Get_Array()[slot] = np.Position;
		}
		Velocity[slot]  = np.Velocity;
		TimeStamp[slot] = np.TimeStamp;

		// Apply partial-frame physics: advance position from creation time to now.
		unsigned int age = now - np.TimeStamp;
		if (age > 0) {
			float t = (float)age;
			Vector3 & pos = Position[0]->Get_Array()[slot];
			pos += Velocity[slot] * t;
			if (HasAccel) {
				pos += Accel * (0.5f * t * t);
				Velocity[slot] += Accel * t;
			}
			if (PingPongPosition && Position[1]) {
				Position[1]->Get_Array()[slot] = pos;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Kill_Old_Particles — advance Start past particles that have exceeded MaxAge.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Kill_Old_Particles()
{
	unsigned int now = WW3D::Get_Sync_Time();

	while (NonNewNum > 0) {
		unsigned int age = now - TimeStamp[Start];
		if (age < MaxAge) {
			break;
		}
		Start = (Start + 1) % MaxNum;
		--NonNewNum;
	}
}

// ---------------------------------------------------------------------------
// Update_Non_New_Particles — advance positions and velocities for all
// non-new particles over the given elapsed time in milliseconds.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Update_Non_New_Particles(unsigned int elapsed)
{
	if (elapsed == 0 || NonNewNum <= 0) {
		return;
	}

	float t = (float)elapsed;
	Vector3 * positions = Position[0]->Get_Array();
	Vector3 * prev_positions = PingPongPosition && Position[1] ? Position[1]->Get_Array() : nullptr;

	unsigned int idx = Start;
	for (int i = 0; i < NonNewNum; ++i) {
		if (prev_positions) {
			prev_positions[idx] = positions[idx];
		}
		positions[idx] += Velocity[idx] * t;
		if (HasAccel) {
			positions[idx] += Accel * (0.5f * t * t);
			Velocity[idx]  += Accel * t;
		}
		idx = (idx + 1) % MaxNum;
	}
}

// ---------------------------------------------------------------------------
// Update_Kinematic_Particle_State
// ---------------------------------------------------------------------------
void ParticleBufferClass::Update_Kinematic_Particle_State()
{
	unsigned int now = WW3D::Get_Sync_Time();
	unsigned int elapsed = now - LastUpdateTime;

	Get_New_Particles();
	Kill_Old_Particles();
	Update_Non_New_Particles(elapsed);

	// Merge new particles into non-new.
	NonNewNum += NewNum;
	End        = NewEnd;
	NewNum     = 0;

	LastUpdateTime = now;
	BoundingBoxDirty = true;
}

// ---------------------------------------------------------------------------
// Update_Visual_Particle_State — interpolate all keyframe properties for
// every living particle.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Update_Visual_Particle_State()
{
	int total = NonNewNum + NewNum;
	if (total <= 0) {
		return;
	}

	unsigned int now = WW3D::Get_Sync_Time();

	// Ensure per-particle visual arrays exist (create lazily).
	bool need_color = (NumColorKeyFrames > 1 || ColorRandom.X != 0.0f ||
	                   ColorRandom.Y != 0.0f || ColorRandom.Z != 0.0f);
	bool need_alpha = (NumAlphaKeyFrames > 1 || OpacityRandom != 0.0f);
	bool need_size  = (NumSizeKeyFrames > 1  || SizeRandom != 0.0f);
	bool need_orient = (NumRotationKeyFrames > 0 || InitialOrientationRandom != 0.0f);
	bool need_frame  = (NumFrameKeyFrames > 1 || FrameRandom != 0.0f);

	if (need_color && !Color) {
		Color = W3DNEW ShareBufferClass<Vector3>(MaxNum, "ParticleBuf::Color");
	}
	if (need_alpha && !Alpha) {
		Alpha = W3DNEW ShareBufferClass<float>(MaxNum, "ParticleBuf::Alpha");
	}
	if (need_size && !Size) {
		Size = W3DNEW ShareBufferClass<float>(MaxNum, "ParticleBuf::Size");
	}
	if (need_orient && !Orientation) {
		Orientation = W3DNEW ShareBufferClass<uint8>(MaxNum, "ParticleBuf::Orientation");
	}
	if (need_frame && !Frame) {
		Frame = W3DNEW ShareBufferClass<uint8>(MaxNum, "ParticleBuf::Frame");
	}

	Vector3 * color_arr  = Color       ? Color->Get_Array()       : nullptr;
	float *   alpha_arr  = Alpha       ? Alpha->Get_Array()        : nullptr;
	float *   size_arr   = Size        ? Size->Get_Array()         : nullptr;
	uint8 *   orient_arr = Orientation ? Orientation->Get_Array()  : nullptr;
	uint8 *   frame_arr  = Frame       ? Frame->Get_Array()        : nullptr;
	float *   ucoord_arr = UCoord      ? UCoord->Get_Array()       : nullptr;

	unsigned int idx = Start;
	for (int i = 0; i < NonNewNum; ++i) {
		unsigned int age = now - TimeStamp[idx];

		// --- Color ---
		if (color_arr) {
			Vector3 c = ColorKeyFrameValues[0];
			if (NumColorKeyFrames > 1) {
				// Walk backwards to find the bracketing keyframe.
				unsigned int kf = NumColorKeyFrames - 1;
				while (kf > 0 && ColorKeyFrameTimes[kf] > age) {
					--kf;
				}
				float delta_t = (float)(age - ColorKeyFrameTimes[kf]);
				c = ColorKeyFrameValues[kf];
				if (kf < NumColorKeyFrames - 1 && ColorKeyFrameDeltas) {
					c += ColorKeyFrameDeltas[kf] * delta_t;
				}
			}
			// Add per-particle random offset.
			if (RandomColorEntries) {
				c += RandomColorEntries[idx & NumRandomColorEntriesMinus1];
			}
			color_arr[idx] = c;
		}

		// --- Alpha ---
		if (alpha_arr) {
			float a = AlphaKeyFrameValues[0];
			if (NumAlphaKeyFrames > 1) {
				unsigned int kf = NumAlphaKeyFrames - 1;
				while (kf > 0 && AlphaKeyFrameTimes[kf] > age) {
					--kf;
				}
				float delta_t = (float)(age - AlphaKeyFrameTimes[kf]);
				a = AlphaKeyFrameValues[kf];
				if (kf < NumAlphaKeyFrames - 1 && AlphaKeyFrameDeltas) {
					a += AlphaKeyFrameDeltas[kf] * delta_t;
				}
			}
			if (RandomAlphaEntries) {
				a += RandomAlphaEntries[idx & NumRandomAlphaEntriesMinus1];
			}
			alpha_arr[idx] = a;
		}

		// --- Size ---
		if (size_arr) {
			float s = SizeKeyFrameValues[0];
			if (NumSizeKeyFrames > 1) {
				unsigned int kf = NumSizeKeyFrames - 1;
				while (kf > 0 && SizeKeyFrameTimes[kf] > age) {
					--kf;
				}
				float delta_t = (float)(age - SizeKeyFrameTimes[kf]);
				s = SizeKeyFrameValues[kf];
				if (kf < NumSizeKeyFrames - 1 && SizeKeyFrameDeltas) {
					s += SizeKeyFrameDeltas[kf] * delta_t;
				}
			}
			if (RandomSizeEntries) {
				s += RandomSizeEntries[idx & NumRandomSizeEntriesMinus1];
			}
			size_arr[idx] = s;
		}

		// --- Orientation (quadratic integration of rotation) ---
		if (orient_arr) {
			float orient_f = 0.0f;
			if (NumRotationKeyFrames > 0 && RotationKeyFrameValues) {
				// Find bracketing keyframe.
				unsigned int kf = NumRotationKeyFrames - 1;
				while (kf > 0 && RotationKeyFrameTimes[kf] > age) {
					--kf;
				}
				// Preintegrated orientation at keyframe.
				orient_f = OrientationKeyFrameValues[kf];
				// Add quadratic contribution within the segment.
				if (kf < NumRotationKeyFrames - 1 && HalfRotationKeyFrameDeltas) {
					float dt = (float)(age - RotationKeyFrameTimes[kf]);
					float omega = RotationKeyFrameValues[kf];
					float half_delta = HalfRotationKeyFrameDeltas[kf];
					orient_f += omega * dt + half_delta * dt * dt;
				} else {
					// Beyond last keyframe: use constant last rotation rate.
					float dt = (float)(age - RotationKeyFrameTimes[kf]);
					orient_f += RotationKeyFrameValues[kf] * dt;
				}
			}
			// Add random orientation offset.
			if (RandomOrientationEntries) {
				orient_f += RandomOrientationEntries[idx & NumRandomOrientationEntriesMinus1];
			}
			// Convert to 0..255 range (1 full rotation = 256 units).
			int orient_i = (int)(orient_f * 256.0f / (2.0f * WWMATH_PI));
			orient_arr[idx] = (uint8)(orient_i & 0xFF);
		}

		// --- Frame ---
		if (frame_arr || ucoord_arr) {
			float f = FrameKeyFrameValues ? FrameKeyFrameValues[0] : 0.0f;
			if (NumFrameKeyFrames > 1) {
				unsigned int kf = NumFrameKeyFrames - 1;
				while (kf > 0 && FrameKeyFrameTimes[kf] > age) {
					--kf;
				}
				float delta_t = (float)(age - FrameKeyFrameTimes[kf]);
				f = FrameKeyFrameValues[kf];
				if (kf < NumFrameKeyFrames - 1 && FrameKeyFrameDeltas) {
					f += FrameKeyFrameDeltas[kf] * delta_t;
				}
			}
			if (RandomFrameEntries) {
				f += RandomFrameEntries[idx & NumRandomFrameEntriesMinus1];
			}
			if (frame_arr) {
				frame_arr[idx] = (uint8)((int)f & 0xFF);
			}
			if (ucoord_arr) {
				ucoord_arr[idx] = f;
			}
		}

		idx = (idx + 1) % MaxNum;
	}
}

// ---------------------------------------------------------------------------
// Combine_Color_And_Alpha — merge Color and Alpha arrays into Diffuse (RGBA).
// ---------------------------------------------------------------------------
void ParticleBufferClass::Combine_Color_And_Alpha()
{
	int total = NonNewNum + NewNum;
	if (total <= 0) {
		return;
	}

	if (!Diffuse) {
		Diffuse = W3DNEW ShareBufferClass<Vector4>(MaxNum, "ParticleBuf::Diffuse");
	}

	Vector4 * diffuse_arr = Diffuse->Get_Array();

	// Constant fallbacks when per-particle arrays are null.
	Vector3 const_color = ColorKeyFrameValues ? ColorKeyFrameValues[0] : Vector3(1.0f, 1.0f, 1.0f);
	float   const_alpha = AlphaKeyFrameValues ? AlphaKeyFrameValues[0] : 1.0f;

	Vector3 * color_arr = Color ? Color->Get_Array() : nullptr;
	float *   alpha_arr = Alpha ? Alpha->Get_Array() : nullptr;

	unsigned int idx = Start;
	for (int i = 0; i < NonNewNum; ++i) {
		Vector3 c = color_arr ? color_arr[idx] : const_color;
		float   a = alpha_arr ? alpha_arr[idx] : const_alpha;

		// Clamp to [0, 1].
		c.X = WWMath::Clamp(c.X, 0.0f, 1.0f);
		c.Y = WWMath::Clamp(c.Y, 0.0f, 1.0f);
		c.Z = WWMath::Clamp(c.Z, 0.0f, 1.0f);
		a   = WWMath::Clamp(a,   0.0f, 1.0f);

		diffuse_arr[idx].Set(c.X, c.Y, c.Z, a);
		idx = (idx + 1) % MaxNum;
	}
}

// ---------------------------------------------------------------------------
// Update_Bounding_Box — scan all particle positions and compute AABB.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Update_Bounding_Box()
{
	if (!BoundingBoxDirty) {
		return;
	}
	BoundingBoxDirty = false;

	int total = NonNewNum + NewNum;
	if (total <= 0) {
		BoundingBox.Center.Set(0.0f, 0.0f, 0.0f);
		BoundingBox.Extent.Set(0.0f, 0.0f, 0.0f);
		return;
	}

	Vector3 * positions = Position[0]->Get_Array();
	unsigned int idx = Start;

	Vector3 mn = positions[idx];
	Vector3 mx = positions[idx];
	idx = (idx + 1) % MaxNum;

	for (int i = 1; i < NonNewNum; ++i) {
		const Vector3 & p = positions[idx];
		if (p.X < mn.X) mn.X = p.X;
		if (p.Y < mn.Y) mn.Y = p.Y;
		if (p.Z < mn.Z) mn.Z = p.Z;
		if (p.X > mx.X) mx.X = p.X;
		if (p.Y > mx.Y) mx.Y = p.Y;
		if (p.Z > mx.Z) mx.Z = p.Z;
		idx = (idx + 1) % MaxNum;
	}

	// Extend by MaxSize so particles at the edges are fully enclosed.
	float half_size = MaxSize * 0.5f;
	mn -= Vector3(half_size, half_size, half_size);
	mx += Vector3(half_size, half_size, half_size);

	BoundingBox.Center = (mn + mx) * 0.5f;
	BoundingBox.Extent = (mx - mn) * 0.5f;
}

// ---------------------------------------------------------------------------
// Generate_APT — build an active-point table if LOD decimation is active.
// Returns the active count through active_point_count.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Generate_APT(ShareBufferClass<unsigned int> ** apt,
	unsigned int & active_point_count)
{
	int total = NonNewNum + NewNum;
	active_point_count = (unsigned int)total;

	if (DecimationThreshold == 0) {
		// No decimation — no APT needed.
		*apt = nullptr;
		return;
	}

	if (!APT || APT->Get_Count() < (int)MaxNum) {
		REF_PTR_RELEASE(APT);
		APT = W3DNEW ShareBufferClass<unsigned int>(MaxNum, "ParticleBuf::APT");
	}

	unsigned int * apt_arr = APT->Get_Array();
	unsigned int count = 0;
	unsigned int idx = Start;
	for (int i = 0; i < NonNewNum; ++i) {
		if (PermutationArray[idx & 0xF] >= DecimationThreshold) {
			apt_arr[count++] = idx;
		}
		idx = (idx + 1) % MaxNum;
	}

	active_point_count = count;
	if (count == (unsigned int)total) {
		*apt = nullptr;	// All active — no APT needed after all.
	} else {
		*apt = APT;
	}
}

// ---------------------------------------------------------------------------
// Render_Particles — combine colour, generate APT, call PointGroup.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Render_Particles(RenderInfoClass & rinfo)
{
	int total = NonNewNum + NewNum;
	if (total <= 0 || !PointGroup) {
		return;
	}

	Combine_Color_And_Alpha();

	unsigned int active_count = 0;
	ShareBufferClass<unsigned int> * apt = nullptr;
	Generate_APT(&apt, active_count);

	PointGroup->Set_Arrays(
		Position[0],
		Diffuse,
		apt,
		Size,
		reinterpret_cast<ShareBufferClass<unsigned char> *>(Orientation),
		reinterpret_cast<ShareBufferClass<unsigned char> *>(Frame),
		(int)active_count);

	PointGroup->Render(rinfo);
}

// ---------------------------------------------------------------------------
// Render_Line — render as a segmented line through all particle positions.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Render_Line(RenderInfoClass & rinfo)
{
	int total = NonNewNum + NewNum;
	if (total <= 1 || !LineRenderer) {
		return;
	}

	Combine_Color_And_Alpha();

	// Build a contiguous array of positions for the line renderer.
	// For a small number of particles this temporary copy is acceptable.
	Vector3 * positions = Position[0]->Get_Array();
	Vector4 * diffuse   = Diffuse    ? Diffuse->Get_Array()   : nullptr;

	// Linearize the circular buffer into temporary heap arrays.
	Vector3 * lin_pos  = W3DNEWARRAY Vector3[NonNewNum];
	Vector4 * lin_diff = diffuse ? W3DNEWARRAY Vector4[NonNewNum] : nullptr;

	unsigned int idx = Start;
	for (int i = 0; i < NonNewNum; ++i) {
		lin_pos[i] = positions[idx];
		if (lin_diff) {
			lin_diff[i] = diffuse[idx];
		}
		idx = (idx + 1) % MaxNum;
	}

	SphereClass sphere;
	Get_Obj_Space_Bounding_Sphere(sphere);

	LineRenderer->Render(
		rinfo,
		Get_Transform(),
		(unsigned int)NonNewNum,
		lin_pos,
		sphere,
		lin_diff);

	delete[] lin_pos;
	delete[] lin_diff;
}

// ---------------------------------------------------------------------------
// Render_Line_Group — render as a group of line segments (motion blur).
// ---------------------------------------------------------------------------
void ParticleBufferClass::Render_Line_Group(RenderInfoClass & rinfo)
{
	int total = NonNewNum + NewNum;
	if (total <= 0 || !LineGroup || !PingPongPosition || !Position[1]) {
		return;
	}

	Combine_Color_And_Alpha();

	// Build tail diffuse.
	TailDiffuseTypeEnum tail_type = Determine_Tail_Diffuse();
	if (TailDiffuse) {
		Vector4 * tail_arr = TailDiffuse->Get_Array();
		Vector4 * head_arr = Diffuse ? Diffuse->Get_Array() : nullptr;
		unsigned int idx = Start;
		for (int i = 0; i < NonNewNum; ++i) {
			switch (tail_type) {
			case BLACK:
				tail_arr[idx].Set(0.0f, 0.0f, 0.0f, 1.0f);
				break;
			case WHITE:
				tail_arr[idx].Set(1.0f, 1.0f, 1.0f, 1.0f);
				break;
			case SAME_AS_HEAD:
				tail_arr[idx] = head_arr ? head_arr[idx] : Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				break;
			case SAME_AS_HEAD_ALPHA_ZERO:
				if (head_arr) {
					tail_arr[idx] = head_arr[idx];
					tail_arr[idx].W = 0.0f;
				} else {
					tail_arr[idx].Set(1.0f, 1.0f, 1.0f, 0.0f);
				}
				break;
			}
			idx = (idx + 1) % MaxNum;
		}
	}

	unsigned int active_count = 0;
	ShareBufferClass<unsigned int> * apt = nullptr;
	Generate_APT(&apt, active_count);

	LineGroup->Set_Arrays(
		Position[0],
		Position[1],
		Diffuse,
		TailDiffuse,
		apt,
		Size,
		UCoord,
		(int)active_count);

	LineGroup->Render(rinfo);
}

// ---------------------------------------------------------------------------
// Determine_Tail_Diffuse — pick the tail colour mode based on the shader.
// ---------------------------------------------------------------------------
ParticleBufferClass::TailDiffuseTypeEnum ParticleBufferClass::Determine_Tail_Diffuse()
{
	// Use SAME_AS_HEAD_ALPHA_ZERO (motion blur style) as the default.
	return SAME_AS_HEAD_ALPHA_ZERO;
}

// ---------------------------------------------------------------------------
// Reset_Colors — rebuild colour keyframe and random tables.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Reset_Colors(ParticlePropertyStruct<Vector3> & props)
{
	delete[] ColorKeyFrameTimes;
	delete[] ColorKeyFrameValues;
	delete[] ColorKeyFrameDeltas;
	delete[] RandomColorEntries;
	ColorKeyFrameTimes  = nullptr;
	ColorKeyFrameValues = nullptr;
	ColorKeyFrameDeltas = nullptr;
	RandomColorEntries  = nullptr;
	REF_PTR_RELEASE(Color);

	ColorRandom = props.Rand;

	// Total keyframes = 1 (start) + extra keyframes.
	NumColorKeyFrames = 1 + props.NumKeyFrames;
	ColorKeyFrameTimes  = W3DNEWARRAY unsigned int[NumColorKeyFrames];
	ColorKeyFrameValues = W3DNEWARRAY Vector3[NumColorKeyFrames];

	ColorKeyFrameTimes[0]  = 0;
	ColorKeyFrameValues[0] = props.Start;

	for (unsigned int i = 0; i < props.NumKeyFrames; ++i) {
		ColorKeyFrameTimes[i + 1]  = (unsigned int)(props.KeyTimes[i] * 1000.0f);
		ColorKeyFrameValues[i + 1] = props.Values[i];
	}

	if (NumColorKeyFrames > 1) {
		ColorKeyFrameDeltas = W3DNEWARRAY Vector3[NumColorKeyFrames - 1];
		for (unsigned int i = 0; i < NumColorKeyFrames - 1; ++i) {
			unsigned int dt = ColorKeyFrameTimes[i + 1] - ColorKeyFrameTimes[i];
			if (dt > 0) {
				Vector3 dv = ColorKeyFrameValues[i + 1] - ColorKeyFrameValues[i];
				ColorKeyFrameDeltas[i] = dv * (1.0f / (float)dt);
			} else {
				ColorKeyFrameDeltas[i].Set(0.0f, 0.0f, 0.0f);
			}
		}
	}

	bool is_constant = (NumColorKeyFrames <= 1) &&
	                   (ColorRandom.X == 0.0f && ColorRandom.Y == 0.0f && ColorRandom.Z == 0.0f);
	if (!is_constant) {
		NumRandomColorEntriesMinus1 = Build_Vector3_Random_Table(
			&RandomColorEntries, MaxNum, ColorRandom);
		// Allocate the per-particle color array.
		Color = W3DNEW ShareBufferClass<Vector3>(MaxNum, "ParticleBuf::Color");
	}
}

// ---------------------------------------------------------------------------
// Reset_Opacity
// ---------------------------------------------------------------------------
void ParticleBufferClass::Reset_Opacity(ParticlePropertyStruct<float> & props)
{
	delete[] AlphaKeyFrameTimes;
	delete[] AlphaKeyFrameValues;
	delete[] AlphaKeyFrameDeltas;
	delete[] RandomAlphaEntries;
	AlphaKeyFrameTimes  = nullptr;
	AlphaKeyFrameValues = nullptr;
	AlphaKeyFrameDeltas = nullptr;
	RandomAlphaEntries  = nullptr;
	REF_PTR_RELEASE(Alpha);

	OpacityRandom = props.Rand;

	NumAlphaKeyFrames = 1 + props.NumKeyFrames;
	AlphaKeyFrameTimes  = W3DNEWARRAY unsigned int[NumAlphaKeyFrames];
	AlphaKeyFrameValues = W3DNEWARRAY float[NumAlphaKeyFrames];

	AlphaKeyFrameTimes[0]  = 0;
	AlphaKeyFrameValues[0] = props.Start;

	for (unsigned int i = 0; i < props.NumKeyFrames; ++i) {
		AlphaKeyFrameTimes[i + 1]  = (unsigned int)(props.KeyTimes[i] * 1000.0f);
		AlphaKeyFrameValues[i + 1] = props.Values[i];
	}

	if (NumAlphaKeyFrames > 1) {
		AlphaKeyFrameDeltas = W3DNEWARRAY float[NumAlphaKeyFrames - 1];
		for (unsigned int i = 0; i < NumAlphaKeyFrames - 1; ++i) {
			unsigned int dt = AlphaKeyFrameTimes[i + 1] - AlphaKeyFrameTimes[i];
			AlphaKeyFrameDeltas[i] = (dt > 0) ?
				(AlphaKeyFrameValues[i + 1] - AlphaKeyFrameValues[i]) / (float)dt : 0.0f;
		}
	}

	bool is_constant = (NumAlphaKeyFrames <= 1) && (OpacityRandom == 0.0f);
	if (!is_constant) {
		NumRandomAlphaEntriesMinus1 = Build_Float_Random_Table(
			&RandomAlphaEntries, MaxNum, OpacityRandom);
		Alpha = W3DNEW ShareBufferClass<float>(MaxNum, "ParticleBuf::Alpha");
	}
}

// ---------------------------------------------------------------------------
// Reset_Size
// ---------------------------------------------------------------------------
void ParticleBufferClass::Reset_Size(ParticlePropertyStruct<float> & props)
{
	delete[] SizeKeyFrameTimes;
	delete[] SizeKeyFrameValues;
	delete[] SizeKeyFrameDeltas;
	delete[] RandomSizeEntries;
	SizeKeyFrameTimes  = nullptr;
	SizeKeyFrameValues = nullptr;
	SizeKeyFrameDeltas = nullptr;
	RandomSizeEntries  = nullptr;
	REF_PTR_RELEASE(Size);

	SizeRandom = props.Rand;

	NumSizeKeyFrames = 1 + props.NumKeyFrames;
	SizeKeyFrameTimes  = W3DNEWARRAY unsigned int[NumSizeKeyFrames];
	SizeKeyFrameValues = W3DNEWARRAY float[NumSizeKeyFrames];

	SizeKeyFrameTimes[0]  = 0;
	SizeKeyFrameValues[0] = props.Start;
	MaxSize = props.Start;

	for (unsigned int i = 0; i < props.NumKeyFrames; ++i) {
		SizeKeyFrameTimes[i + 1]  = (unsigned int)(props.KeyTimes[i] * 1000.0f);
		SizeKeyFrameValues[i + 1] = props.Values[i];
		if (props.Values[i] > MaxSize) {
			MaxSize = props.Values[i];
		}
	}
	MaxSize += SizeRandom;

	if (NumSizeKeyFrames > 1) {
		SizeKeyFrameDeltas = W3DNEWARRAY float[NumSizeKeyFrames - 1];
		for (unsigned int i = 0; i < NumSizeKeyFrames - 1; ++i) {
			unsigned int dt = SizeKeyFrameTimes[i + 1] - SizeKeyFrameTimes[i];
			SizeKeyFrameDeltas[i] = (dt > 0) ?
				(SizeKeyFrameValues[i + 1] - SizeKeyFrameValues[i]) / (float)dt : 0.0f;
		}
	}

	bool is_constant = (NumSizeKeyFrames <= 1) && (SizeRandom == 0.0f);
	if (!is_constant) {
		NumRandomSizeEntriesMinus1 = Build_Float_Random_Table(
			&RandomSizeEntries, MaxNum, SizeRandom);
		Size = W3DNEW ShareBufferClass<float>(MaxNum, "ParticleBuf::Size");
	}
}

// ---------------------------------------------------------------------------
// Reset_Rotations
// ---------------------------------------------------------------------------
void ParticleBufferClass::Reset_Rotations(ParticlePropertyStruct<float> & props,
	float orient_rnd)
{
	delete[] RotationKeyFrameTimes;
	delete[] RotationKeyFrameValues;
	delete[] HalfRotationKeyFrameDeltas;
	delete[] OrientationKeyFrameValues;
	delete[] RandomRotationEntries;
	delete[] RandomOrientationEntries;
	RotationKeyFrameTimes      = nullptr;
	RotationKeyFrameValues     = nullptr;
	HalfRotationKeyFrameDeltas = nullptr;
	OrientationKeyFrameValues  = nullptr;
	RandomRotationEntries      = nullptr;
	RandomOrientationEntries   = nullptr;
	REF_PTR_RELEASE(Orientation);

	RotationRandom           = props.Rand;
	InitialOrientationRandom = orient_rnd;

	bool has_rotation = (props.NumKeyFrames > 0 || props.Start != 0.0f || RotationRandom != 0.0f);
	bool has_orient   = (orient_rnd != 0.0f);

	if (!has_rotation && !has_orient) {
		NumRotationKeyFrames              = 0;
		NumRandomRotationEntriesMinus1    = 0;
		NumRandomOrientationEntriesMinus1 = 0;
		return;
	}

	// Build rotation keyframes.  Convert from rotations/second to rotations/ms.
	NumRotationKeyFrames = 1 + props.NumKeyFrames;
	RotationKeyFrameTimes     = W3DNEWARRAY unsigned int[NumRotationKeyFrames];
	RotationKeyFrameValues    = W3DNEWARRAY float[NumRotationKeyFrames];
	OrientationKeyFrameValues = W3DNEWARRAY float[NumRotationKeyFrames];

	RotationKeyFrameTimes[0]  = 0;
	// Convert from rotations/s → radians/ms.
	RotationKeyFrameValues[0] = props.Start * (2.0f * WWMATH_PI / 1000.0f);
	OrientationKeyFrameValues[0] = 0.0f;

	for (unsigned int i = 0; i < props.NumKeyFrames; ++i) {
		unsigned int t = (unsigned int)(props.KeyTimes[i] * 1000.0f);
		RotationKeyFrameTimes[i + 1]  = t;
		RotationKeyFrameValues[i + 1] = props.Values[i] * (2.0f * WWMATH_PI / 1000.0f);
	}

	// Pre-integrate orientation at each keyframe and compute half-deltas.
	if (NumRotationKeyFrames > 1) {
		HalfRotationKeyFrameDeltas = W3DNEWARRAY float[NumRotationKeyFrames - 1];
		for (unsigned int i = 0; i < NumRotationKeyFrames - 1; ++i) {
			unsigned int dt = RotationKeyFrameTimes[i + 1] - RotationKeyFrameTimes[i];
			float omega0 = RotationKeyFrameValues[i];
			float omega1 = RotationKeyFrameValues[i + 1];
			float d_omega = (dt > 0) ? (omega1 - omega0) / (float)dt : 0.0f;
			HalfRotationKeyFrameDeltas[i] = d_omega * 0.5f;
			// Integrate using trapezoidal rule to get orientation at next keyframe.
			OrientationKeyFrameValues[i + 1] = OrientationKeyFrameValues[i] +
				(omega0 + omega1) * 0.5f * (float)dt;
		}
	}

	if (RotationRandom != 0.0f) {
		// Convert random range from rot/s to rad/ms.
		float rnd_rad = RotationRandom * (2.0f * WWMATH_PI / 1000.0f);
		NumRandomRotationEntriesMinus1 = Build_Float_Random_Table(
			&RandomRotationEntries, MaxNum, rnd_rad);
	}
	if (orient_rnd != 0.0f) {
		// orient_rnd is in degrees — convert to radians.
		float rnd_rad = orient_rnd * (WWMATH_PI / 180.0f);
		NumRandomOrientationEntriesMinus1 = Build_Float_Random_Table(
			&RandomOrientationEntries, MaxNum, rnd_rad);
	}

	Orientation = W3DNEW ShareBufferClass<uint8>(MaxNum, "ParticleBuf::Orientation");
}

// ---------------------------------------------------------------------------
// Reset_Frames
// ---------------------------------------------------------------------------
void ParticleBufferClass::Reset_Frames(ParticlePropertyStruct<float> & props)
{
	delete[] FrameKeyFrameTimes;
	delete[] FrameKeyFrameValues;
	delete[] FrameKeyFrameDeltas;
	delete[] RandomFrameEntries;
	FrameKeyFrameTimes  = nullptr;
	FrameKeyFrameValues = nullptr;
	FrameKeyFrameDeltas = nullptr;
	RandomFrameEntries  = nullptr;
	REF_PTR_RELEASE(Frame);

	FrameRandom = props.Rand;

	NumFrameKeyFrames = 1 + props.NumKeyFrames;
	FrameKeyFrameTimes  = W3DNEWARRAY unsigned int[NumFrameKeyFrames];
	FrameKeyFrameValues = W3DNEWARRAY float[NumFrameKeyFrames];

	FrameKeyFrameTimes[0]  = 0;
	FrameKeyFrameValues[0] = props.Start;

	for (unsigned int i = 0; i < props.NumKeyFrames; ++i) {
		FrameKeyFrameTimes[i + 1]  = (unsigned int)(props.KeyTimes[i] * 1000.0f);
		FrameKeyFrameValues[i + 1] = props.Values[i];
	}

	if (NumFrameKeyFrames > 1) {
		FrameKeyFrameDeltas = W3DNEWARRAY float[NumFrameKeyFrames - 1];
		for (unsigned int i = 0; i < NumFrameKeyFrames - 1; ++i) {
			unsigned int dt = FrameKeyFrameTimes[i + 1] - FrameKeyFrameTimes[i];
			FrameKeyFrameDeltas[i] = (dt > 0) ?
				(FrameKeyFrameValues[i + 1] - FrameKeyFrameValues[i]) / (float)dt : 0.0f;
		}
	}

	bool is_constant = (NumFrameKeyFrames <= 1) && (FrameRandom == 0.0f);
	if (!is_constant) {
		NumRandomFrameEntriesMinus1 = Build_Float_Random_Table(
			&RandomFrameEntries, MaxNum, FrameRandom);
		Frame = W3DNEW ShareBufferClass<uint8>(MaxNum, "ParticleBuf::Frame");
	}
}

// ---------------------------------------------------------------------------
// Reset_Blur_Times
// ---------------------------------------------------------------------------
void ParticleBufferClass::Reset_Blur_Times(ParticlePropertyStruct<float> & props)
{
	delete[] BlurTimeKeyFrameTimes;
	delete[] BlurTimeKeyFrameValues;
	delete[] BlurTimeKeyFrameDeltas;
	delete[] RandomBlurTimeEntries;
	BlurTimeKeyFrameTimes  = nullptr;
	BlurTimeKeyFrameValues = nullptr;
	BlurTimeKeyFrameDeltas = nullptr;
	RandomBlurTimeEntries  = nullptr;

	BlurTimeRandom = props.Rand;

	NumBlurTimeKeyFrames = 1 + props.NumKeyFrames;
	BlurTimeKeyFrameTimes  = W3DNEWARRAY unsigned int[NumBlurTimeKeyFrames];
	BlurTimeKeyFrameValues = W3DNEWARRAY float[NumBlurTimeKeyFrames];

	BlurTimeKeyFrameTimes[0]  = 0;
	BlurTimeKeyFrameValues[0] = props.Start;

	for (unsigned int i = 0; i < props.NumKeyFrames; ++i) {
		BlurTimeKeyFrameTimes[i + 1]  = (unsigned int)(props.KeyTimes[i] * 1000.0f);
		BlurTimeKeyFrameValues[i + 1] = props.Values[i];
	}

	if (NumBlurTimeKeyFrames > 1) {
		BlurTimeKeyFrameDeltas = W3DNEWARRAY float[NumBlurTimeKeyFrames - 1];
		for (unsigned int i = 0; i < NumBlurTimeKeyFrames - 1; ++i) {
			unsigned int dt = BlurTimeKeyFrameTimes[i + 1] - BlurTimeKeyFrameTimes[i];
			BlurTimeKeyFrameDeltas[i] = (dt > 0) ?
				(BlurTimeKeyFrameValues[i + 1] - BlurTimeKeyFrameValues[i]) / (float)dt : 0.0f;
		}
	}

	if (BlurTimeRandom != 0.0f) {
		NumRandomBlurTimeEntriesMinus1 = Build_Float_Random_Table(
			&RandomBlurTimeEntries, MaxNum, BlurTimeRandom);
	}
}

// ---------------------------------------------------------------------------
// Get_*_Key_Frames — export keyframe data back to callers.
// ---------------------------------------------------------------------------
void ParticleBufferClass::Get_Color_Key_Frames(ParticlePropertyStruct<Vector3> & colors) const
{
	colors.Start = ColorKeyFrameValues ? ColorKeyFrameValues[0] : Vector3(1.0f, 1.0f, 1.0f);
	colors.Rand  = ColorRandom;
	colors.NumKeyFrames = (NumColorKeyFrames > 0) ? NumColorKeyFrames - 1 : 0;
	colors.KeyTimes = nullptr;
	colors.Values   = nullptr;
	if (colors.NumKeyFrames > 0) {
		colors.KeyTimes = W3DNEWARRAY float[colors.NumKeyFrames];
		colors.Values   = W3DNEWARRAY Vector3[colors.NumKeyFrames];
		for (unsigned int i = 0; i < colors.NumKeyFrames; ++i) {
			colors.KeyTimes[i] = (float)ColorKeyFrameTimes[i + 1] / 1000.0f;
			colors.Values[i]   = ColorKeyFrameValues[i + 1];
		}
	}
}

void ParticleBufferClass::Get_Opacity_Key_Frames(ParticlePropertyStruct<float> & opacities) const
{
	opacities.Start = AlphaKeyFrameValues ? AlphaKeyFrameValues[0] : 1.0f;
	opacities.Rand  = OpacityRandom;
	opacities.NumKeyFrames = (NumAlphaKeyFrames > 0) ? NumAlphaKeyFrames - 1 : 0;
	opacities.KeyTimes = nullptr;
	opacities.Values   = nullptr;
	if (opacities.NumKeyFrames > 0) {
		opacities.KeyTimes = W3DNEWARRAY float[opacities.NumKeyFrames];
		opacities.Values   = W3DNEWARRAY float[opacities.NumKeyFrames];
		for (unsigned int i = 0; i < opacities.NumKeyFrames; ++i) {
			opacities.KeyTimes[i] = (float)AlphaKeyFrameTimes[i + 1] / 1000.0f;
			opacities.Values[i]   = AlphaKeyFrameValues[i + 1];
		}
	}
}

void ParticleBufferClass::Get_Size_Key_Frames(ParticlePropertyStruct<float> & sizes) const
{
	sizes.Start = SizeKeyFrameValues ? SizeKeyFrameValues[0] : 1.0f;
	sizes.Rand  = SizeRandom;
	sizes.NumKeyFrames = (NumSizeKeyFrames > 0) ? NumSizeKeyFrames - 1 : 0;
	sizes.KeyTimes = nullptr;
	sizes.Values   = nullptr;
	if (sizes.NumKeyFrames > 0) {
		sizes.KeyTimes = W3DNEWARRAY float[sizes.NumKeyFrames];
		sizes.Values   = W3DNEWARRAY float[sizes.NumKeyFrames];
		for (unsigned int i = 0; i < sizes.NumKeyFrames; ++i) {
			sizes.KeyTimes[i] = (float)SizeKeyFrameTimes[i + 1] / 1000.0f;
			sizes.Values[i]   = SizeKeyFrameValues[i + 1];
		}
	}
}

void ParticleBufferClass::Get_Rotation_Key_Frames(ParticlePropertyStruct<float> & rotations) const
{
	// Convert internal rad/ms back to rotations/s.
	const float inv_scale = 1000.0f / (2.0f * WWMATH_PI);
	rotations.Start = (RotationKeyFrameValues && NumRotationKeyFrames > 0)
		? RotationKeyFrameValues[0] * inv_scale : 0.0f;
	rotations.Rand  = RotationRandom * inv_scale;
	rotations.NumKeyFrames = (NumRotationKeyFrames > 0) ? NumRotationKeyFrames - 1 : 0;
	rotations.KeyTimes = nullptr;
	rotations.Values   = nullptr;
	if (rotations.NumKeyFrames > 0) {
		rotations.KeyTimes = W3DNEWARRAY float[rotations.NumKeyFrames];
		rotations.Values   = W3DNEWARRAY float[rotations.NumKeyFrames];
		for (unsigned int i = 0; i < rotations.NumKeyFrames; ++i) {
			rotations.KeyTimes[i] = (float)RotationKeyFrameTimes[i + 1] / 1000.0f;
			rotations.Values[i]   = RotationKeyFrameValues[i + 1] * inv_scale;
		}
	}
}

void ParticleBufferClass::Get_Frame_Key_Frames(ParticlePropertyStruct<float> & frames) const
{
	frames.Start = FrameKeyFrameValues ? FrameKeyFrameValues[0] : 0.0f;
	frames.Rand  = FrameRandom;
	frames.NumKeyFrames = (NumFrameKeyFrames > 0) ? NumFrameKeyFrames - 1 : 0;
	frames.KeyTimes = nullptr;
	frames.Values   = nullptr;
	if (frames.NumKeyFrames > 0) {
		frames.KeyTimes = W3DNEWARRAY float[frames.NumKeyFrames];
		frames.Values   = W3DNEWARRAY float[frames.NumKeyFrames];
		for (unsigned int i = 0; i < frames.NumKeyFrames; ++i) {
			frames.KeyTimes[i] = (float)FrameKeyFrameTimes[i + 1] / 1000.0f;
			frames.Values[i]   = FrameKeyFrameValues[i + 1];
		}
	}
}

void ParticleBufferClass::Get_Blur_Time_Key_Frames(ParticlePropertyStruct<float> & blurtimes) const
{
	blurtimes.Start = BlurTimeKeyFrameValues ? BlurTimeKeyFrameValues[0] : 0.0f;
	blurtimes.Rand  = BlurTimeRandom;
	blurtimes.NumKeyFrames = (NumBlurTimeKeyFrames > 0) ? NumBlurTimeKeyFrames - 1 : 0;
	blurtimes.KeyTimes = nullptr;
	blurtimes.Values   = nullptr;
	if (blurtimes.NumKeyFrames > 0) {
		blurtimes.KeyTimes = W3DNEWARRAY float[blurtimes.NumKeyFrames];
		blurtimes.Values   = W3DNEWARRAY float[blurtimes.NumKeyFrames];
		for (unsigned int i = 0; i < blurtimes.NumKeyFrames; ++i) {
			blurtimes.KeyTimes[i] = (float)BlurTimeKeyFrameTimes[i + 1] / 1000.0f;
			blurtimes.Values[i]   = BlurTimeKeyFrameValues[i + 1];
		}
	}
}
