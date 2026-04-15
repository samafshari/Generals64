/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "WW3D2/hlod.h"
#include "WW3D2/hanim.h"
#include "WW3D2/shader.h"
#include "WW3D2/matinfo.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/texture.h"
#include "WW3D2/texturefilter.h"

#include "W3DDevice/GameClient/W3DAssetManager.h"

namespace
{
	bool HasMeaningfulColor(int color)
	{
		return (color & 0x00FFFFFF) != 0;
	}

	bool HasTextureSwap(const char *oldTexture, const char *newTexture)
	{
		return oldTexture != nullptr
			&& newTexture != nullptr
			&& oldTexture[0] != '\0'
			&& newTexture[0] != '\0';
	}
}

W3DAssetManager::W3DAssetManager()
{
}

W3DAssetManager::~W3DAssetManager()
{
}

RenderObjClass *W3DAssetManager::Create_Render_Obj(const char *name)
{
	return WW3DAssetManager::Create_Render_Obj(name);
}

HAnimClass *W3DAssetManager::Get_HAnim(const char *name)
{
	return WW3DAssetManager::Get_HAnim(name);
}

bool W3DAssetManager::Load_3D_Assets(const char *filename)
{
	return WW3DAssetManager::Load_3D_Assets(filename);
}

TextureClass *W3DAssetManager::Get_Texture(
	const char *filename,
	MipCountType mip_level_count,
	WW3DFormat texture_format,
	bool allow_compression,
	TextureBaseClass::TexAssetType type,
	bool allow_reduction)
{
	if (filename != nullptr && filename[0] != '\0' && _strnicmp(filename, "ZHC", 3) == 0)
	{
		allow_reduction = false;
	}

	return WW3DAssetManager::Get_Texture(
		filename,
		mip_level_count,
		texture_format,
		allow_compression,
		type,
		allow_reduction);
}

int W3DAssetManager::replaceAssetTexture(RenderObjClass *robj, TextureClass *oldTex, TextureClass *newTex)
{
	if (robj == nullptr || oldTex == nullptr || newTex == nullptr)
	{
		return 0;
	}

	switch (robj->Class_ID())
	{
	case RenderObjClass::CLASSID_MESH:
		return replaceMeshTexture(robj, oldTex, newTex);
	case RenderObjClass::CLASSID_HLOD:
		return replaceHLODTexture(robj, oldTex, newTex);
	default:
		{
			int didReplace = 0;
			const int subObjectCount = robj->Get_Num_Sub_Objects();
			for (int i = 0; i < subObjectCount; ++i)
			{
				RenderObjClass *subObject = robj->Get_Sub_Object(i);
				didReplace |= replaceAssetTexture(subObject, oldTex, newTex);
				REF_PTR_RELEASE(subObject);
			}
			return didReplace;
		}
	}
}

Int W3DAssetManager::replaceHLODTexture(RenderObjClass *robj, TextureClass *oldTex, TextureClass *newTex)
{
	if (robj == nullptr)
	{
		return 0;
	}

	int didReplace = 0;
	const int subObjectCount = robj->Get_Num_Sub_Objects();
	for (int i = 0; i < subObjectCount; ++i)
	{
		RenderObjClass *subObject = robj->Get_Sub_Object(i);
		didReplace |= replaceAssetTexture(subObject, oldTex, newTex);
		REF_PTR_RELEASE(subObject);
	}

	return didReplace;
}

Int W3DAssetManager::replaceMeshTexture(RenderObjClass *robj, TextureClass *oldTex, TextureClass *newTex)
{
	if (robj == nullptr || oldTex == nullptr || newTex == nullptr)
	{
		return 0;
	}

	MeshClass *mesh = static_cast<MeshClass *>(robj);
	MeshModelClass *model = mesh->Get_Model();
	MaterialInfoClass *material = mesh->Get_Material_Info();
	int didReplace = 0;

	if (model != nullptr && material != nullptr)
	{
		for (int i = 0; i < material->Texture_Count(); ++i)
		{
			TextureClass *candidate = material->Peek_Texture(i);
			if (candidate == nullptr)
			{
				continue;
			}

			if (candidate == oldTex || stricmp(candidate->Get_Texture_Name(), oldTex->Get_Texture_Name()) == 0)
			{
				model->Replace_Texture(candidate, newTex);
				material->Replace_Texture(i, newTex);
				didReplace = 1;
			}
		}
	}

	REF_PTR_RELEASE(material);
	REF_PTR_RELEASE(model);
	return didReplace;
}

int W3DAssetManager::replacePrototypeTexture(RenderObjClass *robj, const char *oldname, const char *newname)
{
	TextureClass *oldTex = Get_Texture(oldname);
	TextureClass *newTex = Get_Texture(newname);
	const int didReplace = replaceAssetTexture(robj, oldTex, newTex);
	REF_PTR_RELEASE(newTex);
	REF_PTR_RELEASE(oldTex);
	return didReplace;
}

RenderObjClass *W3DAssetManager::Create_Render_Obj(
	const char *name,
	float scale,
	const int color,
	const char *oldTexture,
	const char *newTexture)
{
	RenderObjClass *renderObject = WW3DAssetManager::Create_Render_Obj(name);
	if (renderObject == nullptr)
	{
		return nullptr;
	}

	if (scale < 0.9999f || scale > 1.0001f)
	{
		renderObject->Scale(scale);
	}

	if (HasMeaningfulColor(color))
	{
		renderObject->Set_ObjectColor(color);
	}

	if (HasTextureSwap(oldTexture, newTexture))
	{
		TextureClass *oldTex = Get_Texture(oldTexture);
		TextureClass *newTex = Get_Texture(newTexture);
		replaceAssetTexture(renderObject, oldTex, newTex);
		REF_PTR_RELEASE(newTex);
		REF_PTR_RELEASE(oldTex);
	}

	return renderObject;
}
