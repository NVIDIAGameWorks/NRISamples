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
    nri::DeviceSemaphore* deviceSemaphore;
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
    nri::QueueSemaphore* m_SwapChainTextureReleaseSemaphore = nullptr;
    nri::QueueSemaphore* m_SwapChainTextureAcquireSemaphore = nullptr;
    nri::QueueSemaphore* m_ComputeSemaphore = nullptr;
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
        NRI.DestroyDeviceSemaphore(*frame.deviceSemaphore);
    }

    for (uint32_t i = 0; i < m_SwapChainBuffers.size(); i++)
    {
        NRI.DestroyFrameBuffer(*m_SwapChainBuffers[i].frameBuffer);
        NRI.DestroyDescriptor(*m_SwapChainBuffers[i].colorAttachment);
    }

    NRI.DestroyDescriptor(*m_Descriptor);
    NRI.DestroyTexture(*m_Texture);
    NRI.DestroyBuffer(*m_GeometryBuffer);
    NRI.DestroyPipeline(*m_GraphicsPipeline);
    NRI.DestroyPipeline(*m_ComputePipeline);
    NRI.DestroyPipelineLayout(*m_GraphicsPipelineLayout);
    NRI.DestroyPipelineLayout(*m_ComputePipelineLayout);
    NRI.DestroyDescriptorPool(*m_DescriptorPool);
    NRI.DestroyQueueSemaphore(*m_SwapChainTextureAcquireSemaphore);
    NRI.DestroyQueueSemaphore(*m_SwapChainTextureReleaseSemaphore);
    NRI.DestroyQueueSemaphore(*m_ComputeSemaphore);
    NRI.DestroySwapChain(*m_SwapChain);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    DestroyUserInterface();

    nri::DestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    nri::PhysicalDeviceGroup mostPerformantPhysicalDeviceGroup = {};
    uint32_t deviceGroupNum = 1;
    NRI_ABORT_ON_FAILURE(nri::GetPhysicalDevices(&mostPerformantPhysicalDeviceGroup, deviceGroupNum));

    // Device
    nri::DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableAPIValidation = m_DebugAPI;
    deviceCreationDesc.enableNRIValidation = m_DebugNRI;
    deviceCreationDesc.D3D11CommandBufferEmulation = D3D11_COMMANDBUFFER_EMULATION;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    deviceCreationDesc.physicalDeviceGroup = &mostPerformantPhysicalDeviceGroup;
    deviceCreationDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    NRI_ABORT_ON_FAILURE( nri::CreateDevice(deviceCreationDesc, m_Device) );

    // NRI
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::GetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI) );

    // Command queue
    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueueGraphics) );
    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::COMPUTE, m_CommandQueueCompute) );

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
        nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum, swapChainFormat);

        for (uint32_t i = 0; i < swapChainTextureNum; i++)
        {
            nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};

            nri::Descriptor* colorAttachment;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(textureViewDesc, colorAttachment) );

            nri::ClearValueDesc clearColor = {};
            nri::FrameBufferDesc frameBufferDesc = {};
            frameBufferDesc.colorAttachmentNum = 1;
            frameBufferDesc.colorAttachments = &colorAttachment;
            frameBufferDesc.colorClearValues = &clearColor;
            nri::FrameBuffer* frameBuffer;
            NRI_ABORT_ON_FAILURE( NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, frameBuffer) );

            const BackBuffer backBuffer = { frameBuffer, frameBuffer, colorAttachment, swapChainTextures[i] };
            m_SwapChainBuffers.push_back(backBuffer);
        }
    }

    // Queue semaphore
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_SwapChainTextureAcquireSemaphore) );
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_SwapChainTextureReleaseSemaphore) );
    NRI_ABORT_ON_FAILURE( NRI.CreateQueueSemaphore(*m_Device, m_ComputeSemaphore) );

    // Buffered resources
    for (Frame& frame : m_Frames)
    {
        NRI_ABORT_ON_FAILURE( NRI.CreateDeviceSemaphore(*m_Device, true, frame.deviceSemaphore) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueueGraphics, nri::WHOLE_DEVICE_GROUP, frame.commandAllocatorGraphics) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueueCompute, nri::WHOLE_DEVICE_GROUP, frame.commandAllocatorCompute) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocatorCompute, frame.commandBufferCompute) );
        for (size_t i = 0; i < frame.commandBufferGraphics.size(); i++)
            NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocatorGraphics, frame.commandBufferGraphics[i]) );
    }

    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;

    // Graphics pipeline
    {
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

    // Compute pipeline
    {
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

    // Storage texture
    {
        nri::TextureDesc textureDesc = nri::Texture2D(swapChainFormat, (uint16_t)GetWindowResolution().x / 2, (uint16_t)GetWindowResolution().y, 1, 1,
            nri::TextureUsageBits::SHADER_RESOURCE_STORAGE);
        NRI_ABORT_ON_FAILURE( NRI.CreateTexture(*m_Device, textureDesc, m_Texture) );
    }

    // Geometry buffer
    {
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

    // Descriptor pool
    {
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = 1;
        descriptorPoolDesc.storageTextureMaxNum = 1;

        NRI_ABORT_ON_FAILURE( NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool) );
    }

    // Storage descriptor
    {
        nri::Texture2DViewDesc texture2DViewDesc = {m_Texture, nri::Texture2DViewType::SHADER_RESOURCE_STORAGE_2D, swapChainFormat};

        NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(texture2DViewDesc, m_Descriptor) );
    }

    // Descriptor set
    {
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_ComputePipelineLayout, 0, &m_DescriptorSet, 1,
            nri::WHOLE_DEVICE_GROUP, 0) );

        nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = {&m_Descriptor, 1, 0};
        NRI.UpdateDescriptorRanges(*m_DescriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &descriptorRangeUpdateDesc);
    }

    // Upload data
    {
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
        textureData.nextLayout = nri::TextureLayout::GENERAL;
        textureData.nextAccess = nri::AccessBits::SHADER_RESOURCE_STORAGE;

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
    PrepareUserInterface();

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

    const uint32_t backBufferIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain, *m_SwapChainTextureAcquireSemaphore);
    const BackBuffer& backBuffer = m_SwapChainBuffers[backBufferIndex];

    nri::CommandAllocator& commandAllocatorGraphics = *frame.commandAllocatorGraphics;
    nri::CommandAllocator& commandAllocatorCompute = *frame.commandAllocatorCompute;
    nri::DeviceSemaphore& deviceSemaphore = *frame.deviceSemaphore;

    NRI.WaitForSemaphore(*m_CommandQueueGraphics, deviceSemaphore);
    NRI.ResetCommandAllocator(commandAllocatorGraphics);
    NRI.ResetCommandAllocator(commandAllocatorCompute);

    nri::TextureTransitionBarrierDesc textureTransitionBarrierDescs[2] = {};

    textureTransitionBarrierDescs[0].texture = backBuffer.texture;
    textureTransitionBarrierDescs[0].prevAccess = nri::AccessBits::UNKNOWN;
    textureTransitionBarrierDescs[0].nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransitionBarrierDescs[0].prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitionBarrierDescs[0].nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
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

        NRI.CmdBeginRenderPass(commandBuffer1, *backBuffer.frameBuffer, nri::RenderPassBeginFlag::NONE);

        const nri::Viewport viewport = { 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f };
        const nri::Rect scissorRect = { 0, 0, windowWidth, windowHeight };
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

        RenderUserInterface(commandBuffer1);

        NRI.CmdEndRenderPass(commandBuffer1);

    }
    NRI.EndCommandBuffer(commandBuffer1);

    // Fill command buffer #2 (graphics)
    nri::CommandBuffer& commandBuffer2 = *frame.commandBufferGraphics[2];
    NRI.BeginCommandBuffer(commandBuffer2, nullptr, 0);
    {
        helper::Annotation annotation(NRI, commandBuffer2, "Composition");

        // Resource transitions
        textureTransitionBarrierDescs[0].prevAccess = nri::AccessBits::COLOR_ATTACHMENT;
        textureTransitionBarrierDescs[0].nextAccess = nri::AccessBits::COPY_DESTINATION;
        textureTransitionBarrierDescs[0].prevLayout = nri::TextureLayout::COLOR_ATTACHMENT;
        textureTransitionBarrierDescs[0].nextLayout = nri::TextureLayout::GENERAL;

        textureTransitionBarrierDescs[1].prevAccess = nri::AccessBits::SHADER_RESOURCE_STORAGE;
        textureTransitionBarrierDescs[1].nextAccess = nri::AccessBits::COPY_SOURCE;
        textureTransitionBarrierDescs[1].prevLayout = nri::TextureLayout::GENERAL;
        textureTransitionBarrierDescs[1].nextLayout = nri::TextureLayout::GENERAL;

        transitionBarriers.textureNum = 2;
        NRI.CmdPipelineBarrier(commandBuffer2, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);

        // Copy texture produced by compute to back buffer
        nri::TextureRegionDesc dstRegion = {};
        dstRegion.offset[0] = (uint16_t)windowWidth / 2;

        nri::TextureRegionDesc srcRegion = {};
        srcRegion.size[0] = (uint16_t)windowWidth / 2;
        srcRegion.size[1] = (uint16_t)windowHeight;
        srcRegion.size[2] = 1;

        NRI.CmdCopyTexture(commandBuffer2, *backBuffer.texture, 0, &dstRegion, *m_Texture, 0, &srcRegion);

        // Resource transitions
        textureTransitionBarrierDescs[0].prevAccess = nri::AccessBits::COPY_DESTINATION;
        textureTransitionBarrierDescs[0].nextAccess = nri::AccessBits::UNKNOWN;
        textureTransitionBarrierDescs[0].prevLayout = nri::TextureLayout::GENERAL;
        textureTransitionBarrierDescs[0].nextLayout = nri::TextureLayout::PRESENT;

        textureTransitionBarrierDescs[1].prevAccess = nri::AccessBits::COPY_SOURCE;
        textureTransitionBarrierDescs[1].nextAccess = nri::AccessBits::SHADER_RESOURCE_STORAGE;
        textureTransitionBarrierDescs[1].prevLayout = nri::TextureLayout::GENERAL;
        textureTransitionBarrierDescs[1].nextLayout = nri::TextureLayout::GENERAL;

        transitionBarriers.textureNum = 2;
        NRI.CmdPipelineBarrier(commandBuffer2, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
    }
    NRI.EndCommandBuffer(commandBuffer2);

    nri::CommandBuffer* commandBufferArray[3] = { &commandBuffer0, &commandBuffer1, &commandBuffer2 };

    // Submit work
    nri::WorkSubmissionDesc workSubmissionDesc = {};
    if (m_IsAsyncMode)
    {
        workSubmissionDesc.commandBufferNum = 1;
        workSubmissionDesc.commandBuffers = &commandBufferArray[0];
        workSubmissionDesc.signalNum = 1;
        workSubmissionDesc.signal = &m_ComputeSemaphore;

        NRI.SubmitQueueWork(*m_CommandQueueCompute, workSubmissionDesc, nullptr);

        workSubmissionDesc = {};
        workSubmissionDesc.commandBufferNum = 1;
        workSubmissionDesc.commandBuffers = &commandBufferArray[1];
        workSubmissionDesc.waitNum = 1;
        workSubmissionDesc.wait = &m_SwapChainTextureAcquireSemaphore;

        NRI.SubmitQueueWork(*m_CommandQueueGraphics, workSubmissionDesc, nullptr);

        workSubmissionDesc = {};
        workSubmissionDesc.commandBufferNum = 1;
        workSubmissionDesc.commandBuffers = &commandBufferArray[2];
        workSubmissionDesc.waitNum = 1;
        workSubmissionDesc.wait = &m_ComputeSemaphore;
        workSubmissionDesc.signalNum = 1;
        workSubmissionDesc.signal = &m_SwapChainTextureReleaseSemaphore;

        NRI.SubmitQueueWork(*m_CommandQueueGraphics, workSubmissionDesc, &deviceSemaphore);
    }
    else
    {
        workSubmissionDesc.commandBufferNum = helper::GetCountOf(commandBufferArray);
        workSubmissionDesc.commandBuffers = commandBufferArray;
        workSubmissionDesc.waitNum = 1;
        workSubmissionDesc.wait = &m_SwapChainTextureAcquireSemaphore;
        workSubmissionDesc.signalNum = 1;
        workSubmissionDesc.signal = &m_SwapChainTextureReleaseSemaphore;

        NRI.SubmitQueueWork(*m_CommandQueueGraphics, workSubmissionDesc, &deviceSemaphore);
    }

    NRI.SwapChainPresent(*m_SwapChain, *m_SwapChainTextureReleaseSemaphore);
}

SAMPLE_MAIN(Sample, 0);
