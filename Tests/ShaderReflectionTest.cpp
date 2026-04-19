// Shader reflection test — verifies the compiled HLSL cbuffer layout matches
// the C++ struct. Compiles the real Shader3D.hlsl (via D3DCompile against the
// generated .hlsl.inc string) and walks the reflection API to check:
//   1. FrameConstants cbuffer is present at slot b0
//   2. sunViewProjection is at byte offset 368
//   3. sunViewProjection is tagged as ROW-major
//   4. Its type is a 4x4 float matrix
//
// The previous symptom (shadow plane moves with camera) matched the exact
// math of a transposed matrix read. If this test shows the matrix is stored
// column-major in the compiled bytecode — despite the row_major qualifier in
// the source — that is the live bug and D3DCOMPILE_PACK_MATRIX_ROW_MAJOR is
// the fix.
//
// Build:
//   cl /std:c++17 /EHsc /nologo Tests/ShaderReflectionTest.cpp d3dcompiler.lib
// Run:
//   ./ShaderReflectionTest.exe

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

// Pull in the generated Shader3D source string. Same header the runtime uses.
namespace Render {
static const char* g_shader3D =
#include "../Renderer/Shaders/VulkanHLSL/Shader3D.hlsl.inc"
;
}

struct Result { int total = 0; int failed = 0; };
static Result g_r;

#define CHECK_(cond) do {                                                       \
    ++g_r.total;                                                                \
    if (!(cond)) { ++g_r.failed;                                                \
        std::printf("  FAIL  %s\n", #cond); }                                   \
    else std::printf("  ok    %s\n", #cond);                                    \
} while (0)

// Compile one VS/PS entry point with the flags we pick and return its
// reflection interface. Returns nullptr + prints error on failure.
static ComPtr<ID3D11ShaderReflection> CompileAndReflect(
    const char* source, const char* entry, const char* profile, UINT flags)
{
    ComPtr<ID3DBlob> bytecode, errors;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                            entry, profile, flags, 0,
                            bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        std::printf("  D3DCompile failed (0x%08x)\n", (unsigned)hr);
        if (errors) std::printf("  %s\n", (const char*)errors->GetBufferPointer());
        return nullptr;
    }
    ComPtr<ID3D11ShaderReflection> refl;
    hr = D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                    IID_ID3D11ShaderReflection, (void**)refl.GetAddressOf());
    if (FAILED(hr)) { std::printf("  D3DReflect failed\n"); return nullptr; }
    return refl;
}

// Look for `sunViewProjection` inside FrameConstants cbuffer and report its
// placement + class (row vs column major).
static void InspectFrameConstants(ID3D11ShaderReflection* refl, const char* label)
{
    std::printf("\n--- %s ---\n", label);

    ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("FrameConstants");
    if (!cb) { std::printf("  NO 'FrameConstants' cbuffer\n"); ++g_r.failed; ++g_r.total; return; }

    D3D11_SHADER_BUFFER_DESC cbDesc{};
    cb->GetDesc(&cbDesc);
    std::printf("  cbuffer '%s' size=%u variables=%u\n",
                cbDesc.Name, cbDesc.Size, cbDesc.Variables);

    for (UINT i = 0; i < cbDesc.Variables; ++i) {
        auto* var = cb->GetVariableByIndex(i);
        D3D11_SHADER_VARIABLE_DESC vd{};
        var->GetDesc(&vd);
        auto* type = var->GetType();
        D3D11_SHADER_TYPE_DESC td{};
        type->GetDesc(&td);
        const char* cls =
            td.Class == D3D_SVC_MATRIX_ROWS   ? "MATRIX_ROWS" :
            td.Class == D3D_SVC_MATRIX_COLUMNS? "MATRIX_COLS" :
            td.Class == D3D_SVC_VECTOR       ? "VECTOR" :
            td.Class == D3D_SVC_SCALAR       ? "SCALAR" :
            "OTHER";
        std::printf("  [%02u] %-30s  offset=%4u size=%4u  class=%-12s  %ux%u\n",
                    i, vd.Name, vd.StartOffset, vd.Size, cls, td.Rows, td.Columns);
    }

    // Targeted checks.
    auto* sunVP = cb->GetVariableByName("sunViewProjection");
    D3D11_SHADER_VARIABLE_DESC sunDesc{};
    if (sunVP) sunVP->GetDesc(&sunDesc);

    D3D11_SHADER_TYPE_DESC sunType{};
    if (sunVP) sunVP->GetType()->GetDesc(&sunType);

    CHECK_(sunVP != nullptr);
    CHECK_(sunDesc.StartOffset == 368);
    CHECK_(sunType.Rows == 4 && sunType.Columns == 4);
    CHECK_(sunType.Class == D3D_SVC_MATRIX_ROWS); // ROW-major in compiled bytecode
}

int main()
{
    // Case A — current production flags (implicit column-major default).
    // The cbuffer should still pack sunViewProjection ROW-major because the
    // HLSL variable is qualified `row_major`. If this fails, per-variable
    // row_major qualifier isn't being honored and we need the global flag.
    {
        UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
        auto r = CompileAndReflect(Render::g_shader3D, "PSMain", "ps_5_0", flags);
        if (r) InspectFrameConstants(r.Get(), "ps_5_0 PSMain, DEFAULT packing");
    }

    // Case B — with the global row-major flag forced on.
    {
        UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
        auto r = CompileAndReflect(Render::g_shader3D, "PSMain", "ps_5_0", flags);
        if (r) InspectFrameConstants(r.Get(), "ps_5_0 PSMain, ROW_MAJOR global flag");
    }

    // Case C — Vertex shader of Shader3D. Same cbuffer shared, different stage.
    {
        UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
        auto r = CompileAndReflect(Render::g_shader3D, "VSMain", "vs_5_0", flags);
        if (r) InspectFrameConstants(r.Get(), "vs_5_0 VSMain, DEFAULT packing");
    }

    std::printf("\n%d / %d passed, %d failed\n",
                g_r.total - g_r.failed, g_r.total, g_r.failed);
    return g_r.failed;
}
