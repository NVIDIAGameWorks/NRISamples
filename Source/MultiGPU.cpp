/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRIFramework.h"
#include "Extensions/NRIRayTracing.h"

#include <array>

struct Box
{
    std::vector<float> positions;
    std::vector<float> texcoords;
    std::vector<uint16_t> indices;
};

void SetBoxGeometry(uint32_t subdivisions, float boxHalfSize, Box& box);

constexpr uint32_t BOX_NUM = 1000;
constexpr uint32_t COMMAND_BUFFER_NUM = 2;

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{};

struct Frame
{
    nri::CommandAllocator* commandAllocator;
    std::array<nri::CommandBuffer*, COMMAND_BUFFER_NUM> commandBuffers;
};

class Sample : public SampleBase
{
public:
    Sample()
    {}

    ~Sample();

private:
    bool Initialize(nri::GraphicsAPI graphicsAPI) override;
    void PrepareFrame(uint32_t frameIndex) override;
    void RenderFrame(uint32_t frameIndex) override;

    void CreateSwapChain(nri::Format& swapChainFormat);
    void CreateCommandBuffers();
    void CreatePipeline(nri::Format swapChainFormat);
    void CreateGeometry();
    void CreateMainFrameBuffer(nri::Format swapChainFormat);
    void CreateDescriptorSet();
    void SetupProjViewMatrix(float4x4& projViewMatrix);
    void RecordGraphics(nri::CommandBuffer& commandBuffer, uint32_t physicalNodeIndex);
    void CopyToSwapChainTexture(nri::CommandBuffer& commandBuffer, uint32_t renderingNodeIndex, uint32_t presentingNodeIndex);

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::Fence* m_FrameFence = nullptr;
    nri::DescriptorSet* m_DescriptorSet = nullptr;
    nri::Descriptor* m_TransformBufferView = nullptr;
    nri::Descriptor* m_ColorTextureView = nullptr;
    nri::Descriptor* m_DepthTextureView = nullptr;
    nri::Pipeline* m_Pipeline = nullptr;
    nri::PipelineLayout* m_PipelineLayout = nullptr;
    nri::Buffer* m_VertexBuffer = nullptr;
    nri::Buffer* m_IndexBuffer = nullptr;
    nri::Buffer* m_TransformBuffer = nullptr;
    nri::Texture* m_DepthTexture = nullptr;
    nri::Texture* m_ColorTexture = nullptr;
    nri::DescriptorPool* m_DescriptorPool = nullptr;
    nri::Format m_DepthFormat = nri::Format::UNKNOWN;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
    std::vector<nri::Fence*> m_QueueFences;

    uint32_t m_NodeNum = 0;
    uint32_t m_BoxIndexNum = 0;
    bool m_IsMGPUEnabled = true;

    const BackBuffer* m_BackBuffer = nullptr;
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::Memory*> m_MemoryAllocations;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (Frame& frame : m_Frames)
    {
        for (nri::CommandBuffer* commandBuffer : frame.commandBuffers)
            NRI.DestroyCommandBuffer(*commandBuffer);

        NRI.DestroyCommandAllocator(*frame.commandAllocator);
    }

    NRI.DestroyDescriptor(*m_ColorTextureView);
    NRI.DestroyDescriptor(*m_DepthTextureView);
    NRI.DestroyDescriptor(*m_TransformBufferView);

    for (nri::Fence* fence : m_QueueFences)
        NRI.DestroyFence(*fence);

    NRI.DestroyTexture(*m_ColorTexture);
    NRI.DestroyTexture(*m_DepthTexture);
    NRI.DestroyBuffer(*m_VertexBuffer);
    NRI.DestroyBuffer(*m_IndexBuffer);
    NRI.DestroyBuffer(*m_TransformBuffer);

    NRI.DestroyPipeline(*m_Pipeline);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);

    NRI.DestroyDescriptorPool(*m_DescriptorPool);

    NRI.DestroyFence(*m_FrameFence);
    NRI.DestroySwapChain(*m_SwapChain);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    DestroyUserInterface();

    nri::nriDestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    nri::AdapterDesc bestAdapterDesc = {};
    uint32_t adapterDescsNum = 1;
    NRI_ABORT_ON_FAILURE(nri::nriEnumerateAdapters(&bestAdapterDesc, adapterDescsNum));

    nri::DeviceCreationDesc deviceCreationDesc = { };
    deviceCreationDesc.adapterDesc = &bestAdapterDesc;
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableAPIValidation = m_DebugAPI;
    deviceCreationDesc.enableNRIValidation = m_DebugNRI;
    deviceCreationDesc.enableMGPU = true;
    deviceCreationDesc.D3D11CommandBufferEmulation = D3D11_COMMANDBUFFER_EMULATION;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    deviceCreationDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    NRI_ABORT_ON_FAILURE(nri::nriCreateDevice(deviceCreationDesc, m_Device));

    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI));

    NRI_ABORT_ON_FAILURE(NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue));
    NRI_ABORT_ON_FAILURE(NRI.CreateFence(*m_Device, 0, m_FrameFence));

    m_DepthFormat = nri::GetSupportedDepthFormat(NRI, *m_Device, 24, false);
    m_NodeNum = NRI.GetDeviceDesc(*m_Device).nodeNum;

    m_QueueFences.resize(m_NodeNum);
    for (size_t i = 0; i < m_QueueFences.size(); i++)
        NRI_ABORT_ON_FAILURE(NRI.CreateFence(*m_Device, 0, m_QueueFences[i]));

    CreateCommandBuffers();

    nri::Format swapChainFormat = nri::Format::UNKNOWN;
    CreateSwapChain(swapChainFormat);

    CreateMainFrameBuffer(swapChainFormat);
    CreatePipeline(swapChainFormat);
    CreateDescriptorSet();
    CreateGeometry();

    return CreateUserInterface(*m_Device, NRI, NRI, swapChainFormat);
}

void Sample::PrepareFrame(uint32_t)
{
    ImGui::Begin("Multi-GPU", nullptr, ImGuiWindowFlags_NoResize);
    {
        if (m_NodeNum == 1)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

        ImGui::Checkbox("Use multiple GPUs", &m_IsMGPUEnabled);

        if (m_NodeNum == 1)
        {
            ImGui::PopStyleVar();
            m_IsMGPUEnabled = false;
        }

        ImGui::Text("Physical device group size: %u", m_NodeNum);
        ImGui::Text("Frametime: %.2f ms", m_Timer.GetSmoothedFrameTime());
    }
    ImGui::End();
}

void Sample::RecordGraphics(nri::CommandBuffer& commandBuffer, uint32_t physicalNodeIndex)
{
    NRI.BeginCommandBuffer(commandBuffer, m_DescriptorPool, physicalNodeIndex);

    nri::TextureTransitionBarrierDesc textureTransition = { };
    textureTransition.texture = m_ColorTexture;
    textureTransition.prevAccess = nri::AccessBits::COPY_SOURCE;
    textureTransition.nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransition.prevLayout = nri::TextureLayout::GENERAL;
    textureTransition.nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    textureTransition.arraySize = 1;
    textureTransition.mipNum = 1;

    nri::TransitionBarrierDesc transitionBarriers = { };
    transitionBarriers.textures = &textureTransition;
    transitionBarriers.textureNum = 1;

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);

    nri::AttachmentsDesc attachmentsDesc = {};
    attachmentsDesc.colorNum = 1;
    attachmentsDesc.colors = &m_ColorTextureView;
    attachmentsDesc.depthStencil = m_DepthTextureView;

    NRI.CmdBeginRendering(commandBuffer, attachmentsDesc);
    {
        nri::ClearDesc clearDescs[2] = {};
        clearDescs[0].attachmentContentType = nri::AttachmentContentType::COLOR;
        clearDescs[1].attachmentContentType = nri::AttachmentContentType::DEPTH;
        clearDescs[1].value.depthStencil.depth = 1.0f;
        NRI.CmdClearAttachments(commandBuffer, clearDescs, helper::GetCountOf(clearDescs), nullptr, 0);

        const nri::Rect scissorRect = { 0, 0, (nri::Dim_t)GetWindowResolution().x, (nri::Dim_t)GetWindowResolution().y };
        const nri::Viewport viewport = { 0.0f, 0.0f, (float)scissorRect.width, (float)scissorRect.height, 0.0f, 1.0f };
        NRI.CmdSetViewports(commandBuffer, &viewport, 1);
        NRI.CmdSetScissors(commandBuffer, &scissorRect, 1);

        NRI.CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);
        NRI.CmdSetPipeline(commandBuffer, *m_Pipeline);
        NRI.CmdSetIndexBuffer(commandBuffer, *m_IndexBuffer, 0, nri::IndexType::UINT16);

        const uint64_t nullOffset = 0;
        NRI.CmdSetVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffer, &nullOffset);

        const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
        const uint32_t constantRangeSize = (uint32_t)helper::Align(sizeof(float4x4), deviceDesc.constantBufferOffsetAlignment);

        for (uint32_t i = 0; i < BOX_NUM; i++)
        {
            const uint32_t dynamicOffset = i * constantRangeSize;
            NRI.CmdSetDescriptorSet(commandBuffer, 0, *m_DescriptorSet, &dynamicOffset);
            NRI.CmdDrawIndexed(commandBuffer, m_BoxIndexNum, 1, 0, 0, 0);
        }
    }
    NRI.CmdEndRendering(commandBuffer);

    attachmentsDesc.depthStencil = nullptr;

    NRI.CmdBeginRendering(commandBuffer, attachmentsDesc);
    {
        RenderUserInterface(*m_Device, commandBuffer);
    }
    NRI.CmdEndRendering(commandBuffer);

    textureTransition.texture = m_ColorTexture;
    textureTransition.prevAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransition.nextAccess = nri::AccessBits::COPY_SOURCE;
    textureTransition.prevLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    textureTransition.nextLayout = nri::TextureLayout::GENERAL;
    textureTransition.arraySize = 1;
    textureTransition.mipNum = 1;

    NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);

    NRI.EndCommandBuffer(commandBuffer);
}

void Sample::CopyToSwapChainTexture(nri::CommandBuffer& commandBuffer, uint32_t renderingNodeIndex, uint32_t presentingNodeIndex)
{
    nri::TextureTransitionBarrierDesc initialTransition = { };
    initialTransition.texture = m_BackBuffer->texture;
    initialTransition.prevAccess = nri::AccessBits::UNKNOWN;
    initialTransition.nextAccess = nri::AccessBits::COPY_DESTINATION;
    initialTransition.prevLayout = nri::TextureLayout::UNKNOWN;
    initialTransition.nextLayout = nri::TextureLayout::GENERAL;
    initialTransition.arraySize = 1;
    initialTransition.mipNum = 1;

    nri::TextureTransitionBarrierDesc finalTransition = { };
    finalTransition.texture = m_BackBuffer->texture;
    finalTransition.prevAccess = nri::AccessBits::COPY_DESTINATION;
    finalTransition.nextAccess = nri::AccessBits::UNKNOWN;
    finalTransition.prevLayout = nri::TextureLayout::GENERAL;
    finalTransition.nextLayout = nri::TextureLayout::PRESENT;
    finalTransition.arraySize = 1;
    finalTransition.mipNum = 1;

    nri::TransitionBarrierDesc initialTransitionBarriers = { };
    initialTransitionBarriers.textures = &initialTransition;
    initialTransitionBarriers.textureNum = 1;

    nri::TransitionBarrierDesc finalTransitionBarriers = { };
    finalTransitionBarriers.textures = &finalTransition;
    finalTransitionBarriers.textureNum = 1;

    NRI.BeginCommandBuffer(commandBuffer, nullptr, presentingNodeIndex);
    NRI.CmdPipelineBarrier(commandBuffer, &initialTransitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);
    NRI.CmdCopyTexture(commandBuffer, *m_BackBuffer->texture, presentingNodeIndex, nullptr, *m_ColorTexture, renderingNodeIndex, nullptr);
    NRI.CmdPipelineBarrier(commandBuffer, &finalTransitionBarriers, nullptr, nri::BarrierDependency::GRAPHICS_STAGE);
    NRI.EndCommandBuffer(commandBuffer);
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    const Frame& frame = m_Frames[frameIndex % BUFFERED_FRAME_MAX_NUM];

    if (frameIndex >= BUFFERED_FRAME_MAX_NUM)
    {
        NRI.Wait(*m_FrameFence, 1 + frameIndex - BUFFERED_FRAME_MAX_NUM);
        NRI.ResetCommandAllocator(*frame.commandAllocator);
    }

    constexpr uint32_t presentingNodeIndex = 0;
    const uint32_t renderingNodeIndex = m_IsMGPUEnabled ? (frameIndex % m_NodeNum) : presentingNodeIndex;

    nri::CommandBuffer* graphics = frame.commandBuffers[0];
    RecordGraphics(*graphics, renderingNodeIndex);

    nri::QueueSubmitDesc queueSubmitDesc = {};
    queueSubmitDesc.commandBuffers = &graphics;
    queueSubmitDesc.commandBufferNum = 1;
    queueSubmitDesc.nodeIndex = renderingNodeIndex;
    NRI.QueueSubmit(*m_CommandQueue, queueSubmitDesc);

    NRI.QueueSignal(*m_CommandQueue, *m_QueueFences[renderingNodeIndex], 1 + frameIndex);

    const uint32_t backBufferIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain);
    m_BackBuffer = &m_SwapChainBuffers[backBufferIndex];

    nri::CommandBuffer* presenting = frame.commandBuffers[1];
    CopyToSwapChainTexture(*presenting, renderingNodeIndex, presentingNodeIndex);

    NRI.QueueWait(*m_CommandQueue, *m_QueueFences[renderingNodeIndex], 1 + frameIndex);

    queueSubmitDesc.commandBuffers = &presenting;
    queueSubmitDesc.commandBufferNum = 1;
    queueSubmitDesc.nodeIndex = presentingNodeIndex;
    NRI.QueueSubmit(*m_CommandQueue, queueSubmitDesc);

    NRI.SwapChainPresent(*m_SwapChain);

    NRI.QueueSignal(*m_CommandQueue, *m_FrameFence, 1 + frameIndex);
}

void Sample::CreateMainFrameBuffer(nri::Format swapChainFormat)
{
    nri::TextureDesc textureDesc = { };
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    textureDesc.width = (uint16_t)GetWindowResolution().x;
    textureDesc.height = (uint16_t)GetWindowResolution().y;
    textureDesc.depth = 1;
    textureDesc.mipNum = 1;
    textureDesc.arraySize = 1;
    textureDesc.sampleNum = 1;

    textureDesc.format = m_DepthFormat;
    textureDesc.usageMask = nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, textureDesc, m_DepthTexture));

    textureDesc.format = swapChainFormat;
    textureDesc.usageMask = nri::TextureUsageBits::COLOR_ATTACHMENT;
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, textureDesc, m_ColorTexture));

    nri::Texture* textures[] = { m_DepthTexture, m_ColorTexture };

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 2;
    resourceGroupDesc.textures = textures;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation));

    nri::TextureTransitionBarrierDesc textureTransitionBarriers[2] = {};
    textureTransitionBarriers[0].texture = m_DepthTexture;
    textureTransitionBarriers[0].prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitionBarriers[0].nextLayout = nri::TextureLayout::DEPTH_STENCIL;
    textureTransitionBarriers[0].nextAccess = nri::AccessBits::DEPTH_STENCIL_WRITE;
    textureTransitionBarriers[1].texture = m_ColorTexture;
    textureTransitionBarriers[1].prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitionBarriers[1].nextLayout = nri::TextureLayout::GENERAL;
    textureTransitionBarriers[1].nextAccess = nri::AccessBits::COPY_SOURCE;

    nri::TransitionBarrierDesc transitionBarrierDesc = {};
    transitionBarrierDesc.textureNum = helper::GetCountOf(textureTransitionBarriers);
    transitionBarrierDesc.textures = textureTransitionBarriers;

    NRI_ABORT_ON_FAILURE(NRI.ChangeResourceStates(*m_CommandQueue, transitionBarrierDesc));

    nri::Texture2DViewDesc depthViewDesc = {m_DepthTexture, nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT, m_DepthFormat};
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(depthViewDesc, m_DepthTextureView));

    nri::Texture2DViewDesc colorViewDesc = {m_ColorTexture, nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};
    NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(colorViewDesc, m_ColorTextureView));
}

void Sample::CreateSwapChain(nri::Format& swapChainFormat)
{
    nri::SwapChainDesc swapChainDesc = {};
    swapChainDesc.windowSystemType = GetWindowSystemType();
    swapChainDesc.window = GetWindow();
    swapChainDesc.commandQueue = m_CommandQueue;
    swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
    swapChainDesc.verticalSyncInterval = m_VsyncInterval;
    swapChainDesc.width = (uint16_t)GetWindowResolution().x;
    swapChainDesc.height = (uint16_t)GetWindowResolution().y;
    swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;

    NRI_ABORT_ON_FAILURE(NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain));

    uint32_t swapChainTextureNum = 0;
    nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);
    swapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

    for (uint32_t i = 0; i < swapChainTextureNum; i++)
    {
        m_SwapChainBuffers.emplace_back();
        m_SwapChainBuffers.back().texture = swapChainTextures[i];
    }
}

void Sample::CreateCommandBuffers()
{
    for (Frame& frame : m_Frames)
    {
        NRI_ABORT_ON_FAILURE(NRI.CreateCommandAllocator(*m_CommandQueue, frame.commandAllocator));

        for (uint32_t i = 0; i < frame.commandBuffers.size(); i++)
            NRI_ABORT_ON_FAILURE(NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffers[i]));
    }
}

void Sample::CreatePipeline(nri::Format swapChainFormat)
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;

    nri::DynamicConstantBufferDesc dynamicConstantBufferDesc = { 0, nri::ShaderStage::VERTEX };

    nri::DescriptorSetDesc descriptorSetDesc = {};
    descriptorSetDesc.dynamicConstantBuffers = &dynamicConstantBufferDesc;
    descriptorSetDesc.dynamicConstantBufferNum = 1;

    nri::PipelineLayoutDesc pipelineLayoutDesc = {};
    pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
    pipelineLayoutDesc.descriptorSetNum = 1;
    pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

    NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout));

    nri::VertexStreamDesc vertexStreamDesc = { };
    vertexStreamDesc.bindingSlot = 0;
    vertexStreamDesc.stride = 5 * sizeof(float);

    nri::VertexAttributeDesc vertexAttributeDesc[2] =
    {
        {
            { "POSITION", 0 }, { 0 },
            0,
            nri::Format::RGB32_SFLOAT,
        },
        {
            { "TEXCOORD", 0 }, { 1 },
            3 * sizeof(float),
            nri::Format::RG32_SFLOAT,
        }
    };

    nri::InputAssemblyDesc inputAssemblyDesc = {};
    inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;
    inputAssemblyDesc.attributes = vertexAttributeDesc;
    inputAssemblyDesc.attributeNum = (uint8_t)helper::GetCountOf(vertexAttributeDesc);
    inputAssemblyDesc.streams = &vertexStreamDesc;
    inputAssemblyDesc.streamNum = 1;

    nri::RasterizationDesc rasterizationDesc = {};
    rasterizationDesc.viewportNum = 1;
    rasterizationDesc.fillMode = nri::FillMode::SOLID;
    rasterizationDesc.cullMode = nri::CullMode::NONE;
    rasterizationDesc.sampleNum = 1;
    rasterizationDesc.sampleMask = 0xFFFF;

    nri::ColorAttachmentDesc colorAttachmentDesc = {};
    colorAttachmentDesc.format = swapChainFormat;
    colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;

    nri::DepthAttachmentDesc depthAttachmentDesc = {};
    depthAttachmentDesc.compareFunc = nri::CompareFunc::LESS;
    depthAttachmentDesc.write = true;

    nri::OutputMergerDesc outputMergerDesc = {};
    outputMergerDesc.colorNum = 1;
    outputMergerDesc.color = &colorAttachmentDesc;

    outputMergerDesc.depthStencilFormat = m_DepthFormat;
    outputMergerDesc.depth.compareFunc = nri::CompareFunc::LESS;
    outputMergerDesc.depth.write = true;

    nri::ShaderDesc shaderStages[] =
    {
        utils::LoadShader(deviceDesc.graphicsAPI, "Simple.vs", shaderCodeStorage),
        utils::LoadShader(deviceDesc.graphicsAPI, "Simple.fs", shaderCodeStorage),
    };

    nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
    graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
    graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
    graphicsPipelineDesc.rasterization = &rasterizationDesc;
    graphicsPipelineDesc.outputMerger = &outputMergerDesc;
    graphicsPipelineDesc.shaderStages = shaderStages;
    graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);

    NRI_ABORT_ON_FAILURE(NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, m_Pipeline));
}

void Sample::CreateDescriptorSet()
{
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.dynamicConstantBufferMaxNum = m_NodeNum;
    descriptorPoolDesc.descriptorSetMaxNum = m_NodeNum;
    NRI_ABORT_ON_FAILURE(NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool));

    NRI_ABORT_ON_FAILURE(NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0, &m_DescriptorSet, 1,
        nri::ALL_NODES, 0));
}

void Sample::CreateGeometry()
{
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    const uint64_t constantRangeSize = helper::Align(sizeof(float4x4), deviceDesc.constantBufferOffsetAlignment);

    Box box;
    SetBoxGeometry(64, 0.5f, box);

    const uint32_t vertexNum = uint32_t(box.positions.size() / 3);
    std::vector<float> vertexData(vertexNum * 5);
    for (uint32_t i = 0; i < vertexNum; i++)
    {
        vertexData[i * 5] = box.positions[i * 3];
        vertexData[i * 5 + 1] = box.positions[i * 3 + 1];
        vertexData[i * 5 + 2] = box.positions[i * 3 + 2];
        vertexData[i * 5 + 3] = box.texcoords[i * 2 + 0];
        vertexData[i * 5 + 4] = box.texcoords[i * 2 + 1];
    }

    m_BoxIndexNum = (uint32_t)box.indices.size();

    const nri::BufferDesc vertexBufferDesc = { helper::GetByteSizeOf(vertexData), 0, nri::BufferUsageBits::VERTEX_BUFFER };
    const nri::BufferDesc indexBufferDesc = { helper::GetByteSizeOf(box.indices), 0, nri::BufferUsageBits::INDEX_BUFFER };
    const nri::BufferDesc transformBufferDesc = { BOX_NUM * constantRangeSize, 0, nri::BufferUsageBits::CONSTANT_BUFFER };
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, vertexBufferDesc, m_VertexBuffer));
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, indexBufferDesc, m_IndexBuffer));
    NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, transformBufferDesc, m_TransformBuffer));

    nri::Buffer* buffers[] = { m_VertexBuffer, m_IndexBuffer, m_TransformBuffer };

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = helper::GetCountOf(buffers);
    resourceGroupDesc.buffers = buffers;

    const size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation))

    std::vector<uint8_t> transforms((size_t)transformBufferDesc.size);

    float4x4 projViewMatrix;
    SetupProjViewMatrix(projViewMatrix);

    constexpr uint32_t lineSize = 17;

    for (uint32_t i = 0; i < BOX_NUM; i++)
    {
        float4x4 matrix = float4x4::Identity();

        const size_t x = i % lineSize;
        const size_t y = i / lineSize;
        matrix.PreTranslation(float3(-1.35f * 0.5f * (lineSize - 1) + 1.35f * x, 8.0f + 1.25f * y, 0.0f));
        matrix.AddScale(float3(1.0f + 0.0001f * (rand() % 2001)));

        float4x4& transform = *(float4x4*)(transforms.data() + i * constantRangeSize);
        transform = projViewMatrix * matrix;
    }

    nri::BufferUploadDesc dataDescArray[] = {
        { vertexData.data(), vertexBufferDesc.size, m_VertexBuffer, 0, nri::AccessBits::UNKNOWN, nri::AccessBits::VERTEX_BUFFER },
        { box.indices.data(), indexBufferDesc.size, m_IndexBuffer, 0, nri::AccessBits::UNKNOWN, nri::AccessBits::INDEX_BUFFER },
        { transforms.data(), transformBufferDesc.size, m_TransformBuffer, 0, nri::AccessBits::UNKNOWN, nri::AccessBits::CONSTANT_BUFFER }
    };
    NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, nullptr, 0, dataDescArray, helper::GetCountOf(dataDescArray)));

    nri::BufferViewDesc bufferViewDesc = { };
    bufferViewDesc.buffer = m_TransformBuffer;
    bufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
    bufferViewDesc.offset = 0;
    bufferViewDesc.size = constantRangeSize;

    NRI_ABORT_ON_FAILURE(NRI.CreateBufferView(bufferViewDesc, m_TransformBufferView));
    NRI.UpdateDynamicConstantBuffers(*m_DescriptorSet, nri::ALL_NODES, 0, 1, &m_TransformBufferView);
}

void Sample::SetupProjViewMatrix(float4x4& projViewMatrix)
{
    const uint32_t windowWidth = GetWindowResolution().x;
    const uint32_t windowHeight = GetWindowResolution().y;
    const float aspect = float(windowWidth) / float(windowHeight);

    float4x4 projectionMatrix;
    projectionMatrix.SetupByHalfFovxInf(DegToRad(45.0f), aspect, 0.1f, 0);

    float4x4 viewMatrix = float4x4::Identity();
    viewMatrix.SetupByRotationYPR(DegToRad(0.0f), DegToRad(0.0f), 0.0f);
    viewMatrix.WorldToView();

    const float3 cameraPosition = float3(0.0f, -4.5f, 2.0f);
    viewMatrix.PreTranslation(-cameraPosition);

    projViewMatrix = projectionMatrix * viewMatrix;
}

SAMPLE_MAIN(Sample, 0);

void SetPositions(Box& box, uint32_t positionOffset, float x, float y, float z)
{
    box.positions[positionOffset + 0] = x;
    box.positions[positionOffset + 1] = y;
    box.positions[positionOffset + 2] = z;
}

void SetTexcoords(Box& box, uint32_t texcoordOffset, float x, float y)
{
    box.texcoords[texcoordOffset + 0] = x;
    box.texcoords[texcoordOffset + 1] = y;
}

void SetQuadIndices(Box& box, uint32_t indexOffset, uint16_t topVertexIndex, uint16_t bottomVertexIndex)
{
    box.indices[indexOffset + 0] = bottomVertexIndex;
    box.indices[indexOffset + 1] = topVertexIndex;
    box.indices[indexOffset + 2] = topVertexIndex + 1;
    box.indices[indexOffset + 3] = bottomVertexIndex;
    box.indices[indexOffset + 4] = topVertexIndex + 1;
    box.indices[indexOffset + 5] = bottomVertexIndex + 1;
}

void SetBoxGeometry(uint32_t subdivisions, float boxHalfSize, Box& box)
{
    const uint32_t edgeVertexNum = subdivisions + 1;

    constexpr uint32_t positionsPerVertex = 3;
    constexpr uint32_t texcoordsPerVertex = 2;
    constexpr uint32_t verticesPerTriangle = 3;
    constexpr uint32_t trianglesPerQuad = 2;
    constexpr uint32_t facesPerBox = 6;

    const float positionStep = 2.0f * boxHalfSize / (edgeVertexNum - 1);
    const float texcoordStep = 1.0f / (edgeVertexNum - 1);

    const uint32_t verticesPerFace = edgeVertexNum * edgeVertexNum;
    const uint32_t quadsPerFace = subdivisions * subdivisions;

    if (facesPerBox * verticesPerFace > UINT16_MAX)
        exit(1);

    const uint32_t positionFaceStride = verticesPerFace * positionsPerVertex;
    const uint32_t texcoordFaceStride = verticesPerFace * texcoordsPerVertex;

    box.positions.resize(facesPerBox * verticesPerFace * positionsPerVertex, 0);
    box.texcoords.resize(facesPerBox * verticesPerFace * texcoordsPerVertex, 0);

    for (uint32_t i = 0; i < edgeVertexNum; i++)
    {
        const float positionX = -boxHalfSize + i * positionStep;
        const float texcoordX = i * texcoordStep;

        for (uint32_t j = 0; j < edgeVertexNum; j++)
        {
            const float positionY = -boxHalfSize + j * positionStep;
            const float texcoordY = j * texcoordStep;

            const uint32_t vertexIndex = i + j * edgeVertexNum;
            uint32_t positionOffset = vertexIndex * positionsPerVertex;
            uint32_t texcoordOffset = vertexIndex * texcoordsPerVertex;

            SetPositions(box, positionOffset + 0 * positionFaceStride, positionX, positionY, -boxHalfSize);
            SetPositions(box, positionOffset + 1 * positionFaceStride, positionX, positionY, +boxHalfSize);
            SetPositions(box, positionOffset + 2 * positionFaceStride, -boxHalfSize, positionX, positionY);
            SetPositions(box, positionOffset + 3 * positionFaceStride, +boxHalfSize, positionX, positionY);
            SetPositions(box, positionOffset + 4 * positionFaceStride, positionX, -boxHalfSize, positionY);
            SetPositions(box, positionOffset + 5 * positionFaceStride, positionX, +boxHalfSize, positionY);

            for (uint32_t k = 0; k < facesPerBox; k++)
                SetTexcoords(box, texcoordOffset + k * texcoordFaceStride, texcoordX, texcoordY);
        }
    }

    const uint32_t indexFaceStride = quadsPerFace * trianglesPerQuad * verticesPerTriangle;

    box.indices.resize(facesPerBox * quadsPerFace * trianglesPerQuad * verticesPerTriangle, 0);

    for (uint32_t i = 0; i < subdivisions; i++)
    {
        for (uint32_t j = 0; j < subdivisions; j++)
        {
            const uint32_t quadIndex = j + i * subdivisions;
            const uint32_t indexOffset = quadIndex * trianglesPerQuad * verticesPerTriangle;

            const uint32_t topVertexIndex = j + i * edgeVertexNum;
            const uint32_t bottomVertexIndex = j + (i + 1) * edgeVertexNum;

            for (uint32_t k = 0; k < facesPerBox; k++)
            {
                SetQuadIndices(box, indexOffset + k * indexFaceStride, uint16_t(topVertexIndex + k * verticesPerFace),
                    uint16_t(bottomVertexIndex + k * verticesPerFace));
            }
        }
    }
}