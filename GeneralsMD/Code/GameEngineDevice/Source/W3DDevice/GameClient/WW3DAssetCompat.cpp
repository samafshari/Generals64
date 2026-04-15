/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "WW3D2/texturefilter.h"
#include "WW3D2/texture.h"
#include "WW3D2/assetmgr.h"

#include "WW3D2/agg_def.h"
#include "WW3D2/assetstatus.h"
#include "WW3D2/collect.h"
#include "WW3D2/distlod.h"
#include "WW3D2/hanim.h"
#include "WW3D2/hlod.h"
#include "WW3D2/nullrobj.h"
#include "WW3D2/proto.h"
#include "WW3D2/texture.h"
#include "WW3D2/w3d_file.h"
#include "chunkio.h"
#include "ffactory.h"
#include "realcrc.h"

#include <cstring>

namespace
{
	NullPrototypeClass g_nullPrototype;

	void LowercaseString(StringClass &value)
	{
		TCHAR *buffer = value.Peek_Buffer();
		if (buffer == nullptr)
			return;

		const int length = value.Get_Length();
		for (int i = 0; i < length; ++i)
			buffer[i] = _totlower(buffer[i]);
	}

	bool IsExcludedName(const DynamicVectorClass<StringClass> &names, const char *name)
	{
		for (int i = 0; i < names.Count(); ++i)
		{
			if (stricmp(names[i], name) == 0)
				return true;
		}

		return false;
	}
}

WW3DAssetManager *WW3DAssetManager::TheInstance = nullptr;

void TextureFilterClass::Apply(unsigned int)
{
}

void TextureFilterClass::Set_Mip_Mapping(FilterType mipmap)
{
	MipMapFilter = mipmap;
}

void TextureFilterClass::_Init_Filters(TextureFilterMode)
{
}

void TextureFilterClass::_Set_Default_Min_Filter(FilterType)
{
}

void TextureFilterClass::_Set_Default_Mag_Filter(FilterType)
{
}

void TextureFilterClass::_Set_Default_Mip_Filter(FilterType)
{
}

void TextureBaseClass::Set_Texture_Name(const char *name)
{
	Name = (name != nullptr) ? name : "";
	LowercaseString(Name);
}

IDirect3DBaseTexture8 *TextureBaseClass::Peek_D3D_Base_Texture() const
{
	return D3DTexture;
}

void TextureBaseClass::Set_D3D_Base_Texture(IDirect3DBaseTexture8 *tex)
{
	D3DTexture = tex;
}

unsigned int TextureBaseClass::Get_Priority()
{
	return 0;
}

unsigned int TextureBaseClass::Set_Priority(unsigned int)
{
	return 0;
}

bool TextureBaseClass::Is_Missing_Texture()
{
	return false;
}

void TextureBaseClass::Set_HSV_Shift(const Vector3 &hsv_shift)
{
	HSVShift = hsv_shift;
}

unsigned TextureBaseClass::Get_Reduction() const
{
	return 0;
}

void TextureBaseClass::Invalidate_Old_Unused_Textures(unsigned)
{
}

void TextureBaseClass::Apply_Null(unsigned int)
{
}

int TextureBaseClass::_Get_Total_Locked_Surface_Size()
{
	return 0;
}

int TextureBaseClass::_Get_Total_Texture_Size()
{
	return 0;
}

int TextureBaseClass::_Get_Total_Lightmap_Texture_Size()
{
	return 0;
}

int TextureBaseClass::_Get_Total_Procedural_Texture_Size()
{
	return 0;
}

int TextureBaseClass::_Get_Total_Locked_Surface_Count()
{
	return 0;
}

int TextureBaseClass::_Get_Total_Texture_Count()
{
	return 0;
}

int TextureBaseClass::_Get_Total_Lightmap_Texture_Count()
{
	return 0;
}

int TextureBaseClass::_Get_Total_Procedural_Texture_Count()
{
	return 0;
}

SurfaceClass *TextureClass::Get_Surface_Level(unsigned int)
{
	return nullptr;
}

IDirect3DSurface8 *TextureClass::Get_D3D_Surface_Level(unsigned int)
{
	return nullptr;
}

TextureClass *Load_Texture(ChunkLoadClass &cload)
{
	TextureClass *newtex = nullptr;
	char name[256] = {};
	bool no_lod = false;

	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_TEXTURE)
		return nullptr;

	W3dTextureInfoStruct texinfo = {};
	bool hastexinfo = false;

	while (cload.Open_Chunk())
	{
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_TEXTURE_NAME:
			cload.Read(name, cload.Cur_Chunk_Length());
			break;
		case W3D_CHUNK_TEXTURE_INFO:
			cload.Read(&texinfo, sizeof(texinfo));
			hastexinfo = true;
			break;
		default:
			break;
		}

		cload.Close_Chunk();
	}

	cload.Close_Chunk();

	if (name[0] == '\0' || WW3DAssetManager::Get_Instance() == nullptr)
		return nullptr;

	MipCountType mipcount = MIP_LEVELS_ALL;
	if (hastexinfo)
	{
		no_lod = (texinfo.Attributes & W3DTEXTURE_NO_LOD) == W3DTEXTURE_NO_LOD;
		if (no_lod)
		{
			mipcount = MIP_LEVELS_1;
		}
		else
		{
			switch (texinfo.Attributes & W3DTEXTURE_MIP_LEVELS_MASK)
			{
			case W3DTEXTURE_MIP_LEVELS_2: mipcount = MIP_LEVELS_2; break;
			case W3DTEXTURE_MIP_LEVELS_3: mipcount = MIP_LEVELS_3; break;
			case W3DTEXTURE_MIP_LEVELS_4: mipcount = MIP_LEVELS_4; break;
			case W3DTEXTURE_MIP_LEVELS_ALL:
			default:
				mipcount = MIP_LEVELS_ALL;
				break;
			}
		}
	}

	newtex = WW3DAssetManager::Get_Instance()->Get_Texture(name, mipcount, WW3D_FORMAT_UNKNOWN);
	if (newtex == nullptr)
		return nullptr;

	TextureFilterClass &filter = newtex->Get_Filter();
	filter.Set_Mip_Mapping(no_lod ? TextureFilterClass::FILTER_TYPE_NONE : TextureFilterClass::FILTER_TYPE_BEST);
	filter.Set_U_Addr_Mode((texinfo.Attributes & W3DTEXTURE_CLAMP_U) != 0 ?
		TextureFilterClass::TEXTURE_ADDRESS_CLAMP :
		TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
	filter.Set_V_Addr_Mode((texinfo.Attributes & W3DTEXTURE_CLAMP_V) != 0 ?
		TextureFilterClass::TEXTURE_ADDRESS_CLAMP :
		TextureFilterClass::TEXTURE_ADDRESS_REPEAT);

	return newtex;
}

WW3DAssetManager::WW3DAssetManager() :
	PrototypeLoaders(PROTOLOADERS_VECTOR_SIZE),
	Prototypes(PROTOTYPES_VECTOR_SIZE),
	WW3D_Load_On_Demand(false),
	Activate_Fog_On_Load(false),
	MetalManager(nullptr)
{
	WWASSERT(TheInstance == nullptr);
	TheInstance = this;

	PrototypeLoaders.Set_Growth_Step(PROTOLOADERS_GROWTH_RATE);
	Prototypes.Set_Growth_Step(PROTOTYPES_GROWTH_RATE);

	PrototypeHashTable = W3DNEWARRAY PrototypeClass * [PROTOTYPE_HASH_TABLE_SIZE];
	memset(PrototypeHashTable, 0, sizeof(PrototypeClass *) * PROTOTYPE_HASH_TABLE_SIZE);

	Register_Prototype_Loader(&_MeshLoader);
	Register_Prototype_Loader(&_HModelLoader);
	Register_Prototype_Loader(&_CollectionLoader);
	Register_Prototype_Loader(&_HLodLoader);
	Register_Prototype_Loader(&_DistLODLoader);
	Register_Prototype_Loader(&_AggregateLoader);
	Register_Prototype_Loader(&_NullLoader);
}

WW3DAssetManager::~WW3DAssetManager()
{
	Free();
	TheInstance = nullptr;

	delete[] PrototypeHashTable;
	PrototypeHashTable = nullptr;
}

void WW3DAssetManager::Free()
{
	Free_Assets();
}

void WW3DAssetManager::Free_Assets()
{
	for (int i = Prototypes.Count() - 1; i >= 0; --i)
	{
		PrototypeClass *proto = Prototypes[i];
		Prototypes.Delete(i);

		if (proto != nullptr)
			proto->DeleteSelf();
	}

	if (PrototypeHashTable != nullptr)
	{
		memset(PrototypeHashTable, 0, sizeof(PrototypeClass *) * PROTOTYPE_HASH_TABLE_SIZE);
	}

	HAnimManager.Free_All_Anims();
	HTreeManager.Free_All_Trees();
	Release_All_Textures();
	Release_All_Font3DDatas();
	Release_All_FontChars();
}

void WW3DAssetManager::Release_Unused_Assets()
{
	Release_Unused_Textures();
	Release_Unused_Font3DDatas();
}

void WW3DAssetManager::Free_Assets_With_Exclusion_List(const DynamicVectorClass<StringClass> &exclusion_names)
{
	for (int i = Prototypes.Count() - 1; i >= 0; --i)
	{
		PrototypeClass *proto = Prototypes[i];
		if (proto == nullptr || IsExcludedName(exclusion_names, proto->Get_Name()))
			continue;

		Remove_Prototype(proto);
		proto->DeleteSelf();
	}

	Release_Unused_Assets();
}

void WW3DAssetManager::Create_Asset_List(DynamicVectorClass<StringClass> &model_list)
{
	for (int i = 0; i < Prototypes.Count(); ++i)
	{
		PrototypeClass *proto = Prototypes[i];
		if (proto == nullptr)
			continue;

		const char *name = proto->Get_Name();
		if (strchr(name, '#') == nullptr && strchr(name, '.') == nullptr)
			model_list.Add(StringClass(name));
	}

	HAnimManager.Create_Asset_List(model_list);
}

bool WW3DAssetManager::Load_3D_Assets(const char *filename)
{
	if (filename == nullptr || *filename == '\0')
		return false;

	auto tryLoad = [&](const char *path) -> bool
	{
		bool loaded = false;
		FileClass *file = _TheFileFactory->Get_File(path);
		if (file != nullptr)
		{
			if (file->Is_Available())
				loaded = Load_3D_Assets(*file);
			_TheFileFactory->Return_File(file);
		}

		return loaded;
	};

	if (tryLoad(filename))
		return true;

	if (strchr(filename, '\\') == nullptr && strchr(filename, '/') == nullptr && strchr(filename, ':') == nullptr)
	{
		StringClass artPath("Art\\W3D\\", true);
		artPath += filename;
		if (tryLoad(artPath))
			return true;
	}

	return false;
}

bool WW3DAssetManager::Load_3D_Assets(FileClass &w3dfile)
{
	if (!w3dfile.Open())
		return false;

	ChunkLoadClass cload(&w3dfile);
	while (cload.Open_Chunk())
	{
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_HIERARCHY:
			HTreeManager.Load_Tree(cload);
			break;
		case W3D_CHUNK_ANIMATION:
		case W3D_CHUNK_COMPRESSED_ANIMATION:
		case W3D_CHUNK_MORPH_ANIMATION:
			HAnimManager.Load_Anim(cload);
			break;
		default:
			Load_Prototype(cload);
			break;
		}

		cload.Close_Chunk();
	}

	w3dfile.Close();
	return true;
}

bool WW3DAssetManager::Load_Prototype(ChunkLoadClass &cload)
{
	PrototypeLoaderClass *loader = Find_Prototype_Loader(cload.Cur_Chunk_ID());
	if (loader == nullptr)
		return false;

	PrototypeClass *newproto = loader->Load_W3D(cload);
	if (newproto == nullptr)
		return false;

	if (Render_Obj_Exists(newproto->Get_Name()))
	{
		newproto->DeleteSelf();
		return false;
	}

	Add_Prototype(newproto);
	return true;
}

RenderObjClass *WW3DAssetManager::Create_Render_Obj(const char *name)
{
	if (name == nullptr || *name == '\0')
		return nullptr;

	PrototypeClass *proto = Find_Prototype(name);
	if (proto == nullptr && WW3D_Load_On_Demand)
	{
		AssetStatusClass::Peek_Instance()->Report_Load_On_Demand_RObj(name);

		char filename[MAX_PATH];
		const char *mesh_name = strchr(name, '.');
		if (mesh_name != nullptr)
		{
			lstrcpyn(filename, name, static_cast<int>(mesh_name - name) + 1);
			lstrcat(filename, ".w3d");
		}
		else
		{
			snprintf(filename, ARRAY_SIZE(filename), "%s.w3d", name);
		}

		if (!Load_3D_Assets(filename))
		{
			StringClass parentPath("..\\", true);
			parentPath += filename;
			Load_3D_Assets(parentPath);
		}

		proto = Find_Prototype(name);
	}

	if (proto == nullptr)
	{
		if (name[0] != '#')
			AssetStatusClass::Peek_Instance()->Report_Missing_RObj(name);
		return nullptr;
	}

	return proto->Create();
}

bool WW3DAssetManager::Render_Obj_Exists(const char *name)
{
	return Find_Prototype(name) != nullptr;
}

RenderObjIterator *WW3DAssetManager::Create_Render_Obj_Iterator()
{
	return nullptr;
}

void WW3DAssetManager::Release_Render_Obj_Iterator(RenderObjIterator *iterator)
{
	delete iterator;
}

AssetIterator *WW3DAssetManager::Create_HAnim_Iterator()
{
	return nullptr;
}

HAnimClass *WW3DAssetManager::Get_HAnim(const char *name)
{
	HAnimClass *anim = HAnimManager.Get_Anim(name);
	if (anim == nullptr && WW3D_Load_On_Demand && !HAnimManager.Is_Missing(name))
	{
		AssetStatusClass::Peek_Instance()->Report_Load_On_Demand_HAnim(name);

		const char *animName = strchr(name, '.');
		if (animName != nullptr)
		{
			char filename[MAX_PATH];
			snprintf(filename, ARRAY_SIZE(filename), "%s.w3d", animName + 1);
			if (!Load_3D_Assets(filename))
			{
				StringClass parentPath("..\\", true);
				parentPath += filename;
				Load_3D_Assets(parentPath);
			}
		}

		anim = HAnimManager.Get_Anim(name);
		if (anim == nullptr)
		{
			HAnimManager.Register_Missing(name);
			AssetStatusClass::Peek_Instance()->Report_Missing_HAnim(name);
		}
	}

	return anim;
}

TextureClass *WW3DAssetManager::Get_Texture(
	const char *filename,
	MipCountType mip_level_count,
	WW3DFormat,
	bool,
	TextureBaseClass::TexAssetType,
	bool)
{
	if (filename == nullptr || *filename == '\0')
		return nullptr;

	StringClass key(filename, true);
	LowercaseString(key);

	TextureClass *tex = TextureHash.Get(key);
	if (tex == nullptr)
	{
		tex = NEW_REF(TextureClass, (static_cast<IDirect3DBaseTexture8 *>(nullptr)));
		tex->Set_Texture_Name(key);
		tex->Set_Full_Path(filename);
		tex->Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
		tex->Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
		tex->Get_Filter().Set_Mip_Mapping(
			mip_level_count == MIP_LEVELS_1 ? TextureFilterClass::FILTER_TYPE_NONE : TextureFilterClass::FILTER_TYPE_BEST);
		tex->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
		tex->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
		TextureHash.Insert(tex->Get_Texture_Name(), tex);
	}

	tex->Add_Ref();
	return tex;
}

void WW3DAssetManager::Release_All_Textures()
{
	HashTemplateIterator<StringClass, TextureClass *> iterator(TextureHash);
	for (iterator.First(); !iterator.Is_Done(); iterator.Next())
	{
		TextureClass *tex = iterator.Peek_Value();
		if (tex != nullptr)
			tex->Release_Ref();
	}

	TextureHash.Remove_All();
}

void WW3DAssetManager::Release_Unused_Textures()
{
	DynamicVectorClass<TextureClass *> unused;
	HashTemplateIterator<StringClass, TextureClass *> iterator(TextureHash);
	for (iterator.First(); !iterator.Is_Done(); iterator.Next())
	{
		TextureClass *tex = iterator.Peek_Value();
		if (tex != nullptr && tex->Num_Refs() == 1)
			unused.Add(tex);
	}

	for (int i = 0; i < unused.Count(); ++i)
	{
		TextureHash.Remove(unused[i]->Get_Texture_Name());
		unused[i]->Release_Ref();
	}
}

void WW3DAssetManager::Release_Texture(TextureClass *texture)
{
	if (texture == nullptr)
		return;

	TextureHash.Remove(texture->Get_Texture_Name());
	texture->Release_Ref();
}

void WW3DAssetManager::Load_Procedural_Textures()
{
}

Font3DInstanceClass *WW3DAssetManager::Get_Font3DInstance(const char *)
{
	return nullptr;
}

FontCharsClass *WW3DAssetManager::Get_FontChars(const char *, int, bool)
{
	return nullptr;
}

AssetIterator *WW3DAssetManager::Create_HTree_Iterator()
{
	return nullptr;
}

HTreeClass *WW3DAssetManager::Get_HTree(const char *name)
{
	HTreeClass *tree = HTreeManager.Get_Tree(name);
	if (tree == nullptr && WW3D_Load_On_Demand)
	{
		AssetStatusClass::Peek_Instance()->Report_Load_On_Demand_HTree(name);

		char filename[MAX_PATH];
		snprintf(filename, ARRAY_SIZE(filename), "%s.w3d", name);
		if (!Load_3D_Assets(filename))
		{
			StringClass parentPath("..\\", true);
			parentPath += filename;
			Load_3D_Assets(parentPath);
		}

		tree = HTreeManager.Get_Tree(name);
		if (tree == nullptr)
			AssetStatusClass::Peek_Instance()->Report_Missing_HTree(name);
	}

	return tree;
}

void WW3DAssetManager::Register_Prototype_Loader(PrototypeLoaderClass *loader)
{
	WWASSERT(loader != nullptr);
	PrototypeLoaders.Add(loader);
}

void WW3DAssetManager::Add_Prototype(PrototypeClass *newproto)
{
	WWASSERT(newproto != nullptr);
	const int hash = CRC_Stringi(newproto->Get_Name()) & PROTOTYPE_HASH_MASK;
	newproto->friend_setNextHash(PrototypeHashTable[hash]);
	PrototypeHashTable[hash] = newproto;
	Prototypes.Add(newproto);
}

void WW3DAssetManager::Remove_Prototype(PrototypeClass *proto)
{
	if (proto == nullptr)
		return;

	const char *name = proto->Get_Name();
	const int hash = CRC_Stringi(name) & PROTOTYPE_HASH_MASK;
	PrototypeClass *prev = nullptr;
	for (PrototypeClass *test = PrototypeHashTable[hash]; test != nullptr; test = test->friend_getNextHash())
	{
		if (stricmp(test->Get_Name(), name) == 0)
		{
			if (prev == nullptr)
				PrototypeHashTable[hash] = test->friend_getNextHash();
			else
				prev->friend_setNextHash(test->friend_getNextHash());
			break;
		}

		prev = test;
	}

	Prototypes.Delete(proto);
}

void WW3DAssetManager::Remove_Prototype(const char *name)
{
	PrototypeClass *proto = Find_Prototype(name);
	if (proto != nullptr)
	{
		Remove_Prototype(proto);
		proto->DeleteSelf();
	}
}

PrototypeClass *WW3DAssetManager::Find_Prototype(const char *name)
{
	if (name == nullptr)
		return nullptr;

	if (stricmp(name, "NULL") == 0)
		return &g_nullPrototype;

	const int hash = CRC_Stringi(name) & PROTOTYPE_HASH_MASK;
	for (PrototypeClass *test = PrototypeHashTable[hash]; test != nullptr; test = test->friend_getNextHash())
	{
		if (stricmp(test->Get_Name(), name) == 0)
			return test;
	}

	return nullptr;
}

AssetIterator *WW3DAssetManager::Create_Font3DData_Iterator()
{
	return nullptr;
}

void WW3DAssetManager::Add_Font3DData(Font3DDataClass *)
{
}

void WW3DAssetManager::Remove_Font3DData(Font3DDataClass *)
{
}

Font3DDataClass *WW3DAssetManager::Get_Font3DData(const char *)
{
	return nullptr;
}

void WW3DAssetManager::Release_All_Font3DDatas()
{
}

void WW3DAssetManager::Release_Unused_Font3DDatas()
{
}

void WW3DAssetManager::Release_All_FontChars()
{
}

PrototypeLoaderClass *WW3DAssetManager::Find_Prototype_Loader(int chunk_id)
{
	for (int i = 0; i < PrototypeLoaders.Count(); ++i)
	{
		PrototypeLoaderClass *loader = PrototypeLoaders[i];
		if (loader != nullptr && loader->Chunk_Type() == chunk_id)
			return loader;
	}

	return nullptr;
}
