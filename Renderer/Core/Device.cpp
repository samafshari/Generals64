#include "Device.h"
#include "Texture.h"

#include <cassert>
#include <vector>
#include <cstdio>

#ifdef BUILD_WITH_D3D11
#include <d3d11sdklayers.h>
#endif

namespace Render
{

#ifdef BUILD_WITH_D3D11

bool Device::Init(const DeviceConfig& config)
{
    m_nativeWindow = config.nativeWindowHandle;
    HWND hwnd = static_cast<HWND>(config.nativeWindowHandle);
    m_vsync = config.vsync;

    RECT rc;
    GetClientRect(hwnd, &rc);
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;

    if (m_width <= 0 || m_height <= 0)
    {
        m_width = 800;
        m_height = 600;
    }

    UINT createFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;
    if (config.debug)
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION,
        m_device.GetAddressOf(),
        &featureLevel,
        m_context.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    if (!CreateSwapChain())
        return false;

    if (!CreateBackBufferViews())
        return false;

    // Set default viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_initialized = true;
    return true;
}

bool Device::CreateSwapChain()
{
    ComPtr<IDXGIDevice1> dxgiDevice;
    m_device.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(adapter.GetAddressOf());

    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));

    // Query DXGI 1.5 tearing support. Required to call Present(0, DXGI_PRESENT_ALLOW_TEARING)
    // which is the only way to defeat DWM's windowed-vsync composition pacing and actually
    // render at refresh rates above the monitor's Hz. Without this, Present(0,0) under
    // FLIP_DISCARD still gets paced to the monitor refresh on Windows 10+.
    m_tearingSupported = false;
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory.As(&factory5)))
        {
            BOOL allow = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
                m_tearingSupported = (allow == TRUE);
        }
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    if (m_tearingSupported && !m_vsync)
        desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    HWND hwnd = static_cast<HWND>(m_nativeWindow);
    HRESULT hr = factory->CreateSwapChainForHwnd(
        m_device.Get(),
        hwnd,
        &desc,
        nullptr,
        nullptr,
        m_swapChain.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    // Disable Alt+Enter fullscreen toggle - we handle windowing ourselves
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    return true;
}

bool Device::CreateBackBufferViews()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr))
        return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBufferRTV.GetAddressOf());
    if (FAILED(hr))
        return false;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
    if (FAILED(hr))
        return false;

    // Read-only DSV allows simultaneous depth testing + SRV sampling
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvRODesc = dsvDesc;
    dsvRODesc.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvRODesc, m_depthStencilViewRO.GetAddressOf());
    if (FAILED(hr))
        return false;

    // SRV for reading depth in pixel shaders (e.g. water shore foam)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &srvDesc, m_depthSRV.GetAddressOf());
    if (FAILED(hr))
        return false;

    return true;
}

void Device::ReleaseBackBufferViews()
{
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_backBufferRTV.Reset();
    m_depthSRV.Reset();
    m_depthStencilViewRO.Reset();
    m_depthStencilView.Reset();
    m_depthStencilBuffer.Reset();
}

void Device::Shutdown()
{
    if (m_context)
        m_context->ClearState();

    ReleaseBackBufferViews();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
    m_initialized = false;
}

void Device::BeginFrame(float clearR, float clearG, float clearB, float clearA)
{
    // With FLIP_DISCARD + BufferCount=2, the backbuffer index rotates after
    // each Present. The cached RTV may point to the wrong buffer (front instead
    // of back). Re-acquire buffer 0's RTV each frame to ensure we render to
    // the CURRENT backbuffer.
    {
        m_backBufferRTV.Reset();
        ComPtr<ID3D11Texture2D> backBuffer;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (backBuffer)
            m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBufferRTV.GetAddressOf());
    }

    float clearColor[4] = { clearR, clearG, clearB, clearA };
    m_context->ClearDepthStencilView(m_depthStencilView.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    if (m_redirectRTV)
    {
        // Inspector "game viewport" mode: clear BOTH targets and bind
        // the redirect for engine rendering. The real backbuffer gets
        // a dark editor-gray fill so the area around docked panels
        // is visible (the panels won't cover the entire backbuffer).
        // ImGui will later draw on top of that gray.
        m_context->ClearRenderTargetView(m_redirectRTV, clearColor);
        const float editorBg[4] = { 0.10f, 0.12f, 0.14f, 1.0f };
        m_context->ClearRenderTargetView(m_backBufferRTV.Get(), editorBg);
        m_context->OMSetRenderTargets(1, &m_redirectRTV, m_depthStencilView.Get());
    }
    else
    {
        m_context->ClearRenderTargetView(m_backBufferRTV.Get(), clearColor);
        m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthStencilView.Get());
    }
}

void Device::SetRedirectRTV(ID3D11RenderTargetView* rtv)
{
    m_redirectRTV = rtv;
}

void Device::SetBackBufferDirect()
{
    m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthStencilView.Get());
}

void Device::ClearBackBufferDirect(float r, float g, float b, float a)
{
    const float c[4] = { r, g, b, a };
    if (m_backBufferRTV)
        m_context->ClearRenderTargetView(m_backBufferRTV.Get(), c);
}

void Device::EndFrame()
{
    const UINT presentFlags = (!m_vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    m_swapChain->Present(m_vsync ? 1 : 0, presentFlags);

    // Flush D3D11 debug messages if debug layer is active
    static int s_debugDumpFrame = 0;
    if (s_debugDumpFrame < 5) // only dump first 5 frames to avoid huge logs
    {
        ComPtr<ID3D11InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue)))
        {
            UINT64 msgCount = infoQueue->GetNumStoredMessages();
            if (msgCount > 0)
            {
                FILE* f = fopen("d3d11_debug.log", s_debugDumpFrame == 0 ? "w" : "a");
                if (f)
                {
                    fprintf(f, "=== Frame %d: %llu messages ===\n", s_debugDumpFrame, msgCount);
                    for (UINT64 i = 0; i < msgCount && i < 50; ++i)
                    {
                        SIZE_T msgSize = 0;
                        infoQueue->GetMessage(i, nullptr, &msgSize);
                        std::vector<char> buf(msgSize);
                        D3D11_MESSAGE* msg = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
                        if (SUCCEEDED(infoQueue->GetMessage(i, msg, &msgSize)))
                        {
                            const char* severity = "?";
                            switch (msg->Severity)
                            {
                            case D3D11_MESSAGE_SEVERITY_CORRUPTION: severity = "CORRUPT"; break;
                            case D3D11_MESSAGE_SEVERITY_ERROR: severity = "ERROR"; break;
                            case D3D11_MESSAGE_SEVERITY_WARNING: severity = "WARN"; break;
                            case D3D11_MESSAGE_SEVERITY_INFO: severity = "INFO"; break;
                            case D3D11_MESSAGE_SEVERITY_MESSAGE: severity = "MSG"; break;
                            }
                            fprintf(f, "[%s] %.*s\n", severity, (int)msg->DescriptionByteLength, msg->pDescription);
                        }
                    }
                    fclose(f);
                }
                infoQueue->ClearStoredMessages();
            }
        }
        ++s_debugDumpFrame;
    }
}

void Device::Resize(int width, int height)
{
    if (!m_swapChain || !m_context)
        return;
    if (width <= 0 || height <= 0)
        return;
    if (width == m_width && height == m_height)
        return;

    m_width = width;
    m_height = height;

    // IDXGISwapChain::ResizeBuffers fails silently if ANY reference to
    // the backbuffer (RTV, UAV, shared SRV) or to backbuffer-sized
    // resources (depth SRV bound to a PS slot, sceneRT bound as input)
    // is still live. ClearState drops every PSSet/VSSet/OMSet binding
    // and Flush drains outstanding GPU work. Without this the resize
    // silently no-ops, the next frame renders to the wrong extent,
    // and the HUD draws at coordinates that now clip off-screen —
    // which is exactly the "HUD disappears / corrupts on resize" symptom.
    m_context->ClearState();
    m_context->Flush();
    m_redirectRTV = nullptr;

    ReleaseBackBufferViews();

    UINT resizeFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    if (m_tearingSupported && !m_vsync)
        resizeFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    HRESULT hr = m_swapChain->ResizeBuffers(0, (UINT)width, (UINT)height,
                                            DXGI_FORMAT_UNKNOWN, resizeFlags);
    if (FAILED(hr))
    {
        // Device-removed is unrecoverable here; swallow and keep the old
        // backbuffer dimensions so CreateBackBufferViews at least rebuilds
        // the depth buffer and we can limp to the next frame.
        m_width = 0;
        m_height = 0;
    }

    // DXGI can clamp the requested extent (monitor size, DPI). Trust the
    // swap chain for the authoritative dimensions so subsequent viewport
    // math matches the actual backbuffer.
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    if (SUCCEEDED(m_swapChain->GetDesc1(&desc)) && desc.Width && desc.Height)
    {
        m_width = (int)desc.Width;
        m_height = (int)desc.Height;
    }
    else if (m_width == 0 || m_height == 0)
    {
        m_width = width;
        m_height = height;
    }

    CreateBackBufferViews();

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

bool Device::CaptureScreenshot(const char* filename)
{
    // Get the backbuffer texture
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = m_device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
    if (FAILED(hr)) return false;

    m_context->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    // Write BMP file
    int w = (int)desc.Width;
    int h = (int)desc.Height;
    int rowBytes = w * 3; // BGR
    int padBytes = (4 - (rowBytes % 4)) % 4;
    int stride = rowBytes + padBytes;
    int imageSize = stride * h;
    int fileSize = 54 + imageSize;

    FILE* f = fopen(filename, "wb");
    if (!f)
    {
        m_context->Unmap(staging.Get(), 0);
        return false;
    }

    // BMP header
    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(int*)(hdr + 2) = fileSize;
    *(int*)(hdr + 10) = 54;
    *(int*)(hdr + 14) = 40;
    *(int*)(hdr + 18) = w;
    *(int*)(hdr + 22) = -h; // top-down
    *(short*)(hdr + 26) = 1;
    *(short*)(hdr + 28) = 24;
    *(int*)(hdr + 34) = imageSize;
    fwrite(hdr, 1, 54, f);

    // Pixel data: DXGI_FORMAT_R8G8B8A8_UNORM → BGR
    std::vector<uint8_t> row(stride, 0);
    const uint8_t* src = (const uint8_t*)mapped.pData;
    for (int y = 0; y < h; ++y)
    {
        const uint32_t* srcRow = (const uint32_t*)(src + y * mapped.RowPitch);
        for (int x = 0; x < w; ++x)
        {
            uint32_t px = srcRow[x];
            row[x * 3 + 0] = (px >> 16) & 0xFF; // B (from R)
            row[x * 3 + 1] = (px >> 8)  & 0xFF; // G
            row[x * 3 + 2] = (px >> 0)  & 0xFF; // R (from B)
        }
        fwrite(row.data(), 1, stride, f);
    }
    fclose(f);
    m_context->Unmap(staging.Get(), 0);
    return true;
}

// --- RHI draw commands -------------------------------------------------------

void Device::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
{
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = x;
    vp.TopLeftY = y;
    vp.Width = w;
    vp.Height = h;
    vp.MinDepth = minDepth;
    vp.MaxDepth = maxDepth;
    m_context->RSSetViewports(1, &vp);
}

void Device::SetTopology(Topology topology)
{
    D3D11_PRIMITIVE_TOPOLOGY d3dTopology;
    switch (topology)
    {
    case Topology::TriangleList:  d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    case Topology::TriangleStrip: d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
    case Topology::LineList:      d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
    default:                      d3dTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    }
    m_context->IASetPrimitiveTopology(d3dTopology);
}

void Device::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
    m_context->DrawIndexed(indexCount, startIndex, baseVertex);
}

void Device::Draw(uint32_t vertexCount, uint32_t startVertex)
{
    m_context->Draw(vertexCount, startVertex);
}

void Device::DrawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
    m_context->DrawInstanced(vertexCountPerInstance, instanceCount, startVertex, startInstance);
}

void Device::ClearInputLayout()
{
    m_context->IASetInputLayout(nullptr);
}

// --- RHI render target management --------------------------------------------

void Device::SetRenderTarget(Texture& colorRT)
{
    ID3D11RenderTargetView* rtv = colorRT.GetRTV();
    if (rtv)
        m_context->OMSetRenderTargets(1, &rtv, nullptr);
}

void Device::SetDepthOnlyRenderTarget(Texture& depthRT)
{
    ID3D11RenderTargetView* nullRTV = nullptr;
    m_context->OMSetRenderTargets(1, &nullRTV, depthRT.GetDSV());
}

void Device::SetBackBuffer()
{
    if (m_redirectRTV)
    {
        m_context->OMSetRenderTargets(1, &m_redirectRTV, m_depthStencilView.Get());
    }
    else
    {
        m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthStencilView.Get());
    }
}

void Device::SetBackBufferReadOnlyDepth()
{
    if (m_redirectRTV)
    {
        m_context->OMSetRenderTargets(1, &m_redirectRTV, m_depthStencilViewRO.Get());
    }
    else
    {
        m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthStencilViewRO.Get());
    }
}

void Device::ClearRenderTarget(Texture& rt, float r, float g, float b, float a)
{
    ID3D11RenderTargetView* rtv = rt.GetRTV();
    if (rtv)
    {
        float clearColor[4] = { r, g, b, a };
        m_context->ClearRenderTargetView(rtv, clearColor);
    }
}

// --- RHI texture copies ------------------------------------------------------

void Device::CopyBackBufferToTexture(Texture& dst)
{
    // Use the redirect RTV when the inspector's "game viewport" mode is
    // active so the post-process chain reads from the off-screen game
    // viewport (where the engine actually rendered the scene), not the
    // empty editor backbuffer. Without this, post-process effects in
    // inspector mode operate on the gray editor fill and any objects
    // drawn during the post-chain become invisible in the inspector.
    ID3D11RenderTargetView* sourceRTV = m_redirectRTV ? m_redirectRTV : m_backBufferRTV.Get();
    if (!sourceRTV)
        return;

    ComPtr<ID3D11Resource> bbRes;
    sourceRTV->GetResource(bbRes.GetAddressOf());

    ComPtr<ID3D11Resource> dstRes;
    ID3D11ShaderResourceView* srv = dst.GetSRV();
    if (srv)
    {
        srv->GetResource(dstRes.GetAddressOf());
        if (bbRes && dstRes)
            m_context->CopyResource(dstRes.Get(), bbRes.Get());
    }
}

void Device::CopyTextureToBackBuffer(Texture& src)
{
    // Mirror of CopyBackBufferToTexture: write the post-processed result
    // back into the active render target — redirect RTV when inspector
    // game-viewport mode is on, real backbuffer otherwise.
    ID3D11RenderTargetView* destRTV = m_redirectRTV ? m_redirectRTV : m_backBufferRTV.Get();
    if (!destRTV)
        return;

    ComPtr<ID3D11Resource> bbRes;
    destRTV->GetResource(bbRes.GetAddressOf());

    ComPtr<ID3D11Resource> srcRes;
    ID3D11ShaderResourceView* srv = src.GetSRV();
    if (srv)
    {
        srv->GetResource(srcRes.GetAddressOf());
        if (bbRes && srcRes)
            m_context->CopyResource(bbRes.Get(), srcRes.Get());
    }
}

void Device::CopyTexture(Texture& dst, Texture& src)
{
    ComPtr<ID3D11Resource> dstRes, srcRes;
    if (dst.GetSRV()) dst.GetSRV()->GetResource(dstRes.GetAddressOf());
    if (src.GetSRV()) src.GetSRV()->GetResource(srcRes.GetAddressOf());
    if (dstRes && srcRes)
        m_context->CopyResource(dstRes.Get(), srcRes.Get());
}

// --- RHI SRV binding ---------------------------------------------------------

void Device::ClearDepthStencil(Texture& depthRT)
{
    ID3D11DepthStencilView* dsv = depthRT.GetDSV();
    if (dsv)
        m_context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void Device::UnbindPSSRVs(uint32_t startSlot, uint32_t count)
{
    ID3D11ShaderResourceView* nullSRVs[8] = {};
    if (count > 8) count = 8;
    m_context->PSSetShaderResources(startSlot, count, nullSRVs);
}

void Device::UnbindVSSRVs(uint32_t startSlot, uint32_t count)
{
    ID3D11ShaderResourceView* nullSRVs[8] = {};
    if (count > 8) count = 8;
    m_context->VSSetShaderResources(startSlot, count, nullSRVs);
}

void Device::BindDepthTexturePS(uint32_t slot)
{
    m_context->PSSetShaderResources(slot, 1, m_depthSRV.GetAddressOf());
}

void Device::UnbindVSSamplers(uint32_t startSlot, uint32_t count)
{
    ID3D11SamplerState* nullSamplers[8] = {};
    if (count > 8) count = 8;
    m_context->VSSetSamplers(startSlot, count, nullSamplers);
}

void Device::UnbindVSConstantBuffers(uint32_t startSlot, uint32_t count)
{
    ID3D11Buffer* nullBuffers[8] = {};
    if (count > 8) count = 8;
    m_context->VSSetConstantBuffers(startSlot, count, nullBuffers);
}

void Device::BindPSTextures(uint32_t startSlot, const Texture* const* textures, uint32_t count)
{
    ID3D11ShaderResourceView* srvs[8] = {};
    if (count > 8) count = 8;
    for (uint32_t i = 0; i < count; ++i)
        srvs[i] = textures[i] ? textures[i]->GetSRV() : nullptr;
    m_context->PSSetShaderResources(startSlot, count, srvs);
}

#endif // BUILD_WITH_D3D11

} // namespace Render
