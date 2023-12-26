/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "NRIFramework.h"

#include <array>

constexpr uint32_t VERTEX_NUM = 1000000 * 3;

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{};

struct Frame
{
    nri::CommandAllocator* commandAllocatorGraphics;
    nri::CommandAllocator* commandAllocatorCompute;
    std::array<nri::CommandBuffer*, 3> commandBufferGraphics;
    nri::CommandBuffer* commandBufferCompute;
};

struct Vertex
{
    float position[3];
};

class Sample : public SampleBase
{
public:

    Sample()
    {}

    ~Sample();

    bool Initialize(nri::GraphicsAPI graphicsAPI) override;
    void PrepareFrame(uint32_t frameIndex) override;
    void RenderFrame(uint32_t frameIndex) override;

private:

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueueGraphics = nullptr;
    nri::CommandQueue* m_CommandQueueCompute = nullptr;
    nri::Fence* m_FrameFence = nullptr;
    nri::Fence* m_ComputeFence = nullptr;
    nri::DescriptorPool* m_DescriptorPool = nullptr;
    nri::PipelineLayout* m_GraphicsPipelineLayout = nullptr;
    nri::PipelineLayout* m_ComputePipelineLayout = nullptr;
    nri::Pipeline* m_GraphicsPipeline = nullptr;
    nri::Pipeline* m_ComputePipeline = nullptr;
    nri::Buffer* m_GeometryBuffer = nullptr;
    nri::Texture* m_Texture = nullptr;
    nri::DescriptorSet* m_DescriptorSet = nullptr;
    nri::Descriptor* m_Descriptor = nullptr;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::Memory*> m_MemoryAllocations;

    bool m_IsAsyncMode = true;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueueGraphics);

    for (Frame& frame : m_Frames)
    {
        for (size_t i = 0; i < frame.commandBufferGraphics.size(); i++)
            NRI.DestroyCommandBuffer(*frame.commandBufferGraphics[i]);

        NRI.DestroyCommandBuffer(*frame.commandBufferCompute);
        NRI.DestroyCommandAllocator(*frame.commandAllocatorCompute);
        NRI.DestroyCommandAllocator(*frame.commandAllocatorGraphics);
    }

    for (uint32_t i = 0; i < m_SwapChainBuffers.size(); i++)
        NRI.DestroyDescriptor(*m_SwapChainBuffers[i].colorAttachment);

    NRI.DestroyDescriptor(*m_Descriptor);
    NRI.DestroyTexture(*m_Texture);
    NRI.DestroyBuffer(*m_GeometryBuffer);
    NRI.DestroyPipeline(*m_GraphicsPipeline);
    NRI.DestroyPipeline(*m_ComputePipeline);
    NRI.DestroyPipelineLayout(*m_GraphicsPipelineLayout);
    NRI.DestroyPipelineLayout(*m_ComputePipelineLayout);
    NRI.DestroyDescriptorPool(*m_DescriptorPool);
    NRI.DestroyFence(*m_ComputeFence);
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

    // Device
    nri::DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableAPIValidation = m_DebugAPI;
    deviceCreationDesc.enableNRIValidation = m_DebugNRI;
    deviceCreationDesc.D3D11CommandBufferEmulation = D3D11_COMMANDBUFFER_EMULATION;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    deviceCreationDesc.adapterDesc = &bestAdapterDesc;
    deviceCreationDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    NRI_ABORT_ON_FAILURE( nri::nriCreateDevice(deviceCreationDesc, m_Device) );

    // NRI
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI) );

    // Command queue
    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueueGraphics) );
    NRI.SetCommandQueueDebugName(*m_CommandQueueGraphics, "GraphicsQueue");

    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::COMPUTE, m_CommandQueueCompute) );
    NRI.SetCommandQueueDebugName(*m_CommandQueueCompute, "ComputeQueue");

    // Fences
    NRI_ABORT_ON_FAILURE( NRI.CreateFence(*m_Device, 0, m_ComputeFence) );
    NRI_ABORT_ON_FAILURE( NRI.CreateFence(*m_Device, 0, m_FrameFence) );

    // Swap chain
    nri::Format swapChainFormat;
    {
        nri::SwapChainDesc swapChainDesc = {};
        swapChainDesc.windowSystemType = GetWindowSystemType();
        swapChainDesc.window = GetWindow();
        swapChainDesc.commandQueue = m_CommandQueueGraphics;
        swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
        swapChainDesc.verticalSyncInterval = m_VsyncInterval;
        swapChainDesc.width = (uint16_t)GetWindowResolution().x;
        swapChainDesc.height = (uint16_t)GetWindowResolution().y;
        swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
        NRI_ABORT_ON_FAILURE( NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain) );

        uint32_t swapChainTextureNum;
        nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);
        swapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

        for (uint32_t i = 0; i < swapChainTextureNum; i++)
        {
            nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};

            nri::Descriptor* colorAttachment;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(textureViewDesc, colorAttachment) );

            const BackBuffer backBuffer = { colorAttachment, swapChainTextures[i] };
            m_SwapChainBuffers.push_back(backBuffer);
        }
    }

    // Buffered resources
    for (Frame& frame : m_Frames)
    {
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueueGraphics, frame.commandAllocatorGraphics) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueueCompute, frame.commandAllocatorCompute) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocatorCompute, frame.commandBufferCompute) );

        for (size_t i = 0; i < frame.commandBufferGraphics.size(); i++)
            NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocatorGraphics, frame.commandBufferGraphics[i]) );
    }

    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;

    { // Graphics pipeline
        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;
        NRI_ABORT_ON_FAILURE( NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_GraphicsPipelineLayout) );

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;
        vertexStreamDesc.stride = sizeof(Vertex);

        nri::VertexAttributeDesc vertexAttributeDesc[1] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
            vertexAttributeDesc[0].streamIndex = 0;
            vertexAttributeDesc[0].offset = helper::GetOffsetOf(&Vertex::position);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk.location = {0};
        }

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

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.color = &colorAttachmentDesc;

        nri::ShaderDesc shaderStages[] =
        {
            utils::LoadShader(deviceDesc.graphicsAPI, "Triangles.vs", shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, "Triangles.fs", shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = m_GraphicsPipelineLayout;
        graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = &rasterizationDesc;
        graphicsPipelineDesc.outputMerger = &outputMergerDesc;
        graphicsPipelineDesc.shaderStages = shaderStages;
        graphicsPipelineDesc.shaderStageNum = helper::GetCountOf(shaderStages);
        NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, m_GraphicsPipeline) );
    }

    { // Compute pipeline
        nri::DescriptorRangeDesc descriptorRangeStorage = {0, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::ShaderStage::COMPUTE};

        nri::DescriptorSetDesc descriptorSetDesc = {0, &descriptorRangeStorage, 1};

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = 1;
        pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::COMPUTE;
        NRI_ABORT_ON_FAILURE( NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_ComputePipelineLayout) );

        nri::ComputePipelineDesc computePipelineDesc = {};
        computePipelineDesc.pipelineLayout = m_ComputePipelineLayout;
        computePipelineDesc.computeShader = utils::LoadShader(deviceDesc.graphicsAPI, "Surface.cs", shaderCodeStorage);
        NRI_ABORT_ON_FAILURE( NRI.CreateComputePipeline(*m_Device, computePipelineDesc, m_ComputePipeline) );
    }

    { // Storage texture
        nri::TextureDesc textureDesc = nri::Texture2D(swapChainFormat, (uint16_t)GetWindowResolution().x / 2, (uint16_t)GetWindowResolution().y, 1, 1,
            nri::TextureUsageBits::SHADER_RESOURCE_STORAGE);
        NRI_ABORT_ON_FAILURE( NRI.CreateTexture(*m_Device, textureDesc, m_Texture) );
    }

    { // Geometry buffer
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(Vertex) * VERTEX_NUM;
        bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER;
        NRI_ABORT_ON_FAILURE( NRI.CreateBuffer(*m_Device, bufferDesc, m_GeometryBuffer) );
    }

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_GeometryBuffer;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &m_Texture;

    m_MemoryAllocations.resize(NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data()))

    { // Descriptor pool
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = 1;
        descriptorPoolDesc.storageTextureMaxNum = 1;

        NRI_ABORT_ON_FAILURE( NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool) );
    }

    { // Storage descriptor
        nri::Texture2DViewDesc texture2DViewDesc = {m_Texture, nri::Texture2DViewType::SHADER_RESOURCE_STORAGE_2D, swapChainFormat};

        NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(texture2DViewDesc, m_Descriptor) );
    }

    { // Descriptor set
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_ComputePipelineLayout, 0, &m_DescriptorSet, 1,
            nri::ALL_NODES, 0) );

        nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = {&m_Descriptor, 1, 0};
        NRI.UpdateDescriptorRanges(*m_DescriptorSet, nri::ALL_NODES, 0, 1, &descriptorRangeUpdateDesc);
    }

    { // Upload data
        std::vector<Vertex> geometryBufferData(VERTEX_NUM);
        for (uint32_t i = 0; i < VERTEX_NUM; i += 3)
        {
            Vertex& v0 = geometryBufferData[i];
            v0.position[0] = Rand::sf1(&m_FastRandState);
            v0.position[1] = Rand::sf1(&m_FastRandState);
            v0.position[2] = Rand::uf1(&m_FastRandState);

            Vertex& v1 = geometryBufferData[i + 1];
            v1.position[0] = v0.position[0] + Rand::sf1(&m_FastRandState) * 0.3f;
            v1.position[1] = v0.position[1] + Rand::sf1(&m_FastRandState) * 0.3f;
            v1.position[2] = Rand::uf1(&m_FastRandState);

            Vertex& v2 = geometryBufferData[i + 2];
            v2.position[0] = v0.position[0] + Rand::sf1(&m_FastRandState) * 0.3f;
            v2.position[1] = v0.position[1] + Rand::sf1(&m_FastRandState) * 0.3f;
            v2.position[2] = Rand::uf1(&m_FastRandState);
        }

        nri::TextureUploadDesc textureData = {};
        textureData.subresources = nullptr;
        textureData.texture = m_Texture;
        textureData.nextState = {nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::TextureLayout::GENERAL};

        nri::BufferUploadDesc bufferData = {};
        bufferData.buffer = m_GeometryBuffer;
        bufferData.data = &geometryBufferData[0];
        bufferData.dataSize = geometryBufferData.size();
        bufferData.nextAccess = nri::AccessBits::VERTEX_BUFFER;

        NRI_ABORT_ON_FAILURE( NRI.UploadData(*m_CommandQueueGraphics, &textureData, 1, &bufferData, 1) );
    }

    return CreateUserInterface(*m_Device, NRI, NRI, swapChainFormat);
}

void Sample::PrepareFrame(uint32_t)
{
    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize);
    {
        ImGui::Text("Left - graphics, Right - compute");
        ImGui::Checkbox("Use ASYNC compute", &m_IsAsyncMode);
    }
    ImGui::End();
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    const uint32_t windowWidth = GetWindowResolution().x;
    const uint32_t windowHeight = GetWindowResolution().y;
    const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
    const Frame& frame = m_Frames[bufferedFrameIndex];

    nri::CommandAllocator& commandAllocatorGraphics = *frame.commandAllocatorGraphics;
    nri::CommandAllocator& commandAllocatorCompute = *frame.commandAllocatorCompute;

    if (frameIndex >= BUFFERED_FRAME_MAX_NUM)
    {
        NRI.Wait(*m_FrameFence, 1 + frameIndex - BUFFERED_FRAME_MAX_NUM);
        NRI.ResetCommandAllocator(commandAllocatorGraphics);
        NRI.ResetCommandAllocator(commandAllocatorCompute);
    }

    const uint32_t backBufferIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain);
    const BackBuffer& backBuffer = m_SwapChainBuffers[backBufferIndex];

    nri::TextureTransitionBarrierDesc textureTransitionBarrierDescs[2] = {};

    textureTransitionBarrierDescs[0].texture = backBuffer.texture;
    textureTransitionBarrierDescs[0].nextState = {nri::AccessBits::COLOR_ATTACHMENT, nri::TextureLayout::COLOR_ATTACHMENT};
    textureTransitionBarrierDescs[0].arraySize = 1;
    textureTransitionBarrierDescs[0].mipNum = 1;

    textureTransitionBarrierDescs[1].texture = m_Texture;
    textureTransitionBarrierDescs[1].arraySize = 1;
    textureTransitionBarrierDescs[1].mipNum = 1;

    nri::TransitionBarrierDesc transitionBarriers = {};
    transitionBarriers.textures = textureTransitionBarrierDescs;

    // Fill command buffer #0 (graphics or compute)
    nri::CommandBuffer& commandBuffer0 = m_IsAsyncMode ? *frame.commandBufferCompute : *frame.commandBufferGraphics[0];
    NRI.BeginCommandBuffer(commandBuffer0, m_DescriptorPool, 0);
    {
        helper::Annotation annotation(NRI, commandBuffer0, "Compute");

        const uint32_t nx = ((windowWidth / 2) + 15) / 16;
        const uint32_t ny = (windowHeight + 15) / 16;

        NRI.CmdSetPipelineLayout(commandBuffer0, *m_ComputePipelineLayout);
        NRI.CmdSetPipeline(commandBuffer0, *m_ComputePipeline);
        NRI.CmdSetDescriptorSet(commandBuffer0, 0, *m_DescriptorSet, nullptr);
        NRI.CmdDispatch(commandBuffer0, nx, ny, 1);
    }
    NRI.EndCommandBuffer(commandBuffer0);

    // Fill command buffer #1 (graphics)
    nri::CommandBuffer& commandBuffer1 = *frame.commandBufferGraphics[1];
    NRI.BeginCommandBuffer(commandBuffer1, nullptr, 0);
    {
        helper::Annotation annotation(NRI, commandBuffer1, "Graphics");

        transitionBarriers.textureNum = 1;
        NRI.CmdPipelineBarrier(commandBuffer1, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);

        nri::AttachmentsDesc attachmentsDesc = {};
        attachmentsDesc.colorNum = 1;
        attachmentsDesc.colors = &backBuffer.colorAttachment;

        NRI.CmdBeginRendering(commandBuffer1, attachmentsDesc);
        {
            const nri::Viewport viewport = { 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f };
            const nri::Rect scissorRect = { 0, 0, (nri::Dim_t)windowWidth, (nri::Dim_t)windowHeight };
            NRI.CmdSetViewports(commandBuffer1, &viewport, 1);
            NRI.CmdSetScissors(commandBuffer1, &scissorRect, 1);

            nri::ClearDesc clearDesc = {};
            clearDesc.colorAttachmentIndex = 0;
            NRI.CmdClearAttachments(commandBuffer1, &clearDesc, 1, nullptr, 0);

            const uint64_t offset = 0;
            NRI.CmdSetPipelineLayout(commandBuffer1, *m_GraphicsPipelineLayout);
            NRI.CmdSetPipeline(commandBuffer1, *m_GraphicsPipeline);
            NRI.CmdSetIndexBuffer(commandBuffer1, *m_GeometryBuffer, 0, nri::IndexType::UINT16);
            NRI.CmdSetVertexBuffers(commandBuffer1, 0, 1, &m_GeometryBuffer, &offset);
            NRI.CmdDraw(commandBuffer1, VERTEX_NUM, 1, 0, 0);

            RenderUserInterface(*m_Device, commandBuffer1);
        }
        NRI.CmdEndRendering(commandBuffer1);

    }
    NRI.EndCommandBuffer(commandBuffer1);

    // Fill command buffer #2 (graphics)
    nri::CommandBuffer& commandBuffer2 = *frame.commandBufferGraphics[2];
    NRI.BeginCommandBuffer(commandBuffer2, nullptr, 0);
    {
        helper::Annotation annotation(NRI, commandBuffer2, "Composition");

        // Resource transitions
        textureTransitionBarrierDescs[0].prevState = {nri::AccessBits::COLOR_ATTACHMENT, nri::TextureLayout::COLOR_ATTACHMENT};
        textureTransitionBarrierDescs[0].nextState = {nri::AccessBits::COPY_DESTINATION, nri::TextureLayout::COPY_DESTINATION};

        textureTransitionBarrierDescs[1].prevState = {nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::TextureLayout::GENERAL};
        textureTransitionBarrierDescs[1].nextState = {nri::AccessBits::COPY_SOURCE, nri::TextureLayout::COPY_SOURCE};

        transitionBarriers.textureNum = 2;
        NRI.CmdPipelineBarrier(commandBuffer2, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);

        // Copy texture produced by compute to back buffer
        nri::TextureRegionDesc dstRegion = {};
        dstRegion.x = (uint16_t)windowWidth / 2;

        nri::TextureRegionDesc srcRegion = {};
        srcRegion.width = (uint16_t)windowWidth / 2;
        srcRegion.height = (uint16_t)windowHeight;
        srcRegion.depth = 1;

        NRI.CmdCopyTexture(commandBuffer2, *backBuffer.texture, 0, &dstRegion, *m_Texture, 0, &srcRegion);

        // Resource transitions
        textureTransitionBarrierDescs[0].prevState = {nri::AccessBits::COPY_DESTINATION, nri::TextureLayout::COPY_DESTINATION};
        textureTransitionBarrierDescs[0].nextState = {nri::AccessBits::UNKNOWN, nri::TextureLayout::PRESENT};

        textureTransitionBarrierDescs[1].prevState = {nri::AccessBits::COPY_SOURCE, nri::TextureLayout::COPY_SOURCE};
        textureTransitionBarrierDescs[1].nextState = {nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::TextureLayout::GENERAL};

        transitionBarriers.textureNum = 2;
        NRI.CmdPipelineBarrier(commandBuffer2, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
    }
    NRI.EndCommandBuffer(commandBuffer2);

    nri::CommandBuffer* commandBufferArray[3] = { &commandBuffer0, &commandBuffer1, &commandBuffer2 };

    // Submit work
    if (m_IsAsyncMode)
    {
        // Wait for the Composition task (from the previous frame) completion before doing the Compute task
        NRI.QueueWait(*m_CommandQueueCompute, *m_FrameFence, frameIndex);

        // Submit the Compute task into the COMPUTE queue
        nri::QueueSubmitDesc computeTaskDesc = {&commandBufferArray[0], 1};
        NRI.QueueSubmit(*m_CommandQueueCompute, computeTaskDesc);

        // Insert fence into the COMPUTE queue
        NRI.QueueSignal(*m_CommandQueueCompute, *m_ComputeFence, 1 + frameIndex);

        // Submit the Graphics task into the GRAPHICS queue
        nri::QueueSubmitDesc graphicsTaskDesc = {&commandBufferArray[1], 1};
        NRI.QueueSubmit(*m_CommandQueueGraphics, graphicsTaskDesc);

        // Wait for the Compute task completion before doing the Composition task
        NRI.QueueWait(*m_CommandQueueGraphics, *m_ComputeFence, 1 + frameIndex);

        // Submit the Composition task into the GRAPHICS queue
        nri::QueueSubmitDesc compositionTaskDesc = {&commandBufferArray[2], 1};
        NRI.QueueSubmit(*m_CommandQueueGraphics, compositionTaskDesc);
    }
    else
    {
        // Submit all tasks to the GRAPHICS queue
        nri::QueueSubmitDesc queueSubmitDesc = {commandBufferArray, helper::GetCountOf(commandBufferArray)};
        NRI.QueueSubmit(*m_CommandQueueGraphics, queueSubmitDesc);
    }

    // Present
    NRI.SwapChainPresent(*m_SwapChain);

    // Next frame
    NRI.QueueSignal(*m_CommandQueueGraphics, *m_FrameFence, 1 + frameIndex);
}

SAMPLE_MAIN(Sample, 0);
