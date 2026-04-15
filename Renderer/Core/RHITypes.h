#pragma once

// Backend-agnostic rendering types.
// All Core/ public APIs use these instead of D3D11/Vulkan-specific enums.
// Backend implementations map to the native enum internally.

#include <cstdint>

namespace Render
{

// --- Rasterizer ---

enum class FillMode
{
    Solid,
    Wireframe
};

enum class CullMode
{
    None,
    Front,
    Back
};

// --- Input assembler ---

enum class Topology
{
    TriangleList,
    TriangleStrip,
    LineList
};

// --- Depth / stencil ---

enum class CompareFunc
{
    Never,
    Less,
    LessEqual,
    Equal,
    GreaterEqual,
    Greater,
    NotEqual,
    Always
};

// --- Sampler ---

enum class Filter
{
    MinMagMipPoint,
    MinMagMipLinear,
    Anisotropic,
    ComparisonMinMagMipLinear
};

enum class AddressMode
{
    Wrap,
    Clamp,
    Border,
    Mirror
};

// --- Pixel / texture format ---

enum class PixelFormat
{
    Unknown,
    RGBA8_UNORM,
    BGRA8_UNORM,
    R16_UINT,
    R32_UINT,
    R32_FLOAT,
    R24G8_TYPELESS,
    D24_UNORM_S8_UINT,
    R24_UNORM_X8_TYPELESS,
    R32_TYPELESS,
    D32_FLOAT,
    BC2_UNORM,
    BC3_UNORM,
    B5G6R5_UNORM,
};

// --- Blend ---

enum class BlendFactor
{
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DestAlpha,
    InvDestAlpha,
    DestColor,
    InvDestColor
};

enum class BlendOp
{
    Add,
    Subtract,
    RevSubtract,
    Min,
    Max
};

// Describes a single render-target blend configuration.
struct BlendDesc
{
    bool enable = false;
    BlendFactor srcColor  = BlendFactor::One;
    BlendFactor destColor = BlendFactor::Zero;
    BlendOp     colorOp   = BlendOp::Add;
    BlendFactor srcAlpha  = BlendFactor::One;
    BlendFactor destAlpha = BlendFactor::Zero;
    BlendOp     alphaOp   = BlendOp::Add;
    uint8_t     writeMask = 0x0F; // RGBA
};

// --- Vertex input layout ---

enum class VertexFormat
{
    Float1,
    Float2,
    Float3,
    Float4,
    UByte4Norm,  // R8G8B8A8_UNORM (packed color)
};

struct VertexAttribute
{
    const char* semantic;       // "POSITION", "NORMAL", etc.
    uint32_t    semanticIndex;
    VertexFormat format;
    uint32_t    offset;
};

} // namespace Render
