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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/part_ldr.cpp         $*
 *                                                                                             *
 *                       Author:: Patrick Smith                                                *
 *                                                                                             *
 *                     $Modtime:: 10/26/01 2:57p                                              $*
 *                                                                                             *
 *                    $Revision:: 11                                                          $*
 *                                                                                             *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "part_ldr.h"
#include "part_emt.h"
#include "w3derr.h"
#include "chunkio.h"
#include "win.h"
#include "assetmgr.h"
#include "texture.h"

#ifndef SAFE_DELETE
#define SAFE_DELETE(pointer) \
{ \
	if (pointer) {	\
		delete pointer; \
		pointer = 0; \
	} \
}
#endif //SAFE_DELETE

#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(pointer)	\
	if (pointer) {					\
		delete [] pointer;			\
		pointer = 0;				\
	}

#endif //SAFE_DELETE_ARRAY


///////////////////////////////////////////////////////////////////////////////////
//
//	Global variable initialization
//
ParticleEmitterLoaderClass	_ParticleEmitterLoader;

//	This array is declared in "W3D_File.H"
const char *EMITTER_TYPE_NAMES[EMITTER_TYPEID_COUNT] =
{
	"Default"
};


///////////////////////////////////////////////////////////////////////////////////
//
//	ParticleEmitterDefClass -- default constructor
//
ParticleEmitterDefClass::ParticleEmitterDefClass (void)
	:	m_pName (NULL),
		m_Version (0L),
		m_pUserString (NULL),
		m_iUserType (EMITTER_TYPEID_DEFAULT),
		m_InitialOrientationRandom (0),
		m_pCreationVolume (NULL),
		m_pVelocityRandomizer (NULL)
{
	::memset (&m_Info, 0, sizeof (m_Info));
	::memset (&m_InfoV2, 0, sizeof (m_InfoV2));
	::memset (&m_ExtraInfo, 0, sizeof (m_ExtraInfo));

	::memset (&m_ColorKeyframes, 0, sizeof (m_ColorKeyframes));
	::memset (&m_OpacityKeyframes, 0, sizeof (m_OpacityKeyframes));
	::memset (&m_SizeKeyframes, 0, sizeof (m_SizeKeyframes));
	::memset (&m_RotationKeyframes, 0, sizeof (m_RotationKeyframes));
	::memset (&m_FrameKeyframes, 0, sizeof (m_FrameKeyframes));
	::memset (&m_BlurTimeKeyframes, 0, sizeof (m_BlurTimeKeyframes));
	::memset (&m_LineProperties, 0, sizeof (m_LineProperties));
}


///////////////////////////////////////////////////////////////////////////////////
//
//	ParticleEmitterDefClass -- copy constructor
//
ParticleEmitterDefClass::ParticleEmitterDefClass (const ParticleEmitterDefClass &src)
	:	m_pName (NULL),
		m_Version (0L),
		m_pUserString (NULL),
		m_iUserType (EMITTER_TYPEID_DEFAULT),
		m_InitialOrientationRandom (src.m_InitialOrientationRandom),
		m_pCreationVolume (NULL),
		m_pVelocityRandomizer (NULL)
{
	::memset (&m_Info, 0, sizeof (m_Info));
	::memset (&m_InfoV2, 0, sizeof (m_InfoV2));
	::memset (&m_ExtraInfo, 0, sizeof (m_ExtraInfo));

	::memset (&m_ColorKeyframes, 0, sizeof (m_ColorKeyframes));
	::memset (&m_OpacityKeyframes, 0, sizeof (m_OpacityKeyframes));
	::memset (&m_SizeKeyframes, 0, sizeof (m_SizeKeyframes));
	::memset (&m_RotationKeyframes, 0, sizeof (m_RotationKeyframes));
	::memset (&m_FrameKeyframes, 0, sizeof (m_FrameKeyframes));
	::memset (&m_BlurTimeKeyframes, 0, sizeof (m_BlurTimeKeyframes));
	::memset (&m_LineProperties, 0, sizeof (m_LineProperties));

	(*this) = src;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	~ParticleEmitterDefClass
//
ParticleEmitterDefClass::~ParticleEmitterDefClass (void)
{
	if (m_pName != NULL) {
		::free (m_pName);
		m_pName = NULL;
	}

	if (m_pUserString != NULL) {
		::free (m_pUserString);
		m_pUserString = NULL;
	}

	Free_Props ();

	SAFE_DELETE (m_pCreationVolume);
	SAFE_DELETE (m_pVelocityRandomizer);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	operator=
//
const ParticleEmitterDefClass &
ParticleEmitterDefClass::operator= (const ParticleEmitterDefClass &src)
{
	Set_Name (src.Get_Name ());
	Set_User_String (src.Get_User_String ());
	Set_User_Type (src.Get_User_Type ());
	m_Version = src.m_Version;

	::memcpy (&m_Info, &src.m_Info, sizeof (m_Info));
	::memcpy (&m_InfoV2, &src.m_InfoV2, sizeof (m_InfoV2));
	::memcpy (&m_ExtraInfo, &src.m_ExtraInfo, sizeof (m_ExtraInfo));
	::memcpy (&m_LineProperties, &src.m_LineProperties, sizeof (m_LineProperties));

	Free_Props ();
	::Copy_Emitter_Property_Struct (m_ColorKeyframes, src.m_ColorKeyframes);
	::Copy_Emitter_Property_Struct (m_OpacityKeyframes, src.m_OpacityKeyframes);
	::Copy_Emitter_Property_Struct (m_SizeKeyframes, src.m_SizeKeyframes);
	::Copy_Emitter_Property_Struct (m_RotationKeyframes, src.m_RotationKeyframes);
	::Copy_Emitter_Property_Struct (m_FrameKeyframes, src.m_FrameKeyframes);
	::Copy_Emitter_Property_Struct (m_BlurTimeKeyframes, src.m_BlurTimeKeyframes);
	m_InitialOrientationRandom = src.m_InitialOrientationRandom;

	SAFE_DELETE (m_pCreationVolume);
	SAFE_DELETE (m_pVelocityRandomizer);
	m_pCreationVolume      = Create_Randomizer (m_InfoV2.CreationVolume);
	m_pVelocityRandomizer  = Create_Randomizer (m_InfoV2.VelRandom);
	return (*this);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Free_Props -- release all heap-allocated keyframe arrays
//
void
ParticleEmitterDefClass::Free_Props (void)
{
	m_ColorKeyframes.NumKeyFrames    = 0;
	m_OpacityKeyframes.NumKeyFrames  = 0;
	m_SizeKeyframes.NumKeyFrames     = 0;
	m_RotationKeyframes.NumKeyFrames = 0;
	m_FrameKeyframes.NumKeyFrames    = 0;
	m_BlurTimeKeyframes.NumKeyFrames = 0;

	SAFE_DELETE_ARRAY (m_ColorKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_ColorKeyframes.Values);
	SAFE_DELETE_ARRAY (m_OpacityKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_OpacityKeyframes.Values);
	SAFE_DELETE_ARRAY (m_SizeKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_SizeKeyframes.Values);
	SAFE_DELETE_ARRAY (m_RotationKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_RotationKeyframes.Values);
	SAFE_DELETE_ARRAY (m_FrameKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_FrameKeyframes.Values);
	SAFE_DELETE_ARRAY (m_BlurTimeKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_BlurTimeKeyframes.Values);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Velocity_Random
//
void
ParticleEmitterDefClass::Set_Velocity_Random (Vector3Randomizer *randomizer)
{
	SAFE_DELETE (m_pVelocityRandomizer);
	m_pVelocityRandomizer = randomizer;

	if (m_pVelocityRandomizer != NULL) {
		Initialize_Randomizer_Struct (*m_pVelocityRandomizer, m_InfoV2.VelRandom);
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Creation_Volume
//
void
ParticleEmitterDefClass::Set_Creation_Volume (Vector3Randomizer *randomizer)
{
	SAFE_DELETE (m_pCreationVolume);
	m_pCreationVolume = randomizer;

	if (m_pCreationVolume != NULL) {
		Initialize_Randomizer_Struct (*m_pCreationVolume, m_InfoV2.CreationVolume);
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_User_String
//
void
ParticleEmitterDefClass::Set_User_String (const char *pstring)
{
	SAFE_FREE (m_pUserString);
	m_pUserString = ::_strdup (pstring);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Name
//
void
ParticleEmitterDefClass::Set_Name (const char *pname)
{
	SAFE_FREE (m_pName);
	m_pName = ::_strdup (pname);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Texture_Filename
//
void
ParticleEmitterDefClass::Set_Texture_Filename (const char *pname)
{
	::lstrcpy (m_Info.TextureFilename, pname);
	Normalize_Filename ();
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Normalize_Filename -- strip any directory prefix, keep bare filename
//
void
ParticleEmitterDefClass::Normalize_Filename (void)
{
	TCHAR path[MAX_PATH];
	::lstrcpy (path, m_Info.TextureFilename);

	LPCTSTR filename = ::strrchr (path, '\\');
	if (filename != NULL) {
		filename++;
		::lstrcpy (m_Info.TextureFilename, filename);
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Load_W3D -- top-level loader; reads all emitter chunks
//
WW3DErrorType
ParticleEmitterDefClass::Load_W3D (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;
	Initialize_To_Ver2 ();

	if ((Read_Header (chunk_load) == WW3D_ERROR_OK) &&
	    (Read_User_Data (chunk_load) == WW3D_ERROR_OK) &&
	    (Read_Info (chunk_load) == WW3D_ERROR_OK))
	{
		if (m_Version > 0x00010000) {
			if ((Read_InfoV2 (chunk_load) == WW3D_ERROR_OK) &&
			    (Read_Props (chunk_load) == WW3D_ERROR_OK))
			{
				ret_val = WW3D_ERROR_OK;
			}
		} else {
			Convert_To_Ver2 ();
			ret_val = WW3D_ERROR_OK;
		}
	}

	// Handle optional extension chunks (line props, keyframe variants, extra info)
	while (chunk_load.Open_Chunk() && ret_val == WW3D_ERROR_OK) {

		switch (chunk_load.Cur_Chunk_ID())
		{
			case W3D_CHUNK_EMITTER_LINE_PROPERTIES:
				ret_val = Read_Line_Properties (chunk_load);
				break;

			case W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES:
				ret_val = Read_Rotation_Keyframes (chunk_load);
				break;

			case W3D_CHUNK_EMITTER_FRAME_KEYFRAMES:
				ret_val = Read_Frame_Keyframes (chunk_load);
				break;

			case W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES:
				ret_val = Read_Blur_Time_Keyframes (chunk_load);
				break;

			case W3D_CHUNK_EMITTER_EXTRA_INFO:
				ret_val = Read_Extra_Info (chunk_load);
				break;

			default:
				WWDEBUG_SAY(("Unhandled Chunk! File: %s Line: %d\r\n", __FILE__, __LINE__));
				break;
		}

		chunk_load.Close_Chunk();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Initialize_To_Ver2 -- set all fields to clean version-2 defaults
//
void
ParticleEmitterDefClass::Initialize_To_Ver2 (void)
{
	::memset (&m_Info, 0, sizeof (m_Info));
	::memset (&m_InfoV2, 0, sizeof (m_InfoV2));
	::memset (&m_ExtraInfo, 0, sizeof (m_ExtraInfo));

	m_InfoV2.BurstSize   = 1;
	m_InfoV2.OutwardVel  = 0;
	m_InfoV2.VelInherit  = 0;
	W3dUtilityClass::Convert_Shader (ShaderClass::_PresetAdditiveSpriteShader, &m_InfoV2.Shader);

	m_InfoV2.CreationVolume.ClassID = Vector3Randomizer::CLASSID_SOLIDBOX;
	m_InfoV2.CreationVolume.Value1  = 0;
	m_InfoV2.CreationVolume.Value2  = 0;
	m_InfoV2.CreationVolume.Value3  = 0;

	m_InfoV2.VelRandom.ClassID = Vector3Randomizer::CLASSID_SOLIDBOX;
	m_InfoV2.VelRandom.Value1  = 0;
	m_InfoV2.VelRandom.Value2  = 0;
	m_InfoV2.VelRandom.Value3  = 0;

	Free_Props ();
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Convert_To_Ver2 -- upgrade a version-1 emitter to version-2 layout
//
void
ParticleEmitterDefClass::Convert_To_Ver2 (void)
{
	if (m_Version < 0x00020000) {
		m_InfoV2.BurstSize  = 1;
		m_InfoV2.OutwardVel = 0;
		m_InfoV2.VelInherit = 0;

		// Select shader based on whether the texture has an alpha channel
		ShaderClass shader = ShaderClass::_PresetAdditiveSpriteShader;
		TextureClass *ptexture = WW3DAssetManager::Get_Instance ()->Get_Texture (m_Info.TextureFilename);
		if (ptexture != NULL) {
			if (Has_Alpha (ptexture->Get_Texture_Format ())) {
				shader = ShaderClass::_PresetAlphaSpriteShader;
			}
			ptexture->Release_Ref ();
		}
		W3dUtilityClass::Convert_Shader (shader, &m_InfoV2.Shader);

		// Map old scalar randomizers to axis-aligned box randomizers
		m_InfoV2.CreationVolume.ClassID = Vector3Randomizer::CLASSID_SOLIDBOX;
		m_InfoV2.CreationVolume.Value1  = m_Info.PositionRandom / 1000.0f;
		m_InfoV2.CreationVolume.Value2  = m_Info.PositionRandom / 1000.0f;
		m_InfoV2.CreationVolume.Value3  = m_Info.PositionRandom / 1000.0f;

		m_InfoV2.VelRandom.ClassID = Vector3Randomizer::CLASSID_SOLIDBOX;
		m_InfoV2.VelRandom.Value1  = m_Info.VelocityRandom;
		m_InfoV2.VelRandom.Value2  = m_Info.VelocityRandom;
		m_InfoV2.VelRandom.Value3  = m_Info.VelocityRandom;

		SAFE_DELETE (m_pCreationVolume);
		SAFE_DELETE (m_pVelocityRandomizer);
		m_pCreationVolume     = Create_Randomizer (m_InfoV2.CreationVolume);
		m_pVelocityRandomizer = Create_Randomizer (m_InfoV2.VelRandom);

		// Synthesize keyframes from start/end color, opacity, and size
		Free_Props ();
		m_ColorKeyframes.Start         = RGBA_TO_VECTOR3 (m_Info.StartColor);
		m_ColorKeyframes.Rand          = Vector3 (0, 0, 0);
		m_ColorKeyframes.NumKeyFrames  = 1;
		m_ColorKeyframes.KeyTimes      = W3DNEW float (m_Info.FadeTime);
		m_ColorKeyframes.Values        = W3DNEW Vector3 (RGBA_TO_VECTOR3 (m_Info.EndColor));

		m_OpacityKeyframes.Start        = ((float)(m_Info.StartColor.A)) / 255.0f;
		m_OpacityKeyframes.Rand         = 0;
		m_OpacityKeyframes.NumKeyFrames = 1;
		m_OpacityKeyframes.KeyTimes     = W3DNEW float (m_Info.FadeTime);
		m_OpacityKeyframes.Values       = W3DNEW float (((float)(m_Info.EndColor.A)) / 255.0f);

		m_SizeKeyframes.Start        = m_Info.StartSize;
		m_SizeKeyframes.Rand         = 0;
		m_SizeKeyframes.NumKeyFrames = 0;
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Header
//
WW3DErrorType
ParticleEmitterDefClass::Read_Header (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;

	if (chunk_load.Open_Chunk () &&
	    (chunk_load.Cur_Chunk_ID () == W3D_CHUNK_EMITTER_HEADER))
	{
		W3dEmitterHeaderStruct header = { 0 };
		if (chunk_load.Read (&header, sizeof (header)) == sizeof (header)) {
			m_pName  = ::_strdup (header.Name);
			m_Version = header.Version;
			ret_val  = WW3D_ERROR_OK;
		}
		chunk_load.Close_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_User_Data
//
WW3DErrorType
ParticleEmitterDefClass::Read_User_Data (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;

	if (chunk_load.Open_Chunk () &&
	    (chunk_load.Cur_Chunk_ID () == W3D_CHUNK_EMITTER_USER_DATA))
	{
		W3dEmitterUserInfoStruct user_info = { 0 };
		if (chunk_load.Read (&user_info, sizeof (user_info)) == sizeof (user_info)) {
			ret_val      = WW3D_ERROR_OK;
			m_iUserType  = user_info.Type;

			if (user_info.SizeofStringParam > 0) {
				m_pUserString = (char *)::malloc (sizeof (char) * (user_info.SizeofStringParam + 1));
				m_pUserString[0] = 0;
				if (chunk_load.Read (m_pUserString, user_info.SizeofStringParam) != user_info.SizeofStringParam) {
					ret_val = WW3D_ERROR_LOAD_FAILED;
				}
			}
		}
		chunk_load.Close_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Info
//
WW3DErrorType
ParticleEmitterDefClass::Read_Info (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;

	if (chunk_load.Open_Chunk () &&
	    (chunk_load.Cur_Chunk_ID () == W3D_CHUNK_EMITTER_INFO))
	{
		::memset (&m_Info, 0, sizeof (m_Info));
		if (chunk_load.Read (&m_Info, sizeof (m_Info)) == sizeof (m_Info)) {
			ret_val = WW3D_ERROR_OK;
		}
		chunk_load.Close_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_InfoV2
//
WW3DErrorType
ParticleEmitterDefClass::Read_InfoV2 (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;

	if (chunk_load.Open_Chunk () &&
	    (chunk_load.Cur_Chunk_ID () == W3D_CHUNK_EMITTER_INFOV2))
	{
		::memset (&m_InfoV2, 0, sizeof (m_InfoV2));
		if (chunk_load.Read (&m_InfoV2, sizeof (m_InfoV2)) == sizeof (m_InfoV2)) {
			SAFE_DELETE (m_pCreationVolume);
			SAFE_DELETE (m_pVelocityRandomizer);
			m_pCreationVolume     = Create_Randomizer (m_InfoV2.CreationVolume);
			m_pVelocityRandomizer = Create_Randomizer (m_InfoV2.VelRandom);
			ret_val = WW3D_ERROR_OK;
		}
		chunk_load.Close_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Props -- reads the color/opacity/size keyframe bundle
//
WW3DErrorType
ParticleEmitterDefClass::Read_Props (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;
	Free_Props ();

	if (chunk_load.Open_Chunk () &&
	    (chunk_load.Cur_Chunk_ID () == W3D_CHUNK_EMITTER_PROPS))
	{
		W3dEmitterPropertyStruct info = { 0 };
		if (chunk_load.Read (&info, sizeof (info)) == sizeof (info)) {
			unsigned int index = 0;

			m_ColorKeyframes.NumKeyFrames   = info.ColorKeyframes - 1;
			m_OpacityKeyframes.NumKeyFrames = info.OpacityKeyframes - 1;
			m_SizeKeyframes.NumKeyFrames    = info.SizeKeyframes - 1;
			m_ColorKeyframes.Rand   = RGBA_TO_VECTOR3 (info.ColorRandom);
			m_OpacityKeyframes.Rand = info.OpacityRandom;
			m_SizeKeyframes.Rand    = info.SizeRandom;

			if (m_ColorKeyframes.NumKeyFrames > 0) {
				m_ColorKeyframes.KeyTimes = W3DNEWARRAY float[m_ColorKeyframes.NumKeyFrames];
				m_ColorKeyframes.Values   = W3DNEWARRAY Vector3[m_ColorKeyframes.NumKeyFrames];
			}
			if (m_OpacityKeyframes.NumKeyFrames > 0) {
				m_OpacityKeyframes.KeyTimes = W3DNEWARRAY float[m_OpacityKeyframes.NumKeyFrames];
				m_OpacityKeyframes.Values   = W3DNEWARRAY float[m_OpacityKeyframes.NumKeyFrames];
			}
			if (m_SizeKeyframes.NumKeyFrames > 0) {
				m_SizeKeyframes.KeyTimes = W3DNEWARRAY float[m_SizeKeyframes.NumKeyFrames];
				m_SizeKeyframes.Values   = W3DNEWARRAY float[m_SizeKeyframes.NumKeyFrames];
			}

			// Color keyframes: first entry is the start value (time is ignored)
			Read_Color_Keyframe (chunk_load, NULL, &m_ColorKeyframes.Start);
			for (index = 0; index < m_ColorKeyframes.NumKeyFrames; index++) {
				Read_Color_Keyframe (chunk_load,
				                    &m_ColorKeyframes.KeyTimes[index],
				                    &m_ColorKeyframes.Values[index]);
			}

			// If the last color is black and a randomizer is present, push it below
			// zero so the randomized result still fades cleanly to black.
			int last = (int)(m_ColorKeyframes.NumKeyFrames) - 1;
			if (last > 0 &&
			    m_ColorKeyframes.Values[last].X == 0 &&
			    m_ColorKeyframes.Values[last].Y == 0 &&
			    m_ColorKeyframes.Values[last].Z == 0 &&
			    (m_ColorKeyframes.Rand.X > 0 || m_ColorKeyframes.Rand.Y > 0 || m_ColorKeyframes.Rand.Z > 0))
			{
				m_ColorKeyframes.Values[last].X = -m_ColorKeyframes.Rand.X;
				m_ColorKeyframes.Values[last].Y = -m_ColorKeyframes.Rand.Y;
				m_ColorKeyframes.Values[last].Z = -m_ColorKeyframes.Rand.Z;
			}

			// Opacity keyframes
			Read_Opacity_Keyframe (chunk_load, NULL, &m_OpacityKeyframes.Start);
			for (index = 0; index < m_OpacityKeyframes.NumKeyFrames; index++) {
				Read_Opacity_Keyframe (chunk_load,
				                      &m_OpacityKeyframes.KeyTimes[index],
				                      &m_OpacityKeyframes.Values[index]);
			}

			// Size keyframes
			Read_Size_Keyframe (chunk_load, NULL, &m_SizeKeyframes.Start);
			for (index = 0; index < m_SizeKeyframes.NumKeyFrames; index++) {
				Read_Size_Keyframe (chunk_load,
				                   &m_SizeKeyframes.KeyTimes[index],
				                   &m_SizeKeyframes.Values[index]);
			}

			ret_val = WW3D_ERROR_OK;
		}
		chunk_load.Close_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Color_Keyframe
//
bool
ParticleEmitterDefClass::Read_Color_Keyframe (ChunkLoadClass &chunk_load,
                                              float *key_time,
                                              Vector3 *value)
{
	W3dEmitterColorKeyframeStruct key_frame = { 0 };
	if (chunk_load.Read (&key_frame, sizeof (key_frame)) != sizeof (key_frame)) {
		return false;
	}
	if (key_time != NULL) {
		(*key_time) = key_frame.Time;
	}
	if (value != NULL) {
		(*value) = RGBA_TO_VECTOR3 (key_frame.Color);
	}
	return true;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Opacity_Keyframe
//
bool
ParticleEmitterDefClass::Read_Opacity_Keyframe (ChunkLoadClass &chunk_load,
                                                float *key_time,
                                                float *value)
{
	W3dEmitterOpacityKeyframeStruct key_frame = { 0 };
	if (chunk_load.Read (&key_frame, sizeof (key_frame)) != sizeof (key_frame)) {
		return false;
	}
	if (key_time != NULL) {
		(*key_time) = key_frame.Time;
	}
	if (value != NULL) {
		(*value) = key_frame.Opacity;
	}
	return true;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Size_Keyframe
//
bool
ParticleEmitterDefClass::Read_Size_Keyframe (ChunkLoadClass &chunk_load,
                                             float *key_time,
                                             float *value)
{
	W3dEmitterSizeKeyframeStruct key_frame = { 0 };
	if (chunk_load.Read (&key_frame, sizeof (key_frame)) != sizeof (key_frame)) {
		return false;
	}
	if (key_time != NULL) {
		(*key_time) = key_frame.Time;
	}
	if (value != NULL) {
		(*value) = key_frame.Size;
	}
	return true;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Line_Properties
//
WW3DErrorType
ParticleEmitterDefClass::Read_Line_Properties (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;

	if (chunk_load.Cur_Chunk_ID () == W3D_CHUNK_EMITTER_LINE_PROPERTIES) {
		if (chunk_load.Read (&m_LineProperties, sizeof (m_LineProperties)) == sizeof (m_LineProperties)) {
			ret_val = WW3D_ERROR_OK;
		}
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Rotation_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Read_Rotation_Keyframes (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_OK;

	W3dEmitterRotationHeaderStruct header;
	if (chunk_load.Read (&header, sizeof (header)) != sizeof (header)) {
		ret_val = WW3D_ERROR_LOAD_FAILED;
	}
	m_RotationKeyframes.NumKeyFrames = header.KeyframeCount;
	m_RotationKeyframes.Rand         = header.Random;
	m_InitialOrientationRandom       = header.OrientationRandom;

	// First key holds the start value
	W3dEmitterRotationKeyframeStruct key;
	if (chunk_load.Read (&key, sizeof (key)) == sizeof (key)) {
		m_RotationKeyframes.Start = key.Rotation;
	}

	if (m_RotationKeyframes.NumKeyFrames > 0) {
		m_RotationKeyframes.KeyTimes = W3DNEWARRAY float[m_RotationKeyframes.NumKeyFrames];
		m_RotationKeyframes.Values   = W3DNEWARRAY float[m_RotationKeyframes.NumKeyFrames];
	}

	for (unsigned int i = 0; (i < header.KeyframeCount) && (ret_val == WW3D_ERROR_OK); i++) {
		W3dEmitterRotationKeyframeStruct k;
		if (chunk_load.Read (&k, sizeof (k)) == sizeof (k)) {
			m_RotationKeyframes.KeyTimes[i] = k.Time;
			m_RotationKeyframes.Values[i]   = k.Rotation;
		} else {
			m_RotationKeyframes.KeyTimes[i] = 0.0f;
			m_RotationKeyframes.Values[i]   = 0.0f;
			ret_val = WW3D_ERROR_LOAD_FAILED;
		}
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Frame_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Read_Frame_Keyframes (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_OK;

	W3dEmitterFrameHeaderStruct header;
	if (chunk_load.Read (&header, sizeof (header)) != sizeof (header)) {
		ret_val = WW3D_ERROR_LOAD_FAILED;
	}

	// First key holds the start value
	W3dEmitterFrameKeyframeStruct key;
	if (chunk_load.Read (&key, sizeof (key)) == sizeof (key)) {
		m_FrameKeyframes.Start = key.Frame;
	}

	m_FrameKeyframes.NumKeyFrames = header.KeyframeCount;
	m_FrameKeyframes.Rand         = header.Random;

	if (m_FrameKeyframes.NumKeyFrames > 0) {
		m_FrameKeyframes.KeyTimes = W3DNEWARRAY float[m_FrameKeyframes.NumKeyFrames];
		m_FrameKeyframes.Values   = W3DNEWARRAY float[m_FrameKeyframes.NumKeyFrames];
	}

	for (unsigned int i = 0; (i < header.KeyframeCount) && (ret_val == WW3D_ERROR_OK); i++) {
		W3dEmitterFrameKeyframeStruct k;
		if (chunk_load.Read (&k, sizeof (k)) != sizeof (k)) {
			ret_val = WW3D_ERROR_LOAD_FAILED;
		}
		m_FrameKeyframes.KeyTimes[i] = k.Time;
		m_FrameKeyframes.Values[i]   = k.Frame;
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Blur_Time_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Read_Blur_Time_Keyframes (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_OK;

	W3dEmitterBlurTimeHeaderStruct header;
	if (chunk_load.Read (&header, sizeof (header)) != sizeof (header)) {
		ret_val = WW3D_ERROR_LOAD_FAILED;
	}

	// First key holds the start value
	W3dEmitterBlurTimeKeyframeStruct key;
	if (chunk_load.Read (&key, sizeof (key)) == sizeof (key)) {
		m_BlurTimeKeyframes.Start = key.BlurTime;
	}

	m_BlurTimeKeyframes.NumKeyFrames = header.KeyframeCount;
	m_BlurTimeKeyframes.Rand         = header.Random;

	if (m_BlurTimeKeyframes.NumKeyFrames > 0) {
		m_BlurTimeKeyframes.KeyTimes = W3DNEWARRAY float[m_BlurTimeKeyframes.NumKeyFrames];
		m_BlurTimeKeyframes.Values   = W3DNEWARRAY float[m_BlurTimeKeyframes.NumKeyFrames];
	}

	for (unsigned int i = 0; (i < header.KeyframeCount) && (ret_val == WW3D_ERROR_OK); i++) {
		W3dEmitterBlurTimeKeyframeStruct k;
		if (chunk_load.Read (&k, sizeof (k)) != sizeof (k)) {
			ret_val = WW3D_ERROR_LOAD_FAILED;
		}
		m_BlurTimeKeyframes.KeyTimes[i] = k.Time;
		m_BlurTimeKeyframes.Values[i]   = k.BlurTime;
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Read_Extra_Info
//
WW3DErrorType
ParticleEmitterDefClass::Read_Extra_Info (ChunkLoadClass &chunk_load)
{
	WW3DErrorType ret_val = WW3D_ERROR_LOAD_FAILED;

	::memset (&m_ExtraInfo, 0, sizeof (m_ExtraInfo));
	if (chunk_load.Read (&m_ExtraInfo, sizeof (m_ExtraInfo)) == sizeof (m_ExtraInfo)) {
		ret_val = WW3D_ERROR_OK;
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_W3D -- top-level saver; writes all emitter chunks
//
WW3DErrorType
ParticleEmitterDefClass::Save_W3D (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER) == TRUE) {
		if ((Save_Header (chunk_save) == WW3D_ERROR_OK)           &&
		    (Save_User_Data (chunk_save) == WW3D_ERROR_OK)         &&
		    (Save_Info (chunk_save) == WW3D_ERROR_OK)              &&
		    (Save_InfoV2 (chunk_save) == WW3D_ERROR_OK)            &&
		    (Save_Props (chunk_save) == WW3D_ERROR_OK)             &&
		    (Save_Line_Properties (chunk_save) == WW3D_ERROR_OK)   &&
		    (Save_Rotation_Keyframes (chunk_save) == WW3D_ERROR_OK) &&
		    (Save_Frame_Keyframes (chunk_save) == WW3D_ERROR_OK)   &&
		    (Save_Blur_Time_Keyframes (chunk_save) == WW3D_ERROR_OK) &&
		    (Save_Extra_Info (chunk_save) == WW3D_ERROR_OK))
		{
			ret_val = WW3D_ERROR_OK;
		}
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Header
//
WW3DErrorType
ParticleEmitterDefClass::Save_Header (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_HEADER) == TRUE) {
		W3dEmitterHeaderStruct header = { 0 };
		header.Version = W3D_CURRENT_EMITTER_VERSION;
		::lstrcpyn (header.Name, m_pName, sizeof (header.Name));
		header.Name[sizeof (header.Name) - 1] = 0;

		if (chunk_save.Write (&header, sizeof (header)) == sizeof (header)) {
			ret_val = WW3D_ERROR_OK;
		}
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_User_Data
//
WW3DErrorType
ParticleEmitterDefClass::Save_User_Data (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_USER_DATA) == TRUE) {
		DWORD string_len = m_pUserString ? (::lstrlen (m_pUserString) + 1) : 0;

		W3dEmitterUserInfoStruct user_info = { 0 };
		user_info.Type              = m_iUserType;
		user_info.SizeofStringParam = string_len;

		if (chunk_save.Write (&user_info, sizeof (user_info)) == sizeof (user_info)) {
			ret_val = WW3D_ERROR_OK;
			if (m_pUserString != NULL) {
				if (chunk_save.Write (m_pUserString, string_len) != string_len) {
					ret_val = WW3D_ERROR_SAVE_FAILED;
				}
			}
		}
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Info
//
WW3DErrorType
ParticleEmitterDefClass::Save_Info (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_INFO) == TRUE) {
		if (chunk_save.Write (&m_Info, sizeof (m_Info)) == sizeof (m_Info)) {
			ret_val = WW3D_ERROR_OK;
		}
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_InfoV2
//
WW3DErrorType
ParticleEmitterDefClass::Save_InfoV2 (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_INFOV2) == TRUE) {
		if (chunk_save.Write (&m_InfoV2, sizeof (m_InfoV2)) == sizeof (m_InfoV2)) {
			ret_val = WW3D_ERROR_OK;
		}
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Props -- writes the color/opacity/size keyframe bundle
//
WW3DErrorType
ParticleEmitterDefClass::Save_Props (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_PROPS) == TRUE) {
		W3dEmitterPropertyStruct info = { 0 };
		info.ColorKeyframes   = m_ColorKeyframes.NumKeyFrames + 1;
		info.OpacityKeyframes = m_OpacityKeyframes.NumKeyFrames + 1;
		info.SizeKeyframes    = m_SizeKeyframes.NumKeyFrames + 1;
		info.OpacityRandom    = m_OpacityKeyframes.Rand;
		info.SizeRandom       = m_SizeKeyframes.Rand;
		VECTOR3_TO_RGBA (m_ColorKeyframes.Rand, info.ColorRandom);

		if (chunk_save.Write (&info, sizeof (info)) == sizeof (info)) {
			if ((Save_Color_Keyframes (chunk_save) == WW3D_ERROR_OK) &&
			    (Save_Opacity_Keyframes (chunk_save) == WW3D_ERROR_OK) &&
			    (Save_Size_Keyframes (chunk_save) == WW3D_ERROR_OK))
			{
				ret_val = WW3D_ERROR_OK;
			}
		}
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Color_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Save_Color_Keyframes (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	W3dEmitterColorKeyframeStruct info = { 0 };
	info.Time = 0;
	VECTOR3_TO_RGBA (m_ColorKeyframes.Start, info.Color);

	if (chunk_save.Write (&info, sizeof (info)) == sizeof (info)) {
		int count = m_ColorKeyframes.NumKeyFrames;
		bool success = true;
		for (int index = 0; (index < count) && success; index++) {
			info.Time = m_ColorKeyframes.KeyTimes[index];
			VECTOR3_TO_RGBA (m_ColorKeyframes.Values[index], info.Color);
			success = (chunk_save.Write (&info, sizeof (info)) == sizeof (info));
		}
		ret_val = success ? WW3D_ERROR_OK : WW3D_ERROR_SAVE_FAILED;
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Opacity_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Save_Opacity_Keyframes (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	W3dEmitterOpacityKeyframeStruct info = { 0 };
	info.Time    = 0;
	info.Opacity = m_OpacityKeyframes.Start;

	if (chunk_save.Write (&info, sizeof (info)) == sizeof (info)) {
		int count = m_OpacityKeyframes.NumKeyFrames;
		bool success = true;
		for (int index = 0; (index < count) && success; index++) {
			info.Time    = m_OpacityKeyframes.KeyTimes[index];
			info.Opacity = m_OpacityKeyframes.Values[index];
			success = (chunk_save.Write (&info, sizeof (info)) == sizeof (info));
		}
		ret_val = success ? WW3D_ERROR_OK : WW3D_ERROR_SAVE_FAILED;
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Size_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Save_Size_Keyframes (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	W3dEmitterSizeKeyframeStruct info = { 0 };
	info.Time = 0;
	info.Size = m_SizeKeyframes.Start;

	if (chunk_save.Write (&info, sizeof (info)) == sizeof (info)) {
		int count = m_SizeKeyframes.NumKeyFrames;
		bool success = true;
		for (int index = 0; (index < count) && success; index++) {
			info.Time = m_SizeKeyframes.KeyTimes[index];
			info.Size = m_SizeKeyframes.Values[index];
			success = (chunk_save.Write (&info, sizeof (info)) == sizeof (info));
		}
		ret_val = success ? WW3D_ERROR_OK : WW3D_ERROR_SAVE_FAILED;
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Line_Properties
//
WW3DErrorType
ParticleEmitterDefClass::Save_Line_Properties (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_LINE_PROPERTIES) == TRUE) {
		if (chunk_save.Write (&m_LineProperties, sizeof (m_LineProperties)) == sizeof (m_LineProperties)) {
			ret_val = WW3D_ERROR_OK;
		}
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Rotation_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Save_Rotation_Keyframes (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES) == TRUE) {
		W3dEmitterRotationHeaderStruct header;
		header.KeyframeCount      = m_RotationKeyframes.NumKeyFrames;
		header.Random             = m_RotationKeyframes.Rand;
		header.OrientationRandom  = m_InitialOrientationRandom;
		chunk_save.Write (&header, sizeof (header));

		bool success = true;
		W3dEmitterRotationKeyframeStruct key;
		key.Time     = 0;
		key.Rotation = m_RotationKeyframes.Start;
		chunk_save.Write (&key, sizeof (key));

		for (unsigned int index = 0; (index < header.KeyframeCount) && success; index++) {
			key.Time     = m_RotationKeyframes.KeyTimes[index];
			key.Rotation = m_RotationKeyframes.Values[index];
			success = (chunk_save.Write (&key, sizeof (key)) == sizeof (key));
		}

		ret_val = success ? WW3D_ERROR_OK : WW3D_ERROR_SAVE_FAILED;
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Frame_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Save_Frame_Keyframes (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_FRAME_KEYFRAMES) == TRUE) {
		W3dEmitterFrameHeaderStruct header;
		header.KeyframeCount = m_FrameKeyframes.NumKeyFrames;
		header.Random        = m_FrameKeyframes.Rand;
		chunk_save.Write (&header, sizeof (header));

		bool success = true;
		W3dEmitterFrameKeyframeStruct key;
		key.Time  = 0;
		key.Frame = m_FrameKeyframes.Start;
		chunk_save.Write (&key, sizeof (key));

		for (unsigned int index = 0; (index < header.KeyframeCount) && success; index++) {
			key.Time  = m_FrameKeyframes.KeyTimes[index];
			key.Frame = m_FrameKeyframes.Values[index];
			success = (chunk_save.Write (&key, sizeof (key)) == sizeof (key));
		}

		ret_val = success ? WW3D_ERROR_OK : WW3D_ERROR_SAVE_FAILED;
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Blur_Time_Keyframes
//
WW3DErrorType
ParticleEmitterDefClass::Save_Blur_Time_Keyframes (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES) == TRUE) {
		W3dEmitterBlurTimeHeaderStruct header;
		header.KeyframeCount = m_BlurTimeKeyframes.NumKeyFrames;
		header.Random        = m_BlurTimeKeyframes.Rand;
		chunk_save.Write (&header, sizeof (header));

		bool success = true;
		W3dEmitterBlurTimeKeyframeStruct key;
		key.Time     = 0;
		key.BlurTime = m_BlurTimeKeyframes.Start;
		chunk_save.Write (&key, sizeof (key));

		for (unsigned int index = 0; (index < header.KeyframeCount) && success; index++) {
			key.Time     = m_BlurTimeKeyframes.KeyTimes[index];
			key.BlurTime = m_BlurTimeKeyframes.Values[index];
			success = (chunk_save.Write (&key, sizeof (key)) == sizeof (key));
		}

		ret_val = success ? WW3D_ERROR_OK : WW3D_ERROR_SAVE_FAILED;
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Save_Extra_Info
//
WW3DErrorType
ParticleEmitterDefClass::Save_Extra_Info (ChunkSaveClass &chunk_save)
{
	WW3DErrorType ret_val = WW3D_ERROR_SAVE_FAILED;

	if (chunk_save.Begin_Chunk (W3D_CHUNK_EMITTER_EXTRA_INFO) == TRUE) {
		W3dEmitterExtraInfoStruct data;
		::memset (&data, 0, sizeof (data));
		data.FutureStartTime = Get_Future_Start_Time ();
		bool success = (chunk_save.Write (&data, sizeof (data)) == sizeof (data));
		ret_val = success ? WW3D_ERROR_OK : WW3D_ERROR_SAVE_FAILED;
		chunk_save.End_Chunk ();
	}

	return ret_val;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Color_Keyframes
//
void
ParticleEmitterDefClass::Set_Color_Keyframes (ParticlePropertyStruct<Vector3> &keyframes)
{
	SAFE_DELETE_ARRAY (m_ColorKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_ColorKeyframes.Values);
	::Copy_Emitter_Property_Struct (m_ColorKeyframes, keyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Opacity_Keyframes
//
void
ParticleEmitterDefClass::Set_Opacity_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	SAFE_DELETE_ARRAY (m_OpacityKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_OpacityKeyframes.Values);
	::Copy_Emitter_Property_Struct (m_OpacityKeyframes, keyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Size_Keyframes
//
void
ParticleEmitterDefClass::Set_Size_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	SAFE_DELETE_ARRAY (m_SizeKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_SizeKeyframes.Values);
	::Copy_Emitter_Property_Struct (m_SizeKeyframes, keyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Rotation_Keyframes
//
void
ParticleEmitterDefClass::Set_Rotation_Keyframes (ParticlePropertyStruct<float> &keyframes, float orient_rnd)
{
	SAFE_DELETE_ARRAY (m_RotationKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_RotationKeyframes.Values);
	::Copy_Emitter_Property_Struct (m_RotationKeyframes, keyframes);
	m_InitialOrientationRandom = orient_rnd;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Frame_Keyframes
//
void
ParticleEmitterDefClass::Set_Frame_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	SAFE_DELETE_ARRAY (m_FrameKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_FrameKeyframes.Values);
	::Copy_Emitter_Property_Struct (m_FrameKeyframes, keyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Set_Blur_Time_Keyframes
//
void
ParticleEmitterDefClass::Set_Blur_Time_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	SAFE_DELETE_ARRAY (m_BlurTimeKeyframes.KeyTimes);
	SAFE_DELETE_ARRAY (m_BlurTimeKeyframes.Values);
	::Copy_Emitter_Property_Struct (m_BlurTimeKeyframes, keyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Color_Keyframes
//
void
ParticleEmitterDefClass::Get_Color_Keyframes (ParticlePropertyStruct<Vector3> &keyframes) const
{
	::Copy_Emitter_Property_Struct (keyframes, m_ColorKeyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Opacity_Keyframes
//
void
ParticleEmitterDefClass::Get_Opacity_Keyframes (ParticlePropertyStruct<float> &keyframes) const
{
	::Copy_Emitter_Property_Struct (keyframes, m_OpacityKeyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Size_Keyframes
//
void
ParticleEmitterDefClass::Get_Size_Keyframes (ParticlePropertyStruct<float> &keyframes) const
{
	::Copy_Emitter_Property_Struct (keyframes, m_SizeKeyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Rotation_Keyframes
//
void
ParticleEmitterDefClass::Get_Rotation_Keyframes (ParticlePropertyStruct<float> &keyframes) const
{
	::Copy_Emitter_Property_Struct (keyframes, m_RotationKeyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Frame_Keyframes
//
void
ParticleEmitterDefClass::Get_Frame_Keyframes (ParticlePropertyStruct<float> &keyframes) const
{
	::Copy_Emitter_Property_Struct (keyframes, m_FrameKeyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Blur_Time_Keyframes
//
void
ParticleEmitterDefClass::Get_Blur_Time_Keyframes (ParticlePropertyStruct<float> &blurtimeframes) const
{
	::Copy_Emitter_Property_Struct (blurtimeframes, m_BlurTimeKeyframes);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Create_Randomizer -- factory: construct the appropriate Vector3Randomizer subclass
//
Vector3Randomizer *
ParticleEmitterDefClass::Create_Randomizer (W3dVolumeRandomizerStruct &info)
{
	Vector3Randomizer *randomizer = NULL;
	switch (info.ClassID)
	{
		case Vector3Randomizer::CLASSID_SOLIDBOX:
			randomizer = W3DNEW Vector3SolidBoxRandomizer (Vector3 (info.Value1, info.Value2, info.Value3));
			break;

		case Vector3Randomizer::CLASSID_SOLIDSPHERE:
			randomizer = W3DNEW Vector3SolidSphereRandomizer (info.Value1);
			break;

		case Vector3Randomizer::CLASSID_HOLLOWSPHERE:
			randomizer = W3DNEW Vector3HollowSphereRandomizer (info.Value1);
			break;

		case Vector3Randomizer::CLASSID_SOLIDCYLINDER:
			randomizer = W3DNEW Vector3SolidCylinderRandomizer (info.Value1, info.Value2);
			break;
	}
	return randomizer;
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Initialize_Randomizer_Struct -- serialise a Vector3Randomizer back into the pod struct
//
void
ParticleEmitterDefClass::Initialize_Randomizer_Struct (const Vector3Randomizer &randomizer,
                                                        W3dVolumeRandomizerStruct &info)
{
	info.ClassID = randomizer.Class_ID ();
	switch (randomizer.Class_ID ())
	{
		case Vector3Randomizer::CLASSID_SOLIDBOX:
		{
			Vector3 extents = ((Vector3SolidBoxRandomizer &)randomizer).Get_Extents ();
			info.Value1 = extents.X;
			info.Value2 = extents.Y;
			info.Value3 = extents.Z;
		}
		break;

		case Vector3Randomizer::CLASSID_SOLIDSPHERE:
			info.Value1 = ((Vector3SolidSphereRandomizer &)randomizer).Get_Radius ();
			break;

		case Vector3Randomizer::CLASSID_HOLLOWSPHERE:
			info.Value1 = ((Vector3HollowSphereRandomizer &)randomizer).Get_Radius ();
			break;

		case Vector3Randomizer::CLASSID_SOLIDCYLINDER:
			info.Value1 = ((Vector3SolidCylinderRandomizer &)randomizer).Get_Height ();
			info.Value2 = ((Vector3SolidCylinderRandomizer &)randomizer).Get_Radius ();
			break;
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	ParticleEmitterPrototypeClass::Create
//
RenderObjClass *
ParticleEmitterPrototypeClass::Create (void)
{
	return ParticleEmitterClass::Create_From_Definition (*m_pDefinition);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	ParticleEmitterLoaderClass::Load_W3D
//
PrototypeClass *
ParticleEmitterLoaderClass::Load_W3D (ChunkLoadClass &chunk_load)
{
	ParticleEmitterPrototypeClass *pprototype = NULL;

	ParticleEmitterDefClass *pdefinition = W3DNEW ParticleEmitterDefClass;
	if (pdefinition != NULL) {
		if (pdefinition->Load_W3D (chunk_load) != WW3D_ERROR_OK) {
			delete pdefinition;
			pdefinition = NULL;
		} else {
			pprototype = W3DNEW ParticleEmitterPrototypeClass (pdefinition);
		}
	}

	return pprototype;
}
