#pragma once

#include "Core/Texture.h"
#include "Core/Device.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace Render
{

// Loads and caches game textures (TGA/DDS) from the game's file system.
// Textures are loaded on first use and kept in a cache.
class ImageCache
{
public:
    static ImageCache& Instance();

    // Get or load a texture by filename (e.g. "SCShellBG.tga")
    // Returns nullptr if the file can't be loaded
    Texture* GetTexture(Device& device, const char* filename);

    // Clear all cached textures
    void Clear();

private:
    ImageCache() = default;

    bool LoadTGA(Device& device, const char* filename, Texture& tex);
    bool LoadDDS(Device& device, const char* filename, Texture& tex);

    std::unordered_map<std::string, std::unique_ptr<Texture>> m_cache;
};

} // namespace Render
