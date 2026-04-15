#include "ShaderCompiler.h"
#include <cstdio>

namespace Render
{

std::vector<uint8_t> LoadShaderBytecode(const char* filePath)
{
    FILE* f = fopen(filePath, "rb");
    if (!f)
        return {};

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0)
    {
        fclose(f);
        return {};
    }

    std::vector<uint8_t> data(size);
    size_t read = fread(data.data(), 1, size, f);
    fclose(f);

    if ((long)read != size)
        return {};

    return data;
}

bool ShaderBytecodeExists(const char* filePath)
{
    FILE* f = fopen(filePath, "rb");
    if (f)
    {
        fclose(f);
        return true;
    }
    return false;
}

} // namespace Render
