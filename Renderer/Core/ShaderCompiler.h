#pragma once

#include <vector>
#include <cstdint>

namespace Render
{

// Load precompiled shader bytecode from a file.
// Returns the raw bytes (DXBC for D3D11, SPIR-V for Vulkan).
// Returns an empty vector on failure.
std::vector<uint8_t> LoadShaderBytecode(const char* filePath);

// Check if a shader bytecode file exists.
bool ShaderBytecodeExists(const char* filePath);

} // namespace Render
