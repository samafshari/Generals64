#ifdef BUILD_WITH_VULKAN

#include "VulkanPipeline.h"
#include "../Device.h"
#include "../Shader.h"
#include <cstdio>

namespace Render
{

static VkBlendFactor ToVkBlend(BlendFactor f)
{
    switch (f) {
    case BlendFactor::Zero:         return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:          return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor:     return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::InvSrcColor:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:     return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:  return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestAlpha:    return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::InvDestAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DestColor:    return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::InvDestColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    default:                        return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp ToVkBlendOp(BlendOp op)
{
    switch (op) {
    case BlendOp::Subtract:    return VK_BLEND_OP_SUBTRACT;
    case BlendOp::RevSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:         return VK_BLEND_OP_MIN;
    case BlendOp::Max:         return VK_BLEND_OP_MAX;
    default:                   return VK_BLEND_OP_ADD;
    }
}

static VkCompareOp ToVkCompareOp(CompareFunc f)
{
    switch (f) {
    case CompareFunc::Never:        return VK_COMPARE_OP_NEVER;
    case CompareFunc::Less:         return VK_COMPARE_OP_LESS;
    case CompareFunc::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareFunc::Equal:        return VK_COMPARE_OP_EQUAL;
    case CompareFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareFunc::Greater:      return VK_COMPARE_OP_GREATER;
    case CompareFunc::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case CompareFunc::Always:       return VK_COMPARE_OP_ALWAYS;
    default:                        return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

static VkFormat VertexFormatToVk(VertexFormat f)
{
    switch (f) {
    case VertexFormat::Float1:     return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:     return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:     return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:     return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
    default:                       return VK_FORMAT_R32G32B32_SFLOAT;
    }
}

bool VulkanPipelineManager::Init(VkDevice device, VkPhysicalDevice /*physDevice*/, VkRenderPass renderPass)
{
    m_renderPass = renderPass;

    // --- Descriptor set layout ---
    // Matches HLSL register layout:
    // Register layout with DXC shifts: -fvk-b-shift 0 -fvk-t-shift 10 -fvk-s-shift 20
    // b0-b2 → bindings 0-2 (UBO)
    // t0-t4 → bindings 10-14 (sampled image)
    // s0-s2 → bindings 20-22 (sampler)
    VkDescriptorSetLayoutBinding bindings[] = {
        // Uniform buffers (b0, b1, b2)
        {  0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        {  1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        {  2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        // Sampled images (t0-t4)
        { 10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 12, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 13, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 14, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        // Samplers (s0-s2)
        { 20, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 21, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 22, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        // Storage buffer (u0 → binding 30) for GPU particle StructuredBuffer
        { 30, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 12;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        return false;

    // --- Pipeline layout ---
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        return false;

    // --- Descriptor pool ---
    // Enough for ~1000 draw calls per frame (generous for this game)
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5000 },
        { VK_DESCRIPTOR_TYPE_SAMPLER, 3000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    };

    // Create per-frame descriptor pools so we can reset one while the other is in flight
    for (uint32_t i = 0; i < MAX_FRAMES; i++)
    {
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1000;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = poolSizes;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPools[i]) != VK_SUCCESS)
            return false;
    }

    return true;
}

void VulkanPipelineManager::Shutdown(VkDevice device)
{
    for (auto& [key, pipeline] : m_pipelineCache)
        vkDestroyPipeline(device, pipeline, nullptr);
    m_pipelineCache.clear();

    for (uint32_t i = 0; i < MAX_FRAMES; i++)
        if (m_descriptorPools[i]) vkDestroyDescriptorPool(device, m_descriptorPools[i], nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
}

VkDescriptorSet VulkanPipelineManager::AllocateDescriptorSet(VkDevice device)
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPools[m_currentPoolIndex];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return set;
}

void VulkanPipelineManager::ResetDescriptorPool(VkDevice device)
{
    // Advance to next pool — the previous frame's pool is safe because
    // we waited for its fence before calling this.
    m_currentPoolIndex = (m_currentPoolIndex + 1) % MAX_FRAMES;
    vkResetDescriptorPool(device, m_descriptorPools[m_currentPoolIndex], 0);
}

VkPipeline VulkanPipelineManager::GetOrCreatePipeline(
    VkDevice device,
    const Shader* shader,
    const RasterizerState* raster,
    const BlendState* blend,
    const DepthStencilState* depth,
    Topology topology,
    VkRenderPass renderPass)
{
    // Use the provided render pass, or fall back to the default main render pass
    if (renderPass == VK_NULL_HANDLE)
        renderPass = m_renderPass;

    PipelineKey key = { shader, raster, blend, depth, topology, renderPass };
    auto it = m_pipelineCache.find(key);
    if (it != m_pipelineCache.end())
        return it->second;

    VkPipeline pipeline = CreateGraphicsPipeline(device, shader, raster, blend, depth, topology, renderPass);
    if (pipeline != VK_NULL_HANDLE)
        m_pipelineCache[key] = pipeline;
    return pipeline;
}

VkPipeline VulkanPipelineManager::CreateGraphicsPipeline(
    VkDevice device,
    const Shader* shader,
    const RasterizerState* raster,
    const BlendState* blend,
    const DepthStencilState* depth,
    Topology topology,
    VkRenderPass renderPass)
{
    if (!shader || !shader->m_vkVertModule || !shader->m_vkFragModule)
        return VK_NULL_HANDLE;

    // --- Shader stages ---
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader->m_vkVertModule;
    stages[0].pName = shader->m_vkVertEntry.c_str(); // DXC preserves original HLSL entry point name

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader->m_vkFragModule;
    stages[1].pName = shader->m_vkFragEntry.c_str();

    // --- Vertex input ---
    VkVertexInputBindingDescription bindingDesc = {};
    bindingDesc.binding = 0;
    bindingDesc.stride = shader->m_vkVertexStride;
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrDescs(shader->m_vkVertexAttributes.size());
    for (size_t i = 0; i < shader->m_vkVertexAttributes.size(); i++)
    {
        attrDescs[i].binding = 0;
        attrDescs[i].location = (uint32_t)i;
        attrDescs[i].format = VertexFormatToVk(shader->m_vkVertexAttributes[i].format);
        attrDescs[i].offset = shader->m_vkVertexAttributes[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (!shader->m_vkVertexAttributes.empty())
    {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = (uint32_t)attrDescs.size();
        vertexInput.pVertexAttributeDescriptions = attrDescs.data();
    }

    // --- Input assembly ---
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    switch (topology) {
    case Topology::TriangleStrip: inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
    case Topology::LineList:      inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
    default:                      inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
    }

    // --- Viewport/scissor (dynamic state) ---
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // --- Rasterizer ---
    VkPipelineRasterizationStateCreateInfo rasterInfo = {};
    rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterInfo.depthClampEnable = VK_FALSE;
    rasterInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterInfo.lineWidth = 1.0f;
    if (raster)
    {
        rasterInfo.polygonMode = (raster->m_vkFill == FillMode::Wireframe) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        switch (raster->m_vkCull) {
        case CullMode::None:  rasterInfo.cullMode = VK_CULL_MODE_NONE; break;
        case CullMode::Front: rasterInfo.cullMode = VK_CULL_MODE_FRONT_BIT; break;
        default:              rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT; break;
        }
        // Negative viewport height (Y-flip) reverses winding order, so invert the front face
        rasterInfo.frontFace = raster->m_vkFrontCCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterInfo.depthBiasEnable = (raster->m_vkDepthBias != 0) ? VK_TRUE : VK_FALSE;
        rasterInfo.depthBiasConstantFactor = (float)raster->m_vkDepthBias;
    }
    else
    {
        rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterInfo.frontFace = VK_FRONT_FACE_CLOCKWISE; // Inverted for negative viewport Y-flip
    }

    // --- Multisampling ---
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // --- Depth/stencil ---
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    if (depth)
    {
        depthStencil.depthTestEnable = depth->m_vkDepthEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = depth->m_vkDepthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = ToVkCompareOp(depth->m_vkCompareFunc);
    }
    else
    {
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }

    // --- Color blending ---
    VkPipelineColorBlendAttachmentState colorBlendAtt = {};
    colorBlendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (blend)
    {
        colorBlendAtt.blendEnable = blend->m_vkDesc.enable ? VK_TRUE : VK_FALSE;
        colorBlendAtt.srcColorBlendFactor = ToVkBlend(blend->m_vkDesc.srcColor);
        colorBlendAtt.dstColorBlendFactor = ToVkBlend(blend->m_vkDesc.destColor);
        colorBlendAtt.colorBlendOp = ToVkBlendOp(blend->m_vkDesc.colorOp);
        colorBlendAtt.srcAlphaBlendFactor = ToVkBlend(blend->m_vkDesc.srcAlpha);
        colorBlendAtt.dstAlphaBlendFactor = ToVkBlend(blend->m_vkDesc.destAlpha);
        colorBlendAtt.alphaBlendOp = ToVkBlendOp(blend->m_vkDesc.alphaOp);
        colorBlendAtt.colorWriteMask = blend->m_vkDesc.writeMask;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAtt;

    // --- Dynamic state (viewport + scissor) ---
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // --- Create pipeline ---
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterInfo;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        fprintf(stderr, "[Vulkan] Failed to create graphics pipeline (result=%d, VS=%s, PS=%s)\n",
                result, stages[0].pName, stages[1].pName);
        return VK_NULL_HANDLE;
    }

    static int s_pipelineLogCount = 0;
    if (s_pipelineLogCount < 200)
    {
        fprintf(stderr, "[Vulkan] Pipeline OK: VS=%s PS=%s blend=%d writeMask=0x%X depth=%d cull=%d stride=%u attrs=%u\n",
            stages[0].pName, stages[1].pName,
            (int)colorBlendAtt.blendEnable, (int)colorBlendAtt.colorWriteMask,
            (int)depthStencil.depthTestEnable, (int)rasterInfo.cullMode,
            bindingDesc.stride, (uint32_t)shader->m_vkVertexAttributes.size());
        s_pipelineLogCount++;
    }
    return pipeline;
}

} // namespace Render

#endif // BUILD_WITH_VULKAN
