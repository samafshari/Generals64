#include "W3DDevice/GameClient/ImageCache.h"
#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

// Use the game's file system to load files from .big archives
#include "Common/FileSystem.h"
#include "Common/File.h"
#include "Common/Registry.h"

// Surface search-path failures / successes in the ImGui Log panel so we
// can tell at a glance whether the asset resolver is working.
#include "Inspector/Inspector.h"

namespace
{
// Names already reported — one line per unique filename.
std::unordered_set<std::string> s_loggedTextureMisses;

File* OpenTextureFile(const char* filename)
{
    if (!TheFileSystem || !filename || !*filename)
        return nullptr;

    std::vector<std::string> candidates;
    candidates.emplace_back(filename);

    const AsciiString language = GetRegistryLanguage();
    if (!language.isEmpty())
    {
        candidates.emplace_back("Data/" + std::string(language.str()) + "/Art/Textures/" + filename);
        candidates.emplace_back("Data\\" + std::string(language.str()) + "\\Art\\Textures\\" + filename);
    }

    candidates.emplace_back("Art/Textures/" + std::string(filename));
    candidates.emplace_back("Art\\Textures\\" + std::string(filename));

    for (const std::string& candidate : candidates)
    {
        if (File* file = TheFileSystem->openFile(candidate.c_str(), File::READ | File::BINARY))
            return file;
    }

    // Every candidate missed. Report once per unique filename so the Log
    // panel doesn't get spammed, and so the user can see the real list of
    // textures the engine couldn't find through the search-path system.
    const std::string key(filename);
    if (s_loggedTextureMisses.insert(key).second)
    {
        Inspector::Log("[Texture MISS] %s (tried %zu candidate paths)",
            filename, candidates.size());
    }

    return nullptr;
}
}

namespace Render
{

ImageCache& ImageCache::Instance()
{
    static ImageCache s_instance;
    return s_instance;
}

Texture* ImageCache::GetTexture(Device& device, const char* filename)
{
    if (!filename || !*filename)
        return nullptr;

    auto it = m_cache.find(filename);
    if (it != m_cache.end())
        return it->second.get();

    // Try loading
    auto tex = std::make_unique<Texture>();
    bool loaded = false;

    // Try DDS first, then TGA
    size_t len = strlen(filename);
    if (len > 4)
    {
        const char* ext = filename + len - 4;
        if (_stricmp(ext, ".dds") == 0)
            loaded = LoadDDS(device, filename, *tex);
        else if (_stricmp(ext, ".tga") == 0)
            loaded = LoadTGA(device, filename, *tex);
    }

    // Try common texture directories
    if (!loaded)
    {
        // Map thumbnails use underscores for path separators
        // e.g. "maps_alpine assault_alpine assault.tga"
        // Convert underscores to directory separators and try
        std::string converted(filename);
        // Replace first underscore with /
        size_t pos = converted.find('_');
        while (pos != std::string::npos && !loaded)
        {
            converted[pos] = '/';
            loaded = LoadTGA(device, converted.c_str(), *tex);
            if (!loaded)
                loaded = LoadDDS(device, converted.c_str(), *tex);
            pos = converted.find('_', pos + 1);
        }
    }

    if (!loaded)
    {
        // Try replacing extension
        std::string ddsName(filename);
        if (ddsName.size() > 4)
        {
            ddsName.replace(ddsName.size() - 4, 4, ".dds");
            loaded = LoadDDS(device, ddsName.c_str(), *tex);
        }
        if (!loaded)
        {
            std::string tgaName(filename);
            if (tgaName.size() > 4)
            {
                tgaName.replace(tgaName.size() - 4, 4, ".tga");
                loaded = LoadTGA(device, tgaName.c_str(), *tex);
            }
        }
    }

    if (!loaded)
    {
        // Debug texture_miss.log writing removed
        m_cache[filename] = nullptr;
        return nullptr;
    }

    Texture* result = tex.get();
    m_cache[filename] = std::move(tex);
    return result;
}

void ImageCache::Clear()
{
    m_cache.clear();
}

// TGA file format header
#pragma pack(push, 1)
struct TGAHeader
{
    uint8_t idLength;
    uint8_t colorMapType;
    uint8_t imageType;      // 2 = uncompressed RGB, 10 = RLE RGB
    uint16_t colorMapOrigin;
    uint16_t colorMapLength;
    uint8_t colorMapDepth;
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t bitsPerPixel;
    uint8_t imageDescriptor;
};
#pragma pack(pop)

bool ImageCache::LoadTGA(Device& device, const char* filename, Texture& tex)
{
    File* file = OpenTextureFile(filename);
    if (!file)
        return false;

    // Read entire file
    int fileSize = file->size();
    if (fileSize < (int)sizeof(TGAHeader))
    {
        file->close();
        return false;
    }

    std::vector<uint8_t> fileData(fileSize);
    file->read(fileData.data(), fileSize);
    file->close();

    const TGAHeader* header = (const TGAHeader*)fileData.data();

    if (header->imageType != 2 && header->imageType != 10)
        return false; // Only uncompressed and RLE RGB supported

    uint32_t width = header->width;
    uint32_t height = header->height;
    uint32_t bpp = header->bitsPerPixel;

    if (bpp != 24 && bpp != 32)
        return false;

    const uint8_t* pixelData = fileData.data() + sizeof(TGAHeader) + header->idLength;
    uint32_t bytesPerPixel = bpp / 8;

    // Convert to RGBA
    std::vector<uint32_t> rgba(width * height);

    if (header->imageType == 2) // Uncompressed
    {
        for (uint32_t y = 0; y < height; y++)
        {
            // TGA is typically bottom-up
            uint32_t destY = (header->imageDescriptor & 0x20) ? y : (height - 1 - y);

            for (uint32_t x = 0; x < width; x++)
            {
                const uint8_t* src = pixelData + (y * width + x) * bytesPerPixel;
                uint8_t b = src[0];
                uint8_t g = src[1];
                uint8_t r = src[2];
                uint8_t a = (bpp == 32) ? src[3] : 255;
                rgba[destY * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }
    }
    else if (header->imageType == 10) // RLE compressed
    {
        uint32_t pixelIndex = 0;
        const uint8_t* src = pixelData;
        const uint8_t* srcEnd = fileData.data() + fileSize;

        while (pixelIndex < width * height && src < srcEnd)
        {
            uint8_t packet = *src++;
            uint32_t count = (packet & 0x7F) + 1;

            if (packet & 0x80) // RLE packet
            {
                if (src + bytesPerPixel > srcEnd) break;
                uint8_t b = src[0], g = src[1], r = src[2];
                uint8_t a = (bpp == 32) ? src[3] : 255;
                src += bytesPerPixel;

                uint32_t color = (a << 24) | (b << 16) | (g << 8) | r;
                for (uint32_t i = 0; i < count && pixelIndex < width * height; i++, pixelIndex++)
                {
                    uint32_t y = pixelIndex / width;
                    uint32_t destY = (header->imageDescriptor & 0x20) ? y : (height - 1 - y);
                    uint32_t x = pixelIndex % width;
                    rgba[destY * width + x] = color;
                }
            }
            else // Raw packet
            {
                for (uint32_t i = 0; i < count && pixelIndex < width * height; i++, pixelIndex++)
                {
                    if (src + bytesPerPixel > srcEnd) break;
                    uint8_t b = src[0], g = src[1], r = src[2];
                    uint8_t a = (bpp == 32) ? src[3] : 255;
                    src += bytesPerPixel;

                    uint32_t y = pixelIndex / width;
                    uint32_t destY = (header->imageDescriptor & 0x20) ? y : (height - 1 - y);
                    uint32_t x = pixelIndex % width;
                    rgba[destY * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
                }
            }
        }
    }

    return tex.CreateFromRGBA(device, rgba.data(), width, height, true);
}

bool ImageCache::LoadDDS(Device& device, const char* filename, Texture& tex)
{
    File* file = OpenTextureFile(filename);
    if (!file)
        return false;

    int fileSize = file->size();
    if (fileSize < 128)
    {
        file->close();
        return false;
    }

    std::vector<uint8_t> fileData(fileSize);
    file->read(fileData.data(), fileSize);
    file->close();

    return tex.CreateFromDDS(device, fileData.data(), fileSize);
}

} // namespace Render
