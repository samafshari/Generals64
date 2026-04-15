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
 *                 Project Name : WW3D2                                    *
 *                                                                         *
 * Implements ParticleEmitterClass — a RenderObjClass that emits particles *
 * into a companion ParticleBufferClass. Makes no D3D8 or D3D11 calls;    *
 * all work is pure data management.                                       *
 *                                                                         *
 *-------------------------------------------------------------------------*/

#include "always.h"
#include "part_emt.h"
#include "part_buf.h"
#include "part_ldr.h"
#include "ww3d.h"
#include "scene.h"
#include "quat.h"
#include "texture.h"
#include "v3_rnd.h"
#include "w3d_file.h"
#include "w3derr.h"
#include <string.h>
#include <stdlib.h>

// ─────────────────────────────────────────────────────────────────────────────
// Static members
// ─────────────────────────────────────────────────────────────────────────────

bool ParticleEmitterClass::DefaultRemoveOnComplete = false;

#ifdef WWDEBUG
bool ParticleEmitterClass::DebugDisable = false;
#endif

// Upper bound on how large a particle buffer we will allocate.
static const unsigned int MAX_BUFFER_SIZE = 1024;
// Minimum usable buffer size.
static const unsigned int MIN_BUFFER_SIZE = 2;

// ─────────────────────────────────────────────────────────────────────────────
// Helper — compute buffer size from emission parameters
// ─────────────────────────────────────────────────────────────────────────────
static unsigned int Compute_Buffer_Size(
	float emit_rate, unsigned int burst_size,
	float max_age, int max_particles, int max_buffer_size)
{
	// We need enough slots to hold all particles alive at the same time.
	// Worst case: burst_size particles emitted every emit_rate seconds, each
	// living max_age seconds, so buffer = burst_size * emit_rate * (max_age+1).
	unsigned int computed = burst_size;
	if (emit_rate > 0.0f && max_age > 0.0f) {
		computed = static_cast<unsigned int>(burst_size * emit_rate * (max_age + 1.0f)) + 1;
	}

	if (max_particles > 0 && static_cast<unsigned int>(max_particles) < computed) {
		computed = static_cast<unsigned int>(max_particles);
	}

	unsigned int cap = (max_buffer_size > 0)
		? static_cast<unsigned int>(max_buffer_size)
		: MAX_BUFFER_SIZE;
	if (computed > cap) {
		computed = cap;
	}
	if (computed < MIN_BUFFER_SIZE) {
		computed = MIN_BUFFER_SIZE;
	}
	return computed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
ParticleEmitterClass::ParticleEmitterClass(
	float emit_rate, unsigned int burst_size,
	Vector3Randomizer *pos_rnd,
	Vector3 base_vel, Vector3Randomizer *vel_rnd,
	float out_vel, float vel_inherit_factor,
	ParticlePropertyStruct<Vector3> &color,
	ParticlePropertyStruct<float>   &opacity,
	ParticlePropertyStruct<float>   &size,
	ParticlePropertyStruct<float>   &rotation, float orient_rnd,
	ParticlePropertyStruct<float>   &frames,
	ParticlePropertyStruct<float>   &blur_times,
	Vector3 accel, float max_age, float future_start,
	TextureClass *tex, ShaderClass shader,
	int max_particles, int max_buffer_size, bool pingpong,
	int render_mode, int frame_mode,
	const W3dEmitterLinePropertiesStruct *line_props)
	: RenderObjClass()
	, EmitRate(emit_rate > 0.0f ? static_cast<unsigned int>(1000.0f / emit_rate) : 1000U)
	, BurstSize(burst_size != 0 ? burst_size : 1)
	, OneTimeBurstSize(1)
	, OneTimeBurst(false)
	, PosRand(pos_rnd)
	, BaseVel(base_vel / 1000.0f)   // convert world-units/sec → world-units/ms
	, VelRand(vel_rnd)
	, OutwardVel(out_vel / 1000.0f) // same conversion
	, VelInheritFactor(vel_inherit_factor)
	, EmitRemain(0)
	, PrevQ(true)                   // identity quaternion
	, PrevOrig(0.0f, 0.0f, 0.0f)
	, Active(false)
	, FirstTime(true)
	, BufferSceneNeeded(true)
	, ParticlesLeft(max_particles > 0 ? max_particles : -1)
	, MaxParticles(max_particles)
	, IsComplete(false)
	, NameString(nullptr)
	, UserString(nullptr)
	, RemoveOnComplete(DefaultRemoveOnComplete)
	, IsInScene(false)
	, GroupID(0)
	, Buffer(nullptr)
	, IsInvisible(false)
{
	unsigned int buf_size = Compute_Buffer_Size(
		emit_rate, BurstSize, max_age, max_particles, max_buffer_size);

	// Acceleration is passed in world-units/sec^2; buffer expects world-units/ms^2.
	Vector3 accel_ms2 = accel / 1000000.0f;

	Buffer = W3DNEW ParticleBufferClass(
		this, buf_size,
		color, opacity, size, rotation, orient_rnd,
		frames, blur_times,
		accel_ms2, max_age, future_start,
		tex, shader, pingpong, render_mode, frame_mode, line_props);
	// W3DNEW already provides refcount=1; we own this reference.
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy constructor
// ─────────────────────────────────────────────────────────────────────────────
ParticleEmitterClass::ParticleEmitterClass(const ParticleEmitterClass &src)
	: RenderObjClass(src)
	, EmitRate(src.EmitRate)
	, BurstSize(src.BurstSize)
	, OneTimeBurstSize(src.OneTimeBurstSize)
	, OneTimeBurst(src.OneTimeBurst)
	, PosRand(src.PosRand ? src.PosRand->Clone() : nullptr)
	, BaseVel(src.BaseVel)
	, VelRand(src.VelRand ? src.VelRand->Clone() : nullptr)
	, OutwardVel(src.OutwardVel)
	, VelInheritFactor(src.VelInheritFactor)
	, EmitRemain(0)
	, PrevQ(true)
	, PrevOrig(0.0f, 0.0f, 0.0f)
	, Active(false)
	, FirstTime(true)
	, BufferSceneNeeded(true)
	, ParticlesLeft(src.MaxParticles > 0 ? src.MaxParticles : -1)
	, MaxParticles(src.MaxParticles)
	, IsComplete(false)
	, NameString(nullptr)
	, UserString(nullptr)
	, RemoveOnComplete(src.RemoveOnComplete)
	, IsInScene(false)
	, GroupID(0)
	, Buffer(nullptr)
	, IsInvisible(src.IsInvisible)
{
	if (src.NameString) {
		NameString = ::strdup(src.NameString);
	}
	if (src.UserString) {
		UserString = ::strdup(src.UserString);
	}

	// Clone the buffer; the clone sets up its own internal state.
	if (src.Buffer) {
		Buffer = static_cast<ParticleBufferClass *>(src.Buffer->Clone());
		Buffer->Set_Emitter(this);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Assignment operator
// ─────────────────────────────────────────────────────────────────────────────
ParticleEmitterClass &ParticleEmitterClass::operator=(const ParticleEmitterClass &src)
{
	if (this == &src) {
		return *this;
	}

	RenderObjClass::operator=(src);

	// Free existing resources.
	if (Buffer) {
		Buffer->Emitter_Is_Dead();
		Buffer->Release_Ref();
		Buffer = nullptr;
	}
	delete PosRand; PosRand = nullptr;
	delete VelRand; VelRand = nullptr;
	::free(NameString);  NameString = nullptr;
	::free(UserString);  UserString = nullptr;

	// Copy scalars.
	EmitRate          = src.EmitRate;
	BurstSize         = src.BurstSize;
	OneTimeBurstSize  = src.OneTimeBurstSize;
	OneTimeBurst      = src.OneTimeBurst;
	BaseVel           = src.BaseVel;
	OutwardVel        = src.OutwardVel;
	VelInheritFactor  = src.VelInheritFactor;
	EmitRemain        = 0;
	PrevQ             = Quaternion(true);
	PrevOrig          = Vector3(0.0f, 0.0f, 0.0f);
	Active            = false;
	FirstTime         = true;
	BufferSceneNeeded = true;
	MaxParticles      = src.MaxParticles;
	ParticlesLeft     = (MaxParticles > 0) ? MaxParticles : -1;
	IsComplete        = false;
	RemoveOnComplete  = src.RemoveOnComplete;
	IsInScene         = false;
	GroupID           = 0;
	IsInvisible       = src.IsInvisible;

	// Deep-copy randomizers.
	PosRand = src.PosRand ? src.PosRand->Clone() : nullptr;
	VelRand = src.VelRand ? src.VelRand->Clone() : nullptr;

	// Deep-copy strings.
	if (src.NameString) {
		NameString = ::strdup(src.NameString);
	}
	if (src.UserString) {
		UserString = ::strdup(src.UserString);
	}

	// Clone the buffer.
	if (src.Buffer) {
		Buffer = static_cast<ParticleBufferClass *>(src.Buffer->Clone());
		Buffer->Set_Emitter(this);
	}

	return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
ParticleEmitterClass::~ParticleEmitterClass()
{
	if (Buffer) {
		Buffer->Emitter_Is_Dead();
		Buffer->Release_Ref();
		Buffer = nullptr;
	}
	delete PosRand;  PosRand  = nullptr;
	delete VelRand;  VelRand  = nullptr;
	::free(NameString);  NameString = nullptr;
	::free(UserString);  UserString = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Clone
// ─────────────────────────────────────────────────────────────────────────────
RenderObjClass *ParticleEmitterClass::Clone() const
{
	return W3DNEW ParticleEmitterClass(*this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Identification
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Set_Name(const char *pname)
{
	::free(NameString);
	NameString = (pname && pname[0]) ? ::strdup(pname) : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene notification
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Notify_Added(SceneClass *scene)
{
	RenderObjClass::Notify_Added(scene);
	if (scene) {
		scene->Register(this, SceneClass::ON_FRAME_UPDATE);
		IsInScene = true;
		Active    = true;
	}
}

void ParticleEmitterClass::Notify_Removed(SceneClass *scene)
{
	if (scene) {
		scene->Unregister(this, SceneClass::ON_FRAME_UPDATE);
		IsInScene = false;
		Active    = false;
	}
	RenderObjClass::Notify_Removed(scene);
}

// ─────────────────────────────────────────────────────────────────────────────
// On_Frame_Update — called once per frame when registered with the scene
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::On_Frame_Update()
{
	if (FirstTime) {
		// First frame: add the buffer to the same scene and seed PrevQ/PrevOrig.
		if (BufferSceneNeeded && Buffer) {
			SceneClass *scene = Peek_Scene();
			if (scene) {
				Buffer->Add(scene);
			}
		}

		// Initialise the previous transform snapshot from current transform.
		const Matrix3D &xfm = Get_Transform();
		PrevQ    = Build_Quaternion(xfm);
		PrevOrig = xfm.Get_Translation();

		FirstTime = false;
	}

	// Check if we have finished emitting.
	if (MaxParticles > 0 && ParticlesLeft <= 0 && !IsComplete) {
		IsComplete = true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Emit — called by ParticleBufferClass::On_Frame_Update to add new particles
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Emit()
{
#ifdef WWDEBUG
	if (DebugDisable) {
		return;
	}
#endif

	if (!Active || IsComplete || IsInvisible) {
		return;
	}

	const Matrix3D &xfm = Get_Transform();
	Quaternion currQ    = Build_Quaternion(xfm);
	Vector3    currOrig = xfm.Get_Translation();

	Create_New_Particles(currQ, currOrig);

	PrevQ    = currQ;
	PrevOrig = currOrig;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create_New_Particles — accumulate time and emit bursts
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Create_New_Particles(
	const Quaternion &curr_quat, const Vector3 &curr_orig)
{
	unsigned int frame_ms = WW3D::Get_Sync_Frame_Time();
	if (frame_ms == 0) {
		frame_ms = 1;
	}

	// Handle one-time burst first.
	if (OneTimeBurst) {
		OneTimeBurst = false;
		unsigned int burst = OneTimeBurstSize;
		for (unsigned int i = 0; i < burst; ++i) {
			if (MaxParticles > 0) {
				if (ParticlesLeft <= 0) {
					IsComplete = true;
					return;
				}
				--ParticlesLeft;
			}
			NewParticleStruct *slot = Buffer->Add_Uninitialized_New_Particle();
			// age = 0: particles born at exactly the current time.
			Initialize_Particle(slot, 0, curr_quat, curr_orig);
		}
	}

	// Normal rate-based emission.
	EmitRemain += frame_ms;

	// Pre-compute slerp info for this interval (reused each sub-step).
	SlerpInfoStruct slerp_info;
	Slerp_Setup(PrevQ, curr_quat, &slerp_info);

	while (EmitRemain >= EmitRate) {
		EmitRemain -= EmitRate;

		// t = how far through the frame interval this emit point falls [0,1].
		float t = 1.0f;
		if (frame_ms > 0) {
			t = 1.0f - (static_cast<float>(EmitRemain) / static_cast<float>(frame_ms));
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
		}

		// Interpolate orientation and position.
		Quaternion interp_q = Cached_Slerp(PrevQ, curr_quat, t, &slerp_info);
		Vector3    interp_o = PrevOrig + t * (curr_orig - PrevOrig);

		// Compute the age (in ms) the particles will have at the next buffer update.
		// They were born at sync_time - EmitRemain.
		unsigned int age = EmitRemain;

		for (unsigned int i = 0; i < BurstSize; ++i) {
			if (MaxParticles > 0) {
				if (ParticlesLeft <= 0) {
					IsComplete = true;
					return;
				}
				--ParticlesLeft;
			}
			NewParticleStruct *slot = Buffer->Add_Uninitialized_New_Particle();
			Initialize_Particle(slot, age, interp_q, interp_o);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize_Particle — fill in one NewParticleStruct
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Initialize_Particle(
	NewParticleStruct *newpart, unsigned int age,
	const Quaternion &quat, const Vector3 &orig)
{
	if (!newpart) {
		return;
	}

	// Position: randomized in local space, then rotated to world space.
	Vector3 local_pos(0.0f, 0.0f, 0.0f);
	if (PosRand) {
		PosRand->Get_Vector(local_pos);
	}
	newpart->Position = orig + quat.Rotate_Vector(local_pos);

	// Base velocity (already in world-units/ms from constructor conversion).
	Vector3 vel = quat.Rotate_Vector(BaseVel);

	// Velocity randomizer contribution (in local space, convert to world).
	if (VelRand) {
		Vector3 vel_rnd(0.0f, 0.0f, 0.0f);
		VelRand->Get_Vector(vel_rnd);
		// VelRand stores values in world-units/sec; convert to ms.
		vel += quat.Rotate_Vector(vel_rnd / 1000.0f);
	}

	// Outward velocity: push radially away from emitter origin.
	if (OutwardVel != 0.0f) {
		Vector3 outward = newpart->Position - orig;
		float len = outward.Length();
		if (len > 1e-6f) {
			vel += outward * (OutwardVel / len);
		} else {
			// Fall back to emitter's local Z axis.
			vel += quat.Rotate_Vector(Vector3(0.0f, 0.0f, OutwardVel));
		}
	}

	// Velocity inheritance from the emitter's own motion. We approximate the
	// emitter velocity by comparing previous and current origin over the frame.
	if (VelInheritFactor != 0.0f && Buffer) {
		unsigned int frame_ms = WW3D::Get_Sync_Frame_Time();
		if (frame_ms > 0) {
			const Matrix3D &xfm = Get_Transform();
			Vector3 curr_o = xfm.Get_Translation();
			Vector3 emitter_vel = (curr_o - PrevOrig) / static_cast<float>(frame_ms);
			vel += emitter_vel * VelInheritFactor;
		}
	}

	newpart->Velocity  = vel;
	// TimeStamp is the absolute ms time at which the particle was born.
	newpart->TimeStamp = WW3D::Get_Sync_Time() - age;
	newpart->GroupID   = GroupID;
}

// ─────────────────────────────────────────────────────────────────────────────
// Restart — put the emitter back to a freshly-started state
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Restart()
{
	Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Start / Stop / Reset / Is_Stopped
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Start()
{
	if (Active) {
		return;
	}

	Active     = true;
	IsComplete = false;
	ParticlesLeft = (MaxParticles > 0) ? MaxParticles : -1;
	EmitRemain = 0;
	++GroupID;

	if (Buffer) {
		Buffer->Set_Current_GroupID(GroupID);
	}

	// Seed previous transform so interpolation starts correctly.
	const Matrix3D &xfm = Get_Transform();
	PrevQ    = Build_Quaternion(xfm);
	PrevOrig = xfm.Get_Translation();
}

void ParticleEmitterClass::Stop()
{
	Active = false;
}

bool ParticleEmitterClass::Is_Stopped()
{
	return !Active;
}

void ParticleEmitterClass::Reset()
{
	Active     = false;
	IsComplete = false;
	FirstTime  = true;
	EmitRemain = 0;
	ParticlesLeft = (MaxParticles > 0) ? MaxParticles : -1;
	++GroupID;

	if (Buffer) {
		Buffer->Set_Current_GroupID(GroupID);
	}

	// Seed previous transform.
	const Matrix3D &xfm = Get_Transform();
	PrevQ    = Build_Quaternion(xfm);
	PrevOrig = xfm.Get_Translation();

	Active = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update_On_Visibility — start/stop based on render-visibility flags
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Update_On_Visibility()
{
	if (Is_Really_Visible()) {
		if (!Active) {
			Start();
		}
	} else {
		Stop();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Scale
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Scale(float scale)
{
	if (PosRand) {
		PosRand->Scale(scale);
	}
	// Base velocity and outward velocity both scale linearly with size.
	BaseVel    *= scale;
	OutwardVel *= scale;

	// Delegate visual scaling to the buffer.
	if (Buffer) {
		Buffer->Scale(scale);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounding volumes — the emitter itself is a point; the buffer owns the volume
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Update_Cached_Bounding_Volumes() const
{
	CachedBoundingSphere.Center.Set(0.0f, 0.0f, 0.0f);
	CachedBoundingSphere.Radius = 0.0f;
	CachedBoundingBox.Center.Set(0.0f, 0.0f, 0.0f);
	CachedBoundingBox.Extent.Set(0.0f, 0.0f, 0.0f);
	Validate_Cached_Bounding_Volumes();
}

// ─────────────────────────────────────────────────────────────────────────────
// Set_Position_Randomizer / Set_Velocity_Randomizer
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Set_Position_Randomizer(Vector3Randomizer *rand)
{
	delete PosRand;
	PosRand = rand;
}

void ParticleEmitterClass::Set_Velocity_Randomizer(Vector3Randomizer *rand)
{
	delete VelRand;
	VelRand = rand;
}

void ParticleEmitterClass::Set_Base_Velocity(const Vector3 &base_vel)
{
	// Store in ms units.
	BaseVel = base_vel / 1000.0f;
}

void ParticleEmitterClass::Set_Outwards_Velocity(float out_vel)
{
	OutwardVel = out_vel / 1000.0f;
}

void ParticleEmitterClass::Set_Velocity_Inheritance_Factor(float inh_factor)
{
	VelInheritFactor = inh_factor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Get_Creation_Volume / Get_Velocity_Random — return clones of the randomizers
// ─────────────────────────────────────────────────────────────────────────────
Vector3Randomizer *ParticleEmitterClass::Get_Creation_Volume() const
{
	return PosRand ? PosRand->Clone() : nullptr;
}

Vector3Randomizer *ParticleEmitterClass::Get_Velocity_Random() const
{
	return VelRand ? VelRand->Clone() : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Add_Dependencies_To_List
// ─────────────────────────────────────────────────────────────────────────────
void ParticleEmitterClass::Add_Dependencies_To_List(
	DynamicVectorClass<StringClass> &file_list, bool textures_only)
{
	if (Buffer) {
		TextureClass *tex = Buffer->Get_Texture();
		if (tex) {
			file_list.Add(tex->Get_Texture_Name().str());
			tex->Release_Ref();
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Create_From_Definition — static factory
// ─────────────────────────────────────────────────────────────────────────────
ParticleEmitterClass *ParticleEmitterClass::Create_From_Definition(
	const ParticleEmitterDefClass &def)
{
	// Retrieve all the keyframe property structs from the definition.
	ParticlePropertyStruct<Vector3> color_props;
	ParticlePropertyStruct<float>   opacity_props;
	ParticlePropertyStruct<float>   size_props;
	ParticlePropertyStruct<float>   rotation_props;
	ParticlePropertyStruct<float>   frame_props;
	ParticlePropertyStruct<float>   blur_props;

	def.Get_Color_Keyframes(color_props);
	def.Get_Opacity_Keyframes(opacity_props);
	def.Get_Size_Keyframes(size_props);
	def.Get_Rotation_Keyframes(rotation_props);
	def.Get_Frame_Keyframes(frame_props);
	def.Get_Blur_Time_Keyframes(blur_props);

	float orient_rnd = def.Get_Initial_Orientation_Random();

	// Randomizers — callers of Get_*() receive *clones* which we own.
	Vector3Randomizer *pos_rnd = def.Get_Creation_Volume();
	Vector3Randomizer *vel_rnd = def.Get_Velocity_Random();

	// Shader
	ShaderClass shader;
	def.Get_Shader(shader);

	// Construct emitter.
	ParticleEmitterClass *emitter = W3DNEW ParticleEmitterClass(
		def.Get_Emission_Rate(),
		def.Get_Burst_Size(),
		pos_rnd,
		def.Get_Velocity(),
		vel_rnd,
		def.Get_Outward_Vel(),
		def.Get_Vel_Inherit(),
		color_props,
		opacity_props,
		size_props,
		rotation_props, orient_rnd,
		frame_props,
		blur_props,
		def.Get_Acceleration(),
		def.Get_Lifetime(),
		def.Get_Future_Start_Time(),
		nullptr,               // texture is resolved by caller from filename
		shader,
		static_cast<int>(def.Get_Max_Emissions()),
		-1,                    // max_buffer_size: use computed default
		false,                 // pingpong
		def.Get_Render_Mode(),
		def.Get_Frame_Mode(),
		def.Get_Line_Properties());

	if (emitter) {
		emitter->Set_Name(def.Get_Name());
	}

	// Free keyframe arrays allocated by Get_*_Keyframes.
	delete[] color_props.KeyTimes;   delete[] color_props.Values;
	delete[] opacity_props.KeyTimes; delete[] opacity_props.Values;
	delete[] size_props.KeyTimes;    delete[] size_props.Values;
	delete[] rotation_props.KeyTimes; delete[] rotation_props.Values;
	delete[] frame_props.KeyTimes;   delete[] frame_props.Values;
	delete[] blur_props.KeyTimes;    delete[] blur_props.Values;

	return emitter;
}

// ─────────────────────────────────────────────────────────────────────────────
// Build_Definition — creates a ParticleEmitterDefClass from our current state
// ─────────────────────────────────────────────────────────────────────────────
ParticleEmitterDefClass *ParticleEmitterClass::Build_Definition() const
{
	if (!Buffer) {
		return nullptr;
	}

	ParticleEmitterDefClass *def = W3DNEW ParticleEmitterDefClass();
	if (!def) {
		return nullptr;
	}

	def->Set_Name(NameString ? NameString : "");

	// Emission parameters.
	def->Set_Emission_Rate(Get_Emission_Rate());
	def->Set_Burst_Size(BurstSize);
	def->Set_Lifetime(Get_Lifetime());
	def->Set_Max_Emissions(static_cast<float>(MaxParticles > 0 ? MaxParticles : 0));
	def->Set_Future_Start_Time(Get_Future_Start_Time());

	// Velocity / acceleration (convert back from ms to seconds).
	def->Set_Velocity(Get_Start_Velocity());
	def->Set_Acceleration(Get_Acceleration());
	def->Set_Outward_Vel(Get_Outwards_Vel());
	def->Set_Vel_Inherit(VelInheritFactor);

	// Randomizers — pass clones; Set_*() takes ownership of the pointer.
	if (PosRand) {
		def->Set_Creation_Volume(PosRand->Clone());
	}
	if (VelRand) {
		def->Set_Velocity_Random(VelRand->Clone());
	}

	// Shader.
	def->Set_Shader(Get_Shader());

	// Visual properties from the buffer.
	{
		ParticlePropertyStruct<Vector3> c;
		Buffer->Get_Color_Key_Frames(c);
		def->Set_Color_Keyframes(c);
		delete[] c.KeyTimes; delete[] c.Values;
	}
	{
		ParticlePropertyStruct<float> o;
		Buffer->Get_Opacity_Key_Frames(o);
		def->Set_Opacity_Keyframes(o);
		delete[] o.KeyTimes; delete[] o.Values;
	}
	{
		ParticlePropertyStruct<float> s;
		Buffer->Get_Size_Key_Frames(s);
		def->Set_Size_Keyframes(s);
		delete[] s.KeyTimes; delete[] s.Values;
	}
	{
		ParticlePropertyStruct<float> r;
		Buffer->Get_Rotation_Key_Frames(r);
		def->Set_Rotation_Keyframes(r, Get_Initial_Orientation_Random());
		delete[] r.KeyTimes; delete[] r.Values;
	}
	{
		ParticlePropertyStruct<float> f;
		Buffer->Get_Frame_Key_Frames(f);
		def->Set_Frame_Keyframes(f);
		delete[] f.KeyTimes; delete[] f.Values;
	}
	{
		ParticlePropertyStruct<float> b;
		Buffer->Get_Blur_Time_Key_Frames(b);
		def->Set_Blur_Time_Keyframes(b);
		delete[] b.KeyTimes; delete[] b.Values;
	}

	// Render / frame mode.
	def->Set_Render_Mode(Get_Render_Mode());
	def->Set_Frame_Mode(Get_Frame_Mode());

	// Texture filename.
	TextureClass *tex = Buffer->Get_Texture();
	if (tex) {
		def->Set_Texture_Filename(tex->Get_Texture_Name().str());
		tex->Release_Ref();
	}

	// User data.
	def->Set_User_Type(Get_User_Type());
	const char *user_str = Get_User_String();
	if (user_str) {
		def->Set_User_String(user_str);
	}

	return def;
}

// ─────────────────────────────────────────────────────────────────────────────
// Save — serialise via the definition
// ─────────────────────────────────────────────────────────────────────────────
WW3DErrorType ParticleEmitterClass::Save(ChunkSaveClass &chunk_save) const
{
	ParticleEmitterDefClass *def = Build_Definition();
	if (!def) {
		return WW3D_ERROR_GENERIC;
	}
	WW3DErrorType result = def->Save_W3D(chunk_save);
	delete def;
	return result;
}
