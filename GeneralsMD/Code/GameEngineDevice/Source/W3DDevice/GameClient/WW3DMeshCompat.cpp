/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "WW3D2/mesh.h"

#include "WW3D2/decalmsh.h"
#include "WW3D2/decalsys.h"
#include "WW3D2/matpass.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/projector.h"
#include "WW3D2/coltest.h"
#include "WW3D2/htree.h"
#include "WW3D2/inttest.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/shader.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/texture.h"
#include "WW3D2/w3d_file.h"
#include "chunkio.h"
#include "W3DDevice/GameClient/ModelRenderer.h"
#include "WWMath/matrix3d.h"
#include "WWMath/matrix4.h"

namespace
{
	unsigned g_meshDebugIdCount = 1;
}

bool MeshClass::Legacy_Meshes_Fogged = true;

MeshModelClass::MeshModelClass() :
	DefMatDesc(nullptr),
	AlternateMatDesc(nullptr),
	CurMatDesc(nullptr),
	MatInfo(nullptr),
	GapFiller(nullptr),
	HasBeenInUse(false)
{
	Set_Flag(DIRTY_BOUNDS, true);

	DefMatDesc = W3DNEW MeshMatDescClass;
	CurMatDesc = DefMatDesc;
	MatInfo = NEW_REF(MaterialInfoClass, ());
}

MeshModelClass::MeshModelClass(const MeshModelClass &that) :
	MeshGeometryClass(that),
	DefMatDesc(nullptr),
	AlternateMatDesc(nullptr),
	CurMatDesc(nullptr),
	MatInfo(nullptr),
	GapFiller(nullptr),
	HasBeenInUse(false)
{
	DefMatDesc = W3DNEW MeshMatDescClass(*(that.DefMatDesc));
	if (that.AlternateMatDesc != nullptr)
		AlternateMatDesc = W3DNEW MeshMatDescClass(*(that.AlternateMatDesc));

	CurMatDesc = DefMatDesc;
	clone_materials(that);
}

MeshModelClass::~MeshModelClass()
{
	Reset_Geometry(0, 0);
	REF_PTR_RELEASE(MatInfo);
	delete DefMatDesc;
	delete AlternateMatDesc;
}

MeshModelClass &MeshModelClass::operator=(const MeshModelClass &that)
{
	if (this != &that)
	{
		MeshGeometryClass::operator=(that);
		*DefMatDesc = *(that.DefMatDesc);
		CurMatDesc = DefMatDesc;

		delete AlternateMatDesc;
		AlternateMatDesc = nullptr;
		if (that.AlternateMatDesc != nullptr)
			AlternateMatDesc = W3DNEW MeshMatDescClass(*(that.AlternateMatDesc));

		clone_materials(that);
		GapFiller = nullptr;
		HasBeenInUse = false;
	}

	return *this;
}

void MeshModelClass::Reset(int polycount, int vertcount, int passcount)
{
	Reset_Geometry(polycount, vertcount);
	MatInfo->Reset();
	DefMatDesc->Reset(polycount, vertcount, passcount);
	delete AlternateMatDesc;
	AlternateMatDesc = nullptr;
	CurMatDesc = DefMatDesc;
	GapFiller = nullptr;
	HasBeenInUse = false;
}

void MeshModelClass::Register_For_Rendering()
{
	HasBeenInUse = true;
}

void MeshModelClass::Replace_Texture(TextureClass *texture, TextureClass *new_texture)
{
	WWASSERT(texture != nullptr);
	WWASSERT(new_texture != nullptr);

	for (int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; ++stage)
	{
		for (int pass = 0; pass < Get_Pass_Count(); ++pass)
		{
			if (Has_Texture_Array(pass, stage))
			{
				for (int i = 0; i < Get_Polygon_Count(); ++i)
				{
					if (Peek_Texture(i, pass, stage) == texture)
						Set_Texture(i, new_texture, pass, stage);
				}
			}
			else if (Peek_Single_Texture(pass, stage) == texture)
			{
				Set_Single_Texture(new_texture, pass, stage);
			}
		}
	}
}

void MeshModelClass::Replace_VertexMaterial(VertexMaterialClass *vmat, VertexMaterialClass *new_vmat)
{
	WWASSERT(vmat != nullptr);
	WWASSERT(new_vmat != nullptr);

	for (int pass = 0; pass < Get_Pass_Count(); ++pass)
	{
		if (Has_Material_Array(pass))
		{
			for (int i = 0; i < Get_Vertex_Count(); ++i)
			{
				if (Peek_Material(i, pass) == vmat)
					Set_Material(i, new_vmat, pass);
			}
		}
		else if (Peek_Single_Material(pass) == vmat)
		{
			Set_Single_Material(new_vmat, pass);
		}
	}
}

DX8FVFCategoryContainer *MeshModelClass::Peek_FVF_Category_Container()
{
	return nullptr;
}

void MeshModelClass::Make_Geometry_Unique()
{
	WWASSERT(Vertex != nullptr);
	if (Vertex == nullptr || VertexNorm == nullptr)
		return;

	ShareBufferClass<Vector3> *uniqueVerts = NEW_REF(ShareBufferClass<Vector3>, (*Vertex));
	REF_PTR_SET(Vertex, uniqueVerts);
	REF_PTR_RELEASE(uniqueVerts);

	ShareBufferClass<Vector3> *norms = NEW_REF(ShareBufferClass<Vector3>, (*VertexNorm));
	REF_PTR_SET(VertexNorm, norms);
	REF_PTR_RELEASE(norms);

#if (!OPTIMIZE_PLANEEQ_RAM)
	if (PlaneEq != nullptr)
	{
		ShareBufferClass<Vector4> *peq = NEW_REF(ShareBufferClass<Vector4>, (*PlaneEq, "MeshModelClass::PlaneEq"));
		REF_PTR_SET(PlaneEq, peq);
		REF_PTR_RELEASE(peq);
	}
#endif
}

MeshClass::MeshClass() :
	Model(nullptr),
	DecalMesh(nullptr),
	LightEnvironment(nullptr),
	BaseVertexOffset(0),
	NextVisibleSkin(nullptr),
	MeshDebugId(g_meshDebugIdCount++),
	IsDisabledByDebugger(false),
	m_alphaOverride(1.0f),
	m_materialPassEmissiveOverride(1.0f),
	m_materialPassAlphaOverride(1.0f)
{
}

MeshClass::MeshClass(const MeshClass &that) :
	RenderObjClass(that),
	Model(nullptr),
	DecalMesh(nullptr),
	LightEnvironment(nullptr),
	BaseVertexOffset(that.BaseVertexOffset),
	NextVisibleSkin(nullptr),
	MeshDebugId(g_meshDebugIdCount++),
	IsDisabledByDebugger(false),
	m_alphaOverride(1.0f),
	m_materialPassEmissiveOverride(1.0f),
	m_materialPassAlphaOverride(1.0f)
{
	REF_PTR_SET(Model, that.Model);
}

MeshClass &MeshClass::operator=(const MeshClass &that)
{
	if (this != &that)
	{
		RenderObjClass::operator=(that);
		REF_PTR_SET(Model, that.Model);
		BaseVertexOffset = that.BaseVertexOffset;
		REF_PTR_RELEASE(DecalMesh);
		LightEnvironment = nullptr;
	}

	return *this;
}

MeshClass::~MeshClass()
{
	Free();
}

void MeshClass::Free()
{
	REF_PTR_RELEASE(Model);
	REF_PTR_RELEASE(DecalMesh);
}

RenderObjClass *MeshClass::Clone() const
{
	return NEW_REF(MeshClass, (*this));
}

const char *MeshClass::Get_Name() const
{
	return (Model != nullptr) ? Model->Get_Name() : "";
}

void MeshClass::Set_Name(const char *name)
{
	if (Model != nullptr)
		Model->Set_Name(name);
}

int MeshClass::Get_Num_Polys() const
{
	if (Model == nullptr)
		return 0;

	return Model->Get_Pass_Count() * Model->Get_Polygon_Count();
}

void MeshClass::Render(RenderInfoClass &)
{
}

void MeshClass::Render_Material_Pass(MaterialPassClass *, IndexBufferClass *)
{
}

void MeshClass::Special_Render(SpecialRenderInfoClass &)
{
}

bool MeshClass::Contains(const Vector3 &point)
{
	if (Model == nullptr)
		return false;

	Vector3 obj_point;
	Matrix3D::Inverse_Transform_Vector(Transform, point, &obj_point);
	return Model->Contains(obj_point);
}

MaterialInfoClass *MeshClass::Get_Material_Info()
{
	if (Model != nullptr && Model->MatInfo != nullptr)
	{
		Model->MatInfo->Add_Ref();
		return Model->MatInfo;
	}

	return nullptr;
}

MeshModelClass *MeshClass::Get_Model()
{
	if (Model != nullptr)
		Model->Add_Ref();

	return Model;
}

uint32 MeshClass::Get_W3D_Flags()
{
	return (Model != nullptr) ? Model->W3dAttributes : 0;
}

const char *MeshClass::Get_User_Text() const
{
	return (Model != nullptr) ? Model->Get_User_Text() : nullptr;
}

void MeshClass::Scale(float scale)
{
	if (Model == nullptr || scale == 1.0f)
		return;

	Vector3 sc(scale, scale, scale);
	Make_Unique();
	Model->Make_Geometry_Unique();
	Model->Scale(sc);
	Invalidate_Cached_Bounding_Volumes();

	RenderObjClass *container = Get_Container();
	if (container != nullptr)
		container->Update_Obj_Space_Bounding_Volumes();
}

void MeshClass::Scale(float scalex, float scaley, float scalez)
{
	if (Model == nullptr)
		return;

	Vector3 sc(scalex, scaley, scalez);
	Make_Unique();
	Model->Make_Geometry_Unique();
	Model->Scale(sc);
	Invalidate_Cached_Bounding_Volumes();

	RenderObjClass *container = Get_Container();
	if (container != nullptr)
		container->Update_Obj_Space_Bounding_Volumes();
}

void MeshClass::Get_Deformed_Vertices(Vector3 *dst_vert, Vector3 *dst_norm)
{
	WWASSERT(Model != nullptr && Model->Get_Flag(MeshGeometryClass::SKIN));
	if (Model != nullptr && Container != nullptr)
		Model->get_deformed_vertices(dst_vert, dst_norm, Container->Get_HTree());
}

void MeshClass::Get_Deformed_Vertices(Vector3 *dst_vert)
{
	WWASSERT(Model != nullptr && Model->Get_Flag(MeshGeometryClass::SKIN));
	if (Model != nullptr && Container != nullptr)
		Model->get_deformed_vertices(dst_vert, Container->Get_HTree());
}

// --- ProjectorClass stub implementations (original was in precompiled WW3D2 lib) ---
ProjectorClass::ProjectorClass() : Mapper(nullptr) {}
ProjectorClass::~ProjectorClass() {}
void ProjectorClass::Set_Transform(const Matrix3D& tm) { Transform = tm; Update_WS_Bounding_Volume(); }
const Matrix3D& ProjectorClass::Get_Transform() const { return Transform; }
void ProjectorClass::Set_Perspective_Projection(float, float, float, float) {}
void ProjectorClass::Set_Ortho_Projection(float xmin, float xmax, float ymin, float ymax, float znear, float zfar)
{
    // Build orthographic projection matrix that maps [xmin,xmax]x[ymin,ymax]x[znear,zfar] -> [0,1]^3
    float dx = xmax - xmin, dy = ymax - ymin, dz = zfar - znear;
    if (dx == 0) dx = 1; if (dy == 0) dy = 1; if (dz == 0) dz = 1;
    Projection.Make_Identity();
    Projection[0][0] = 1.0f / dx;  Projection[0][3] = -xmin / dx;
    Projection[1][1] = 1.0f / dy;  Projection[1][3] = -ymin / dy;
    Projection[2][2] = 1.0f / dz;  Projection[2][3] = -znear / dz;
    Update_WS_Bounding_Volume();
}
void ProjectorClass::Update_WS_Bounding_Volume() {}
void ProjectorClass::Compute_Texture_Coordinate(const Vector3& point, Vector3* set_stq)
{
    // Transform world point into projector local space, then apply projection
    Vector3 local;
    Matrix3D inv;
    Transform.Get_Inverse(inv);
    Matrix3D::Transform_Vector(inv, point, &local);

    // Multiply by 4x4 Projection: result = Projection * (local, 1)
    float x = Projection[0][0]*local.X + Projection[0][1]*local.Y + Projection[0][2]*local.Z + Projection[0][3];
    float y = Projection[1][0]*local.X + Projection[1][1]*local.Y + Projection[1][2]*local.Z + Projection[1][3];
    float w = Projection[3][0]*local.X + Projection[3][1]*local.Y + Projection[3][2]*local.Z + Projection[3][3];
    if (w == 0.0f) w = 1.0f;
    set_stq->X = x; // S
    set_stq->Y = y; // T
    set_stq->Z = w; // Q
}

void MeshClass::Create_Decal(DecalGeneratorClass *generator)
{
    if (!generator || !Model)
        return;

    // Build world-to-decal-UV projection matrix from the projector.
    // ProjectorClass::Compute_Texture_Coordinate does:
    //   invTransform * point -> local, then Projection * local -> STQ, then S/Q, T/Q are UVs.
    // We bake this into a single 4x4 matrix for the GPU shader.

    // Build the world-to-decal-UV projection matrix by sampling the projector
    // at known world points. For ortho projections (faction logos), the mapping
    // is affine so 4 samples fully determine the 4x4 matrix.
    const Matrix3D& tm = generator->Get_Transform();
    Vector3 origin = tm.Get_Translation();
    Vector3 axisX(tm[0][0], tm[1][0], tm[2][0]);
    Vector3 axisY(tm[0][1], tm[1][1], tm[2][1]);
    Vector3 axisZ(tm[0][2], tm[1][2], tm[2][2]);

    Vector3 stq0, stq1, stq2, stq3;
    Vector3 p1 = origin + axisX;
    Vector3 p2 = origin + axisY;
    Vector3 p3 = origin + axisZ;
    generator->Compute_Texture_Coordinate(origin, &stq0);
    generator->Compute_Texture_Coordinate(p1, &stq1);
    generator->Compute_Texture_Coordinate(p2, &stq2);
    generator->Compute_Texture_Coordinate(p3, &stq3);

    // Homogeneous divide for each sample
    auto divW = [](const Vector3& stq) -> Vector3 {
        float q = (stq.Z != 0.0f) ? stq.Z : 1.0f;
        return Vector3(stq.X / q, stq.Y / q, 0.5f);
    };
    Vector3 uv0 = divW(stq0), uv1 = divW(stq1), uv2 = divW(stq2), uv3 = divW(stq3);

    // Compute per-axis gradients in projector space
    float duX = uv1.X - uv0.X, duY = uv2.X - uv0.X, duZ = uv3.X - uv0.X;
    float dvX = uv1.Y - uv0.Y, dvY = uv2.Y - uv0.Y, dvZ = uv3.Y - uv0.Y;

    // Transform gradients from projector-local axes to world space
    // Row i of the matrix: dot(worldPos, grad_i) + offset_i
    Render::Float4x4 projMat;
    projMat._11 = duX * axisX.X + duY * axisY.X + duZ * axisZ.X;
    projMat._12 = duX * axisX.Y + duY * axisY.Y + duZ * axisZ.Y;
    projMat._13 = duX * axisX.Z + duY * axisY.Z + duZ * axisZ.Z;
    projMat._14 = uv0.X - (projMat._11 * origin.X + projMat._12 * origin.Y + projMat._13 * origin.Z);

    projMat._21 = dvX * axisX.X + dvY * axisY.X + dvZ * axisZ.X;
    projMat._22 = dvX * axisX.Y + dvY * axisY.Y + dvZ * axisZ.Y;
    projMat._23 = dvX * axisX.Z + dvY * axisY.Z + dvZ * axisZ.Z;
    projMat._24 = uv0.Y - (projMat._21 * origin.X + projMat._22 * origin.Y + projMat._23 * origin.Z);

    // Depth row for [0,1] clipping along projection axis
    float depthScale = 1.0f / std::max(axisZ.Length(), 0.001f);
    projMat._31 = axisZ.X * depthScale;
    projMat._32 = axisZ.Y * depthScale;
    projMat._33 = axisZ.Z * depthScale;
    projMat._34 = -(projMat._31 * origin.X + projMat._32 * origin.Y + projMat._33 * origin.Z) + 0.5f;

    projMat._41 = 0; projMat._42 = 0; projMat._43 = 0; projMat._44 = 1.0f;

    // Pull texture + opacity from the material pass. The DX8 pipeline used the
    // VertexMaterial's diffuse to drive faction tint via a multi-texture stage,
    // but in this codebase that field is fixed-function scaffolding (typically
    // zero). Player color is resolved at draw time from the owning render
    // object's house color instead — see ModelRenderer::RenderMesh decal loop.
    TextureClass* decalTex = nullptr;
    float decalOpacity = 1.0f;
    MaterialPassClass* material = generator->Get_Material();
    if (material)
    {
        decalTex = material->Peek_Texture(0);
        VertexMaterialClass* vtxMat = material->Peek_Material();
        if (vtxMat)
            decalOpacity = vtxMat->Get_Opacity();
        material->Release_Ref();
    }

    Render::ModelRenderer::Instance().AddMeshDecal(
        Model, generator->Get_Decal_ID(), projMat, decalTex,
        generator->Get_Backface_Threshhold(), decalOpacity);
}

void MeshClass::Delete_Decal(uint32 decal_id)
{
    if (Model)
        Render::ModelRenderer::Instance().RemoveMeshDecal(Model, decal_id);
}

void MeshClass::Replace_Texture(TextureClass *texture, TextureClass *new_texture)
{
	if (Model != nullptr)
		Model->Replace_Texture(texture, new_texture);
}

void MeshClass::Replace_VertexMaterial(VertexMaterialClass *vmat, VertexMaterialClass *new_vmat)
{
	if (Model != nullptr)
		Model->Replace_VertexMaterial(vmat, new_vmat);
}

void MeshClass::Make_Unique(bool force_meshmdl_clone)
{
	if (Model == nullptr || (Model->Num_Refs() == 1 && !force_meshmdl_clone))
		return;

	MeshModelClass *newmesh = NEW_REF(MeshModelClass, (*Model));
	REF_PTR_SET(Model, newmesh);
	REF_PTR_RELEASE(newmesh);
}

WW3DErrorType MeshClass::Load_W3D(ChunkLoadClass &cload)
{
	Free();

	Model = NEW_REF(MeshModelClass, ());
	if (Model == nullptr)
		return WW3D_ERROR_LOAD_FAILED;

	if (Model->Load_W3D(cload) != WW3D_ERROR_OK)
	{
		Free();
		return WW3D_ERROR_LOAD_FAILED;
	}

	const int col_bits = (Model->W3dAttributes & W3D_MESH_FLAG_COLLISION_TYPE_MASK) >> W3D_MESH_FLAG_COLLISION_TYPE_SHIFT;
	Set_Collision_Type(col_bits << 1);
	Set_Hidden(Model->W3dAttributes & W3D_MESH_FLAG_HIDDEN);

	int is_translucent = Model->Get_Flag(MeshModelClass::SORT);
	int is_alpha = 0;
	int is_additive = 0;

	if (Model->Has_Shader_Array(0))
	{
		for (int i = 0; i < Model->Get_Polygon_Count(); ++i)
		{
			ShaderClass shader = Model->Get_Shader(i, 0);
			is_translucent |= (shader.Get_Alpha_Test() == ShaderClass::ALPHATEST_ENABLE);
			is_alpha |= (shader.Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ZERO ||
				shader.Get_Src_Blend_Func() != ShaderClass::SRCBLEND_ONE) &&
				(shader.Get_Alpha_Test() != ShaderClass::ALPHATEST_ENABLE);
			is_additive |= (shader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ONE &&
				shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE);
		}
	}
	else
	{
		ShaderClass shader = Model->Get_Single_Shader(0);
		is_translucent |= (shader.Get_Alpha_Test() == ShaderClass::ALPHATEST_ENABLE);
		is_alpha |= (shader.Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ZERO ||
			shader.Get_Src_Blend_Func() != ShaderClass::SRCBLEND_ONE) &&
			(shader.Get_Alpha_Test() != ShaderClass::ALPHATEST_ENABLE);
		is_additive |= (shader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ONE &&
			shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE);
	}

	Set_Translucent(is_translucent);
	Set_Alpha(is_alpha);
	Set_Additive(is_additive);

	return WW3D_ERROR_OK;
}

bool MeshClass::Cast_Ray(RayCollisionTestClass &raytest)
{
	if (Model == nullptr || (Get_Collision_Type() & raytest.CollisionType) == 0)
		return false;
	if (raytest.CheckTranslucent && Is_Alpha() != 0)
		return false;
	if (Is_Hidden() && !raytest.CheckHidden)
		return false;
	if (Is_Animation_Hidden() || raytest.Result->StartBad)
		return false;

	Matrix3D world = Get_Transform();
	if (Model->Get_Flag(MeshModelClass::ALIGNED))
	{
		Vector3 mesh_position;
		world.Get_Translation(&mesh_position);
		world.Obj_Look_At(mesh_position, mesh_position - raytest.Ray.Get_Dir(), 0.0f);
	}
	else if (Model->Get_Flag(MeshModelClass::ORIENTED))
	{
		Vector3 mesh_position;
		world.Get_Translation(&mesh_position);
		world.Obj_Look_At(mesh_position, raytest.Ray.Get_P0(), 0.0f);
	}

	Matrix3D world_to_obj;
	world.Get_Inverse(world_to_obj);
	RayCollisionTestClass objray(raytest, world_to_obj);
	const bool hit = Model->Cast_Ray(objray);
	if (hit)
	{
		raytest.CollidedRenderObj = this;
		Matrix3D::Rotate_Vector(world, raytest.Result->Normal, &(raytest.Result->Normal));
		if (raytest.Result->ComputeContactPoint)
			Matrix3D::Transform_Vector(world, raytest.Result->ContactPoint, &(raytest.Result->ContactPoint));
	}

	return hit;
}

bool MeshClass::Cast_AABox(AABoxCollisionTestClass &boxtest)
{
	if (Model == nullptr || (Get_Collision_Type() & boxtest.CollisionType) == 0 || boxtest.Result->StartBad)
		return false;

	const bool hit = Model->Cast_World_Space_AABox(boxtest, Get_Transform());
	if (hit)
		boxtest.CollidedRenderObj = this;

	return hit;
}

bool MeshClass::Cast_OBBox(OBBoxCollisionTestClass &boxtest)
{
	if (Model == nullptr || (Get_Collision_Type() & boxtest.CollisionType) == 0 || boxtest.Result->StartBad)
		return false;

	Matrix3D world_to_obj;
	Get_Transform().Get_Orthogonal_Inverse(world_to_obj);
	OBBoxCollisionTestClass localtest(boxtest, world_to_obj);
	const bool hit = Model->Cast_OBBox(localtest);
	if (hit)
	{
		boxtest.CollidedRenderObj = this;
		Matrix3D::Rotate_Vector(Get_Transform(), boxtest.Result->Normal, &(boxtest.Result->Normal));
		if (boxtest.Result->ComputeContactPoint)
			Matrix3D::Transform_Vector(Get_Transform(), boxtest.Result->ContactPoint, &(boxtest.Result->ContactPoint));
	}

	return hit;
}

bool MeshClass::Intersect_AABox(AABoxIntersectionTestClass &boxtest)
{
	if (Model == nullptr || (Get_Collision_Type() & boxtest.CollisionType) == 0)
		return false;

	Matrix3D inv_tm;
	Get_Transform().Get_Orthogonal_Inverse(inv_tm);
	OBBoxIntersectionTestClass local_test(boxtest, inv_tm);
	return Model->Intersect_OBBox(local_test);
}

bool MeshClass::Intersect_OBBox(OBBoxIntersectionTestClass &boxtest)
{
	if (Model == nullptr || (Get_Collision_Type() & boxtest.CollisionType) == 0)
		return false;

	Matrix3D inv_tm;
	Get_Transform().Get_Orthogonal_Inverse(inv_tm);
	OBBoxIntersectionTestClass local_test(boxtest, inv_tm);
	return Model->Intersect_OBBox(local_test);
}

void MeshClass::Get_Obj_Space_Bounding_Sphere(SphereClass &sphere) const
{
	if (Model != nullptr)
	{
		Model->Get_Bounding_Sphere(&sphere);
	}
	else
	{
		sphere.Center.Set(0.0f, 0.0f, 0.0f);
		sphere.Radius = 1.0f;
	}
}

void MeshClass::Get_Obj_Space_Bounding_Box(AABoxClass &box) const
{
	if (Model != nullptr)
	{
		Model->Get_Bounding_Box(&box);
	}
	else
	{
		box.Init(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	}
}

void MeshClass::Generate_Culling_Tree()
{
	if (Model != nullptr)
		Model->Generate_Culling_Tree();
}

void MeshClass::Add_Dependencies_To_List(DynamicVectorClass<StringClass> &file_list, bool textures_only)
{
	MaterialInfoClass *material = Get_Material_Info();
	if (material != nullptr)
	{
		for (int index = 0; index < material->Texture_Count(); ++index)
		{
			TextureClass *texture = material->Peek_Texture(index);
			if (texture != nullptr)
				file_list.Add(texture->Get_Full_Path());
		}

		material->Release_Ref();
	}

	RenderObjClass::Add_Dependencies_To_List(file_list, textures_only);
}

void MeshClass::Update_Cached_Bounding_Volumes() const
{
	Get_Obj_Space_Bounding_Sphere(CachedBoundingSphere);
	Vector3 worldCenter;
	Matrix3D::Transform_Vector(Get_Transform(), CachedBoundingSphere.Center, &worldCenter);
	CachedBoundingSphere.Center = worldCenter;

	if (Model != nullptr && (Model->Get_Flag(MeshModelClass::ALIGNED) || Model->Get_Flag(MeshModelClass::ORIENTED)))
	{
		CachedBoundingBox.Center = CachedBoundingSphere.Center;
		CachedBoundingBox.Extent.Set(CachedBoundingSphere.Radius, CachedBoundingSphere.Radius, CachedBoundingSphere.Radius);
	}
	else
	{
		Get_Obj_Space_Bounding_Box(CachedBoundingBox);
		CachedBoundingBox.Transform(Get_Transform());
	}

	Validate_Cached_Bounding_Volumes();
}

void Set_MeshModel_Flag(RenderObjClass *robj, int flag, int onoff)
{
	if (robj == nullptr)
		return;

	if (robj->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh = static_cast<MeshClass *>(robj);
		MeshModelClass *model = mesh->Get_Model();
		if (model != nullptr)
		{
			model->Set_Flag(static_cast<MeshModelClass::FlagsType>(flag), onoff != 0);
			model->Release_Ref();
		}
		return;
	}

	for (int i = 0; i < robj->Get_Num_Sub_Objects(); ++i)
	{
		RenderObjClass *sub_obj = robj->Get_Sub_Object(i);
		if (sub_obj != nullptr)
		{
			Set_MeshModel_Flag(sub_obj, flag, onoff);
			sub_obj->Release_Ref();
		}
	}
}

int MeshClass::Get_Sort_Level() const
{
	return (Model != nullptr) ? Model->Get_Sort_Level() : SORT_LEVEL_NONE;
}

void MeshClass::Set_Sort_Level(int level)
{
	if (Model != nullptr)
		Model->Set_Sort_Level(level);
}

int MeshClass::Get_Draw_Call_Count() const
{
	if (Model == nullptr)
		return 0;

	const int polygonRendererCount = Model->PolygonRendererList.Count();
	if (polygonRendererCount > 0)
		return polygonRendererCount;

	if (Model->MatInfo != nullptr && Model->MatInfo->Texture_Count() > 0)
		return Model->MatInfo->Texture_Count();

	return 1;
}
