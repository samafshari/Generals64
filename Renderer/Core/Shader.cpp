#include "Shader.h"
#include "Device.h"

#ifdef BUILD_WITH_D3D11

namespace Render
{

// --- Helper: RHI enum to D3D11 conversions ---

static D3D11_FILL_MODE ToD3D11(FillMode f)
{
    switch (f) {
    case FillMode::Wireframe: return D3D11_FILL_WIREFRAME;
    default:                  return D3D11_FILL_SOLID;
    }
}

static D3D11_CULL_MODE ToD3D11(CullMode c)
{
    switch (c) {
    case CullMode::None:  return D3D11_CULL_NONE;
    case CullMode::Front: return D3D11_CULL_FRONT;
    default:              return D3D11_CULL_BACK;
    }
}

static D3D11_COMPARISON_FUNC ToD3D11(CompareFunc f)
{
    switch (f) {
    case CompareFunc::Never:        return D3D11_COMPARISON_NEVER;
    case CompareFunc::Less:         return D3D11_COMPARISON_LESS;
    case CompareFunc::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
    case CompareFunc::Equal:        return D3D11_COMPARISON_EQUAL;
    case CompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareFunc::Greater:      return D3D11_COMPARISON_GREATER;
    case CompareFunc::NotEqual:     return D3D11_COMPARISON_NOT_EQUAL;
    case CompareFunc::Always:       return D3D11_COMPARISON_ALWAYS;
    default:                        return D3D11_COMPARISON_LESS_EQUAL;
    }
}

static D3D11_FILTER ToD3D11(Filter f)
{
    switch (f) {
    case Filter::MinMagMipPoint:             return D3D11_FILTER_MIN_MAG_MIP_POINT;
    case Filter::MinMagMipLinear:            return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    case Filter::Anisotropic:                return D3D11_FILTER_ANISOTROPIC;
    case Filter::ComparisonMinMagMipLinear:  return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    default:                                 return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    }
}

static D3D11_TEXTURE_ADDRESS_MODE ToD3D11(AddressMode m)
{
    switch (m) {
    case AddressMode::Clamp:  return D3D11_TEXTURE_ADDRESS_CLAMP;
    case AddressMode::Border: return D3D11_TEXTURE_ADDRESS_BORDER;
    case AddressMode::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
    default:                  return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}

static D3D11_BLEND ToD3D11Blend(BlendFactor f)
{
    switch (f) {
    case BlendFactor::Zero:         return D3D11_BLEND_ZERO;
    case BlendFactor::One:          return D3D11_BLEND_ONE;
    case BlendFactor::SrcColor:     return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor:  return D3D11_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha:     return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:  return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestAlpha:    return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::InvDestAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DestColor:    return D3D11_BLEND_DEST_COLOR;
    case BlendFactor::InvDestColor: return D3D11_BLEND_INV_DEST_COLOR;
    default:                        return D3D11_BLEND_ONE;
    }
}

static D3D11_BLEND_OP ToD3D11(BlendOp op)
{
    switch (op) {
    case BlendOp::Add:         return D3D11_BLEND_OP_ADD;
    case BlendOp::Subtract:    return D3D11_BLEND_OP_SUBTRACT;
    case BlendOp::RevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:         return D3D11_BLEND_OP_MIN;
    case BlendOp::Max:         return D3D11_BLEND_OP_MAX;
    default:                   return D3D11_BLEND_OP_ADD;
    }
}

static DXGI_FORMAT VertexFormatToDXGI(VertexFormat f)
{
    switch (f) {
    case VertexFormat::Float1:     return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::Float2:     return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3:     return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4:     return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::UByte4Norm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:                       return DXGI_FORMAT_R32G32B32_FLOAT;
    }
}

// Shader ----------------------------------------------------------------------

bool Shader::CompileVS(Device& device, const char* source, const char* entryPoint, const char* profile)
{
    ComPtr<ID3DBlob> errors;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entryPoint, profile, flags, 0,
                            m_vsBytecode.GetAddressOf(), errors.GetAddressOf());

    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((const char*)errors->GetBufferPointer());
        return false;
    }

    return LoadVS(device, m_vsBytecode->GetBufferPointer(), m_vsBytecode->GetBufferSize());
}

bool Shader::CompilePS(Device& device, const char* source, const char* entryPoint, const char* profile)
{
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entryPoint, profile, flags, 0,
                            bytecode.GetAddressOf(), errors.GetAddressOf());

    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((const char*)errors->GetBufferPointer());
        return false;
    }

    return LoadPS(device, bytecode->GetBufferPointer(), bytecode->GetBufferSize());
}

void Shader::Destroy(Device& /*device*/) { m_vertexShader.Reset(); m_pixelShader.Reset(); m_inputLayout.Reset(); m_vsBytecode.Reset(); }

bool Shader::LoadVS(Device& device, const void* bytecode, size_t size)
{
    return SUCCEEDED(device.GetDevice()->CreateVertexShader(bytecode, size, nullptr, m_vertexShader.GetAddressOf()));
}

bool Shader::LoadPS(Device& device, const void* bytecode, size_t size)
{
    return SUCCEEDED(device.GetDevice()->CreatePixelShader(bytecode, size, nullptr, m_pixelShader.GetAddressOf()));
}

bool Shader::CreateInputLayout(Device& device, const VertexAttribute* attributes, uint32_t count, uint32_t vertexStride)
{
    if (!m_vsBytecode)
        return false;

    // Convert RHI VertexAttribute array to D3D11 input element array
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        elements[i] = {};
        elements[i].SemanticName = attributes[i].semantic;
        elements[i].SemanticIndex = attributes[i].semanticIndex;
        elements[i].Format = VertexFormatToDXGI(attributes[i].format);
        elements[i].AlignedByteOffset = attributes[i].offset;
        elements[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    }

    return SUCCEEDED(device.GetDevice()->CreateInputLayout(
        elements.data(), count,
        m_vsBytecode->GetBufferPointer(), m_vsBytecode->GetBufferSize(),
        m_inputLayout.GetAddressOf()));
}

void Shader::Bind(Device& device) const
{
    auto* ctx = device.GetContext();
    ctx->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    ctx->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    if (m_inputLayout)
        ctx->IASetInputLayout(m_inputLayout.Get());
}

// RasterizerState -------------------------------------------------------------

bool RasterizerState::Create(Device& device, FillMode fill, CullMode cull, bool frontCCW, int depthBias, bool scissorEnable, float slopeScaledDepthBias)
{
    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = ToD3D11(fill);
    desc.CullMode = ToD3D11(cull);
    desc.FrontCounterClockwise = frontCCW ? TRUE : FALSE;
    desc.DepthBias = depthBias;
    desc.SlopeScaledDepthBias = slopeScaledDepthBias;
    desc.DepthClipEnable = TRUE;
    desc.ScissorEnable = scissorEnable ? TRUE : FALSE;

    return SUCCEEDED(device.GetDevice()->CreateRasterizerState(&desc, m_state.GetAddressOf()));
}

void RasterizerState::Bind(Device& device) const
{
    device.GetContext()->RSSetState(m_state.Get());
}

// BlendState ------------------------------------------------------------------

bool BlendState::CreateOpaque(Device& device)
{
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = FALSE;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    return SUCCEEDED(device.GetDevice()->CreateBlendState(&desc, m_state.GetAddressOf()));
}

bool BlendState::CreateAlphaBlend(Device& device)
{
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = TRUE;
    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    return SUCCEEDED(device.GetDevice()->CreateBlendState(&desc, m_state.GetAddressOf()));
}

bool BlendState::CreateAdditive(Device& device)
{
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = TRUE;
    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    return SUCCEEDED(device.GetDevice()->CreateBlendState(&desc, m_state.GetAddressOf()));
}

bool BlendState::CreateMultiplicative(Device& device)
{
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = TRUE;
    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
    desc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    return SUCCEEDED(device.GetDevice()->CreateBlendState(&desc, m_state.GetAddressOf()));
}

bool BlendState::CreateFromDesc(Device& device, const BlendDesc& rhi)
{
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = rhi.enable ? TRUE : FALSE;
    desc.RenderTarget[0].SrcBlend = ToD3D11Blend(rhi.srcColor);
    desc.RenderTarget[0].DestBlend = ToD3D11Blend(rhi.destColor);
    desc.RenderTarget[0].BlendOp = ToD3D11(rhi.colorOp);
    desc.RenderTarget[0].SrcBlendAlpha = ToD3D11Blend(rhi.srcAlpha);
    desc.RenderTarget[0].DestBlendAlpha = ToD3D11Blend(rhi.destAlpha);
    desc.RenderTarget[0].BlendOpAlpha = ToD3D11(rhi.alphaOp);
    desc.RenderTarget[0].RenderTargetWriteMask = rhi.writeMask;

    return SUCCEEDED(device.GetDevice()->CreateBlendState(&desc, m_state.GetAddressOf()));
}

void BlendState::Bind(Device& device) const
{
    float blendFactor[4] = { 0, 0, 0, 0 };
    device.GetContext()->OMSetBlendState(m_state.Get(), blendFactor, 0xFFFFFFFF);
}

// DepthStencilState -----------------------------------------------------------

bool DepthStencilState::Create(Device& device, bool depthEnable, bool depthWrite, CompareFunc compareFunc)
{
    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = depthEnable ? TRUE : FALSE;
    desc.DepthWriteMask = depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = ToD3D11(compareFunc);

    return SUCCEEDED(device.GetDevice()->CreateDepthStencilState(&desc, m_state.GetAddressOf()));
}

void DepthStencilState::Bind(Device& device, uint8_t stencilRef) const
{
    device.GetContext()->OMSetDepthStencilState(m_state.Get(), stencilRef);
}

// SamplerState ----------------------------------------------------------------

void SamplerState::Destroy(Device& /*device*/) { m_state.Reset(); }

bool SamplerState::Create(Device& device, Filter filter, AddressMode addressMode)
{
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = ToD3D11(filter);
    desc.AddressU = ToD3D11(addressMode);
    desc.AddressV = ToD3D11(addressMode);
    desc.AddressW = ToD3D11(addressMode);
    desc.MaxAnisotropy = (filter == Filter::Anisotropic) ? 16 : 1;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    return SUCCEEDED(device.GetDevice()->CreateSamplerState(&desc, m_state.GetAddressOf()));
}

bool SamplerState::CreateComparison(Device& device, Filter filter, AddressMode addressMode)
{
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = ToD3D11(filter);
    desc.AddressU = ToD3D11(addressMode);
    desc.AddressV = ToD3D11(addressMode);
    desc.AddressW = ToD3D11(addressMode);
    desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    desc.BorderColor[0] = desc.BorderColor[1] = desc.BorderColor[2] = desc.BorderColor[3] = 1.0f;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(device.GetDevice()->CreateSamplerState(&desc, m_state.GetAddressOf()));
}

void SamplerState::BindVS(Device& device, uint32_t slot) const
{
    device.GetContext()->VSSetSamplers(slot, 1, m_state.GetAddressOf());
}

void SamplerState::BindPS(Device& device, uint32_t slot) const
{
    device.GetContext()->PSSetSamplers(slot, 1, m_state.GetAddressOf());
}

// ComputeShader ---------------------------------------------------------------

bool ComputeShader::Compile(Device& device, const char* source, const char* entryPoint, const char* profile)
{
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entryPoint, profile, flags, 0,
                            bytecode.GetAddressOf(), errors.GetAddressOf());

    if (FAILED(hr))
    {
        if (errors)
            OutputDebugStringA((const char*)errors->GetBufferPointer());
        return false;
    }

    return SUCCEEDED(device.GetDevice()->CreateComputeShader(
        bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
        nullptr, m_shader.GetAddressOf()));
}

void ComputeShader::Bind(Device& device) const
{
    device.GetContext()->CSSetShader(m_shader.Get(), nullptr, 0);
}

void ComputeShader::Dispatch(Device& device, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) const
{
    device.GetContext()->Dispatch(groupsX, groupsY, groupsZ);
}

} // namespace Render

#endif // BUILD_WITH_D3D11
