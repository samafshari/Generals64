#include "Buffer.h"
#include "Device.h"

#ifdef BUILD_WITH_D3D11

namespace Render
{

// VertexBuffer ----------------------------------------------------------------

void VertexBuffer::Destroy(Device& /*device*/) { m_buffer.Reset(); m_vertexCount = 0; }

bool VertexBuffer::Create(Device& device, const void* data, uint32_t vertexCount, uint32_t stride, bool dynamic)
{
    m_vertexCount = vertexCount;
    m_stride = stride;
    m_dynamic = dynamic;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = vertexCount * stride;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    return SUCCEEDED(device.GetDevice()->CreateBuffer(&desc, data ? &initData : nullptr, m_buffer.GetAddressOf()));
}

void VertexBuffer::Update(Device& device, const void* data, uint32_t size)
{
    if (m_dynamic)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(device.GetContext()->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, data, size);
            device.GetContext()->Unmap(m_buffer.Get(), 0);
        }
    }
    else
    {
        device.GetContext()->UpdateSubresource(m_buffer.Get(), 0, nullptr, data, 0, 0);
    }
}

void VertexBuffer::Bind(Device& device, uint32_t slot) const
{
    UINT offset = 0;
    device.GetContext()->IASetVertexBuffers(slot, 1, m_buffer.GetAddressOf(), &m_stride, &offset);
}

// IndexBuffer -----------------------------------------------------------------

void IndexBuffer::Destroy(Device& /*device*/) { m_buffer.Reset(); m_indexCount = 0; }

bool IndexBuffer::Create(Device& device, const uint16_t* data, uint32_t indexCount, bool dynamic)
{
    m_indexCount = indexCount;
    m_is32Bit = false;
    m_dynamic = dynamic;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = indexCount * sizeof(uint16_t);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    return SUCCEEDED(device.GetDevice()->CreateBuffer(&desc, data ? &initData : nullptr, m_buffer.GetAddressOf()));
}

bool IndexBuffer::Create32(Device& device, const uint32_t* data, uint32_t indexCount, bool dynamic)
{
    m_indexCount = indexCount;
    m_is32Bit = true;
    m_dynamic = dynamic;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = indexCount * sizeof(uint32_t);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;

    return SUCCEEDED(device.GetDevice()->CreateBuffer(&desc, data ? &initData : nullptr, m_buffer.GetAddressOf()));
}

void IndexBuffer::Update(Device& device, const void* data, uint32_t size)
{
    if (m_dynamic)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(device.GetContext()->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, data, size);
            device.GetContext()->Unmap(m_buffer.Get(), 0);
        }
    }
    else
    {
        device.GetContext()->UpdateSubresource(m_buffer.Get(), 0, nullptr, data, 0, 0);
    }
}

void IndexBuffer::Bind(Device& device) const
{
    DXGI_FORMAT fmt = m_is32Bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    device.GetContext()->IASetIndexBuffer(m_buffer.Get(), fmt, 0);
}

// ConstantBuffer --------------------------------------------------------------

void ConstantBuffer::Destroy(Device& /*device*/) { m_buffer.Reset(); m_size = 0; }

bool ConstantBuffer::Create(Device& device, uint32_t sizeBytes)
{
    // Constant buffers must be 16-byte aligned
    m_size = (sizeBytes + 15) & ~15;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = m_size;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return SUCCEEDED(device.GetDevice()->CreateBuffer(&desc, nullptr, m_buffer.GetAddressOf()));
}

void ConstantBuffer::Update(Device& device, const void* data, uint32_t sizeBytes)
{
    if (!m_buffer)
        return;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(device.GetContext()->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, data, sizeBytes);
        device.GetContext()->Unmap(m_buffer.Get(), 0);
    }
}

void ConstantBuffer::BindVS(Device& device, uint32_t slot) const
{
    device.GetContext()->VSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
}

void ConstantBuffer::BindPS(Device& device, uint32_t slot) const
{
    device.GetContext()->PSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
}

// GPUBuffer (StructuredBuffer) ------------------------------------------------

void GPUBuffer::Destroy(Device& /*device*/) { m_buffer.Reset(); m_srv.Reset(); m_elementCount = 0; }

bool GPUBuffer::Create(Device& device, uint32_t elementSize, uint32_t maxElements, const void* initialData)
{
    m_elementSize = elementSize;
    m_maxElements = maxElements;
    m_elementCount = 0;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = elementSize * maxElements;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = elementSize;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = initialData;

    HRESULT hr = device.GetDevice()->CreateBuffer(&desc, initialData ? &initData : nullptr, m_buffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = maxElements;

    hr = device.GetDevice()->CreateShaderResourceView(m_buffer.Get(), &srvDesc, m_srv.GetAddressOf());
    return SUCCEEDED(hr);
}

void GPUBuffer::Update(Device& device, const void* data, uint32_t elementCount)
{
    if (!m_buffer || elementCount > m_maxElements)
        return;
    m_elementCount = elementCount;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = device.GetContext()->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        memcpy(mapped.pData, data, elementCount * m_elementSize);
        device.GetContext()->Unmap(m_buffer.Get(), 0);
    }
}

void GPUBuffer::BindVS(Device& device, uint32_t slot) const
{
    device.GetContext()->VSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}

void GPUBuffer::BindPS(Device& device, uint32_t slot) const
{
    device.GetContext()->PSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}

// ComputeBuffer ---------------------------------------------------------------

void ComputeBuffer::Destroy(Device& /*device*/) { m_buffer.Reset(); m_staging.Reset(); m_srv.Reset(); m_uav.Reset(); m_elementCount = 0; }

bool ComputeBuffer::Create(Device& device, uint32_t elementSize, uint32_t maxElements, bool hasUAV)
{
    m_elementSize = elementSize;
    m_maxElements = maxElements;
    m_elementCount = 0;

    // GPU buffer (DEFAULT usage — not CPU writable, must use UpdateSubresource or staging)
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = elementSize * maxElements;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (hasUAV)
        desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = elementSize;

    HRESULT hr = device.GetDevice()->CreateBuffer(&desc, nullptr, m_buffer.GetAddressOf());
    if (FAILED(hr))
        return false;

    // SRV for read access
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = maxElements;

    hr = device.GetDevice()->CreateShaderResourceView(m_buffer.Get(), &srvDesc, m_srv.GetAddressOf());
    if (FAILED(hr))
        return false;

    // UAV for write access from compute shaders
    if (hasUAV)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = maxElements;

        hr = device.GetDevice()->CreateUnorderedAccessView(m_buffer.Get(), &uavDesc, m_uav.GetAddressOf());
        if (FAILED(hr))
            return false;
    }

    // Staging buffer for GPU->CPU readback
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = elementSize * maxElements;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    stagingDesc.StructureByteStride = elementSize;

    hr = device.GetDevice()->CreateBuffer(&stagingDesc, nullptr, m_staging.GetAddressOf());
    return SUCCEEDED(hr);
}

void ComputeBuffer::Upload(Device& device, const void* data, uint32_t elementCount)
{
    if (!m_buffer || elementCount > m_maxElements)
        return;
    m_elementCount = elementCount;

    D3D11_BOX box = {};
    box.left = 0;
    box.right = elementCount * m_elementSize;
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;

    device.GetContext()->UpdateSubresource(m_buffer.Get(), 0, &box, data, 0, 0);
}

void ComputeBuffer::Readback(Device& device, void* dst, uint32_t sizeBytes)
{
    if (!m_buffer || !m_staging)
        return;

    // Copy GPU buffer to staging
    device.GetContext()->CopyResource(m_staging.Get(), m_buffer.Get());

    // Map staging buffer for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = device.GetContext()->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        memcpy(dst, mapped.pData, sizeBytes);
        device.GetContext()->Unmap(m_staging.Get(), 0);
    }
}

void ComputeBuffer::BindSRV(Device& device, uint32_t slot) const
{
    device.GetContext()->CSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}

void ComputeBuffer::BindUAV(Device& device, uint32_t slot) const
{
    UINT initialCount = (UINT)-1;
    device.GetContext()->CSSetUnorderedAccessViews(slot, 1, m_uav.GetAddressOf(), &initialCount);
}

void ComputeBuffer::UnbindUAV(Device& device, uint32_t slot) const
{
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    UINT initialCount = (UINT)-1;
    device.GetContext()->CSSetUnorderedAccessViews(slot, 1, &nullUAV, &initialCount);
}

} // namespace Render

#endif // BUILD_WITH_D3D11
