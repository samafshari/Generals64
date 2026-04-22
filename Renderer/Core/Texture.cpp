#include "Texture.h"
#include "Device.h"
#include <cstring>
#include <vector>
#include <algorithm>

#ifdef BUILD_WITH_D3D11

namespace Render
{

static DXGI_FORMAT ToD3D11Format(PixelFormat fmt)
{
    switch (fmt) {
    case PixelFormat::RGBA8_UNORM:          return DXGI_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::BGRA8_UNORM:          return DXGI_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::R16_UINT:             return DXGI_FORMAT_R16_UINT;
    case PixelFormat::R32_UINT:             return DXGI_FORMAT_R32_UINT;
    case PixelFormat::R32_FLOAT:            return DXGI_FORMAT_R32_FLOAT;
    case PixelFormat::R24G8_TYPELESS:       return DXGI_FORMAT_R24G8_TYPELESS;
    case PixelFormat::D24_UNORM_S8_UINT:    return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case PixelFormat::R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case PixelFormat::R32_TYPELESS:         return DXGI_FORMAT_R32_TYPELESS;
    case PixelFormat::D32_FLOAT:            return DXGI_FORMAT_D32_FLOAT;
    case PixelFormat::BC2_UNORM:            return DXGI_FORMAT_BC2_UNORM;
    case PixelFormat::BC3_UNORM:            return DXGI_FORMAT_BC3_UNORM;
    case PixelFormat::B5G6R5_UNORM:         return DXGI_FORMAT_B5G6R5_UNORM;
    default:                                return DXGI_FORMAT_UNKNOWN;
    }
}

void Texture::Destroy(Device& /*device*/)
{
    m_texture.Reset();
    m_srv.Reset();
    m_rtv.Reset();
    m_dsv.Reset();
    m_width = 0;
    m_height = 0;
}

bool Texture::CreateFromRGBA(Device& device, const void* pixels, uint32_t width, uint32_t height, bool generateMips)
{
    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = generateMips ? 0 : 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (generateMips)
        desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

    if (generateMips)
    {
        // Create empty texture, then upload + generate mips
        HRESULT hr = device.GetDevice()->CreateTexture2D(&desc, nullptr, m_texture.GetAddressOf());
        if (FAILED(hr))
            return false;

        device.GetContext()->UpdateSubresource(m_texture.Get(), 0, nullptr, pixels, width * 4, 0);
    }
    else
    {
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = pixels;
        initData.SysMemPitch = width * 4;

        HRESULT hr = device.GetDevice()->CreateTexture2D(&desc, &initData, m_texture.GetAddressOf());
        if (FAILED(hr))
            return false;
    }

    HRESULT hr = device.GetDevice()->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    if (FAILED(hr))
        return false;

    if (generateMips)
        device.GetContext()->GenerateMips(m_srv.Get());

    return true;
}

bool Texture::CreateFromPixels(Device& device, const void* pixels, uint32_t width, uint32_t height, PixelFormat format, uint32_t bytesPerPixel)
{
    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = ToD3D11Format(format);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels;
    initData.SysMemPitch = width * bytesPerPixel;

    HRESULT hr = device.GetDevice()->CreateTexture2D(&desc, &initData, m_texture.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = device.GetDevice()->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    return SUCCEEDED(hr);
}

bool Texture::CreateDynamic(Device& device, uint32_t width, uint32_t height, PixelFormat format)
{
    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = ToD3D11Format(format);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device.GetDevice()->CreateTexture2D(&desc, nullptr, m_texture.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = device.GetDevice()->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    return SUCCEEDED(hr);
}

bool Texture::UpdateFromRGBA(Device& device, const void* pixels, uint32_t width, uint32_t height)
{
    if (!m_texture)
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = device.GetContext()->Map(m_texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
        return false;

    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
    uint32_t srcPitch = width * 4;
    for (uint32_t row = 0; row < height; ++row)
    {
        memcpy(dst, src, srcPitch);
        src += srcPitch;
        dst += mapped.RowPitch;
    }

    device.GetContext()->Unmap(m_texture.Get(), 0);
    return true;
}

bool Texture::CreateFromDDS(Device& device, const void* data, size_t size)
{
    // DDS magic number: "DDS " (0x20534444)
    if (size < 128 || memcmp(data, "DDS ", 4) != 0)
        return false;

    // Parse DDS header
    struct DDSHeader
    {
        uint32_t magic;
        uint32_t size;
        uint32_t flags;
        uint32_t height;
        uint32_t width;
        uint32_t pitchOrLinearSize;
        uint32_t depth;
        uint32_t mipMapCount;
        uint32_t reserved1[11];
        // Pixel format
        uint32_t pfSize;
        uint32_t pfFlags;
        uint32_t pfFourCC;
        uint32_t pfRGBBitCount;
        uint32_t pfRBitMask;
        uint32_t pfGBitMask;
        uint32_t pfBBitMask;
        uint32_t pfABitMask;
        uint32_t caps;
        uint32_t caps2;
        uint32_t caps3;
        uint32_t caps4;
        uint32_t reserved2;
    };

    const DDSHeader* header = (const DDSHeader*)data;
    m_width = header->width;
    m_height = header->height;
    uint32_t mipCount = header->mipMapCount > 0 ? header->mipMapCount : 1;

    // Determine format from pixel format
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t blockSize = 0;
    bool compressed = false;

    const uint32_t DDPF_FOURCC = 0x4;
    const uint32_t DDPF_RGB = 0x40;

    if (header->pfFlags & DDPF_FOURCC)
    {
        compressed = true;
        switch (header->pfFourCC)
        {
        case '1TXD': // DXT1 — decompress to RGBA8 with alpha derived from brightness
        {
            m_hasAlpha = false;
            const uint8_t* src = (const uint8_t*)data + 128;
            uint32_t w = header->width, h = header->height;
            std::vector<uint32_t> pixels(w * h);

            uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
            for (uint32_t by = 0; by < bh; ++by)
            {
                for (uint32_t bx = 0; bx < bw; ++bx)
                {
                    const uint8_t* block = src + (by * bw + bx) * 8;
                    uint16_t c0 = block[0] | (block[1] << 8);
                    uint16_t c1 = block[2] | (block[3] << 8);

                    uint8_t r[4], g[4], b[4];
                    r[0] = ((c0 >> 11) & 0x1F) * 255 / 31;
                    g[0] = ((c0 >> 5)  & 0x3F) * 255 / 63;
                    b[0] = (c0         & 0x1F) * 255 / 31;
                    r[1] = ((c1 >> 11) & 0x1F) * 255 / 31;
                    g[1] = ((c1 >> 5)  & 0x3F) * 255 / 63;
                    b[1] = (c1         & 0x1F) * 255 / 31;

                    if (c0 > c1) {
                        r[2] = (2*r[0] + r[1]) / 3; g[2] = (2*g[0] + g[1]) / 3; b[2] = (2*b[0] + b[1]) / 3;
                        r[3] = (r[0] + 2*r[1]) / 3; g[3] = (g[0] + 2*g[1]) / 3; b[3] = (b[0] + 2*b[1]) / 3;
                    } else {
                        r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
                        r[3] = 0; g[3] = 0; b[3] = 0;
                    }

                    uint32_t lookup = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);
                    for (int i = 0; i < 16; ++i)
                    {
                        uint32_t px = bx * 4 + (i % 4);
                        uint32_t py = by * 4 + (i / 4);
                        if (px >= w || py >= h) continue;

                        int idx = (lookup >> (i * 2)) & 3;
                        uint8_t pr = r[idx], pg = g[idx], pb = b[idx];
                        // DXT1 alpha: only index 3 when c0<=c1 is transparent (punch-through)
                        uint8_t pa = (c0 <= c1 && idx == 3) ? 0 : 255;
                        pixels[py * w + px] = pr | (pg << 8) | (pb << 16) | (pa << 24);
                    }
                }
            }

            return CreateFromRGBA(device, pixels.data(), w, h, true);
        }
        case '3TXD': // DXT3
            format = DXGI_FORMAT_BC2_UNORM;
            blockSize = 16;
            break;
        case '5TXD': // DXT5
            format = DXGI_FORMAT_BC3_UNORM;
            blockSize = 16;
            break;
        default:
            return false;
        }
    }
    else if (header->pfFlags & DDPF_RGB)
    {
        if (header->pfRGBBitCount == 32)
        {
            if (header->pfABitMask == 0xFF000000 && header->pfRBitMask == 0x00FF0000 &&
                header->pfGBitMask == 0x0000FF00 && header->pfBBitMask == 0x000000FF)
            {
                format = DXGI_FORMAT_B8G8R8A8_UNORM;
            }
            else
            {
                format = DXGI_FORMAT_R8G8B8A8_UNORM;
            }
        }
        else if (header->pfRGBBitCount == 16)
        {
            format = DXGI_FORMAT_B5G6R5_UNORM;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    if (format == DXGI_FORMAT_UNKNOWN)
        return false;

    const uint8_t* pixelData = (const uint8_t*)data + 128; // skip header

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.MipLevels = mipCount;
    texDesc.ArraySize = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    // Build subresource data for all mip levels
    std::vector<D3D11_SUBRESOURCE_DATA> subresources(mipCount);
    const uint8_t* pData = pixelData;
    uint32_t w = m_width;
    uint32_t h = m_height;

    for (uint32_t i = 0; i < mipCount; i++)
    {
        subresources[i].pSysMem = pData;

        if (compressed)
        {
            uint32_t blocksW = (w + 3) / 4;
            uint32_t blocksH = (h + 3) / 4;
            subresources[i].SysMemPitch = blocksW * blockSize;
            pData += blocksW * blocksH * blockSize;
        }
        else
        {
            subresources[i].SysMemPitch = w * (header->pfRGBBitCount / 8);
            pData += w * h * (header->pfRGBBitCount / 8);
        }

        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    HRESULT hr = device.GetDevice()->CreateTexture2D(&texDesc, subresources.data(), m_texture.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = device.GetDevice()->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    return SUCCEEDED(hr);
}

bool Texture::CreateRenderTarget(Device& device, uint32_t width, uint32_t height, PixelFormat format)
{
    // Skip recreation if the RT already exists at the requested size and format
    if (m_texture && m_width == width && m_height == height)
    {
        D3D11_TEXTURE2D_DESC existing;
        m_texture->GetDesc(&existing);
        DXGI_FORMAT dxgiFormat = ToD3D11Format(format);
        if (existing.Format == dxgiFormat && (existing.BindFlags & D3D11_BIND_RENDER_TARGET))
            return true;
    }

    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = ToD3D11Format(format);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    HRESULT hr = device.GetDevice()->CreateTexture2D(&desc, nullptr, m_texture.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = device.GetDevice()->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = device.GetDevice()->CreateRenderTargetView(m_texture.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr))
        return false;

    // D3D11 CreateTexture2D(nullptr) leaves the texture contents undefined,
    // which on some drivers is 0xFFFFFFFF (solid white). If any downstream
    // pass samples this RT before fully writing it — e.g. the scene is
    // rendered into a viewport smaller than the RT, or a CopyResource into
    // it silently no-ops because of a size mismatch with its source — the
    // undefined region surfaces as a visible white rectangle in the final
    // frame (RenderDoc confirmed this via a "white everywhere the scene
    // didn't overwrite" pattern in the post-FX sceneTexture input). Clear
    // to transparent black here so first-use-before-write is benign.
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    device.GetContext()->ClearRenderTargetView(m_rtv.Get(), zero);
    return true;
}

bool Texture::CreateDepthTarget(Device& device, uint32_t width, uint32_t height)
{
    // Skip recreation if the depth target already exists at the requested size
    if (m_texture && m_width == width && m_height == height)
    {
        D3D11_TEXTURE2D_DESC existing;
        m_texture->GetDesc(&existing);
        if ((existing.BindFlags & D3D11_BIND_DEPTH_STENCIL))
            return true;
    }

    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device.GetDevice()->CreateTexture2D(&desc, nullptr, m_texture.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device.GetDevice()->CreateDepthStencilView(m_texture.Get(), &dsvDesc, m_dsv.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device.GetDevice()->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srv.GetAddressOf());
    if (FAILED(hr))
        return false;

    // Clear depth to the far plane (1.0) immediately so any sampler reading
    // this target before it's written sees "nothing in front" rather than
    // undefined driver memory — same class of fix as the colour RT above.
    device.GetContext()->ClearDepthStencilView(
        m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    return true;
}

bool Texture::IsValid() const { return m_srv.Get() != nullptr; }

void Texture::BindPS(Device& device, uint32_t slot) const
{
    device.GetContext()->PSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}

void Texture::BindVS(Device& device, uint32_t slot) const
{
    device.GetContext()->VSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}

} // namespace Render

#endif // BUILD_WITH_D3D11
