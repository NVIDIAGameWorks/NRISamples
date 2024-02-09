// © 2021 NVIDIA Corporation

#include "NRIFramework.h"

#include <array>

constexpr uint32_t GLOBAL_DESCRIPTOR_SET = 0;
constexpr uint32_t MATERIAL_DESCRIPTOR_SET = 1;
constexpr float CLEAR_DEPTH = 0.0f;
constexpr uint32_t TEXTURES_PER_MATERIAL = 4;
constexpr uint32_t CONSTANT_BUFFER = 0;
constexpr uint32_t INDEX_BUFFER = 1;
constexpr uint32_t VERTEX_BUFFER = 2;

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{};

struct GlobalConstantBufferLayout
{
    float4x4 gWorldToClip;
    float3 gCameraPos;
};

struct Frame
{
    nri::CommandAllocator* commandAllocator;
    nri::CommandBuffer* commandBuffer;
    uint32_t globalConstantBufferViewOffsets;
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
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::Fence* m_FrameFence = nullptr;
    nri::DescriptorPool* m_DescriptorPool = nullptr;
    nri::PipelineLayout* m_PipelineLayout = nullptr;
    nri::Descriptor* m_DepthAttachment = nullptr;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
    std::vector<nri::Pipeline*> m_Pipelines;
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::DescriptorSet*> m_DescriptorSets;
    std::vector<nri::Texture*> m_Textures;
    std::vector<nri::Buffer*> m_Buffers;
    std::vector<nri::Memory*> m_MemoryAllocations;
    std::vector<nri::Descriptor*> m_Descriptors;

    nri::Format m_DepthFormat = nri::Format::UNKNOWN;

    utils::Scene m_Scene;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (Frame& frame : m_Frames)
    {
        NRI.DestroyCommandBuffer(*frame.commandBuffer);
        NRI.DestroyCommandAllocator(*frame.commandAllocator);
    }

    for (uint32_t i = 0; i < m_SwapChainBuffers.size(); i++)
        NRI.DestroyDescriptor(*m_SwapChainBuffers[i].colorAttachment);

    for (size_t i = 0; i < m_Descriptors.size(); i++)
        NRI.DestroyDescriptor(*m_Descriptors[i]);

    for (size_t i = 0; i < m_Textures.size(); i++)
        NRI.DestroyTexture(*m_Textures[i]);

    for (size_t i = 0; i < m_Buffers.size(); i++)
        NRI.DestroyBuffer(*m_Buffers[i]);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    for (size_t i = 0; i < m_Pipelines.size(); i++)
        NRI.DestroyPipeline(*m_Pipelines[i]);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);

    NRI.DestroyDescriptorPool(*m_DescriptorPool);
    NRI.DestroyFence(*m_FrameFence);
    NRI.DestroySwapChain(*m_SwapChain);

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
    deviceCreationDesc.enableD3D11CommandBufferEmulation = D3D11_COMMANDBUFFER_EMULATION;
    deviceCreationDesc.spirvBindingOffsets = SPIRV_BINDING_OFFSETS;
    deviceCreationDesc.adapterDesc = &bestAdapterDesc;
    deviceCreationDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    NRI_ABORT_ON_FAILURE( nri::nriCreateDevice(deviceCreationDesc, m_Device) );

    // NRI
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI) );
    NRI_ABORT_ON_FAILURE( nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI) );

    // Command queue
    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue) );

    // Fences
    NRI_ABORT_ON_FAILURE( NRI.CreateFence(*m_Device, 0, m_FrameFence) );

    m_DepthFormat = nri::GetSupportedDepthFormat(NRI, *m_Device, 24, false);

    { // Swap chain
        nri::SwapChainDesc swapChainDesc = {};
        swapChainDesc.window = GetWindow();
        swapChainDesc.commandQueue = m_CommandQueue;
        swapChainDesc.format = nri::SwapChainFormat::BT709_G22_10BIT;
        swapChainDesc.verticalSyncInterval = m_VsyncInterval;
        swapChainDesc.width = (uint16_t)GetWindowResolution().x;
        swapChainDesc.height = (uint16_t)GetWindowResolution().y;
        swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
        NRI_ABORT_ON_FAILURE( NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain) );
    }

    uint32_t swapChainTextureNum;
    nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);
    nri::Format swapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

    // Buffered resources
    for (Frame& frame : m_Frames)
    {
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueue, frame.commandAllocator) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffer) );
    }

    // Pipeline
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;
    {
        nri::DescriptorRangeDesc globalDescriptorRange[2];
        globalDescriptorRange[0] = { 0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::ALL };
        globalDescriptorRange[1] = { 0, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER };

        nri::DescriptorRangeDesc materialDescriptorRange[1];
        materialDescriptorRange[0] = { 0, TEXTURES_PER_MATERIAL, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER };

        nri::DescriptorSetDesc descriptorSetDescs[] =
        {
            {0, globalDescriptorRange, helper::GetCountOf(globalDescriptorRange)},
            {1, materialDescriptorRange, helper::GetCountOf(materialDescriptorRange)},
        };

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = helper::GetCountOf(descriptorSetDescs);
        pipelineLayoutDesc.descriptorSets = descriptorSetDescs;
        pipelineLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        NRI_ABORT_ON_FAILURE( NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout) );

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;
        vertexStreamDesc.stride = sizeof(utils::Vertex);

        nri::VertexAttributeDesc vertexAttributeDesc[4] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
            vertexAttributeDesc[0].offset = helper::GetOffsetOf(&utils::Vertex::position);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk = {0};

            vertexAttributeDesc[1].format = nri::Format::RG16_SFLOAT;
            vertexAttributeDesc[1].offset = helper::GetOffsetOf(&utils::Vertex::uv);
            vertexAttributeDesc[1].d3d = {"TEXCOORD", 0};
            vertexAttributeDesc[1].vk = {1};

            vertexAttributeDesc[2].format = nri::Format::R10_G10_B10_A2_UNORM;
            vertexAttributeDesc[2].offset = helper::GetOffsetOf(&utils::Vertex::normal);
            vertexAttributeDesc[2].d3d = {"NORMAL", 0};
            vertexAttributeDesc[2].vk = {2};

            vertexAttributeDesc[3].format = nri::Format::R10_G10_B10_A2_UNORM;
            vertexAttributeDesc[3].offset = helper::GetOffsetOf(&utils::Vertex::tangent);
            vertexAttributeDesc[3].d3d = {"TANGENT", 0};
            vertexAttributeDesc[3].vk = {3};
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
        rasterizationDesc.frontCounterClockwise = true;

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = swapChainFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.color = &colorAttachmentDesc;
        outputMergerDesc.depthStencilFormat = m_DepthFormat;
        outputMergerDesc.depth.write = true;
        outputMergerDesc.depth.compareFunc = CLEAR_DEPTH == 1.0f ? nri::CompareFunc::LESS : nri::CompareFunc::GREATER;

        nri::ShaderDesc shaderStages[] =
        {
            utils::LoadShader(deviceDesc.graphicsAPI, "Forward.vs", shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, "Forward.fs", shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
        graphicsPipelineDesc.inputAssembly = &inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = &rasterizationDesc;
        graphicsPipelineDesc.outputMerger = &outputMergerDesc;
        graphicsPipelineDesc.shaders = shaderStages;
        graphicsPipelineDesc.shaderNum = helper::GetCountOf(shaderStages);

        nri::Pipeline* pipeline;

        { // Opaque
            NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, pipeline) );
            m_Pipelines.push_back(pipeline);
        }

        { // Alpha opaque
            shaderStages[1] = utils::LoadShader(deviceDesc.graphicsAPI, "ForwardDiscard.fs", shaderCodeStorage);

            rasterizationDesc.cullMode = nri::CullMode::NONE;
            outputMergerDesc.depth.write = true;
            colorAttachmentDesc.blendEnabled = false;
            NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, pipeline) );
            m_Pipelines.push_back(pipeline);
        }

        shaderStages[1] = utils::LoadShader(deviceDesc.graphicsAPI, "ForwardTransparent.fs", shaderCodeStorage);

        { // Transparent
            rasterizationDesc.cullMode = nri::CullMode::NONE;
            outputMergerDesc.depth.write = false;
            colorAttachmentDesc.blendEnabled = true;
            colorAttachmentDesc.colorBlend = {nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFunc::ADD};
            NRI_ABORT_ON_FAILURE( NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, pipeline) );
            m_Pipelines.push_back(pipeline);
        }
    }

    // Scene
    std::string sceneFile = utils::GetFullPath(m_SceneFile, utils::DataFolder::SCENES);
    NRI_ABORT_ON_FALSE( utils::LoadScene(sceneFile, m_Scene, false) );

    // Camera
    m_Camera.Initialize(m_Scene.aabb.GetCenter(), m_Scene.aabb.vMin, false);

    const uint32_t textureNum = (uint32_t)m_Scene.textures.size();
    const uint32_t materialNum = (uint32_t)m_Scene.materials.size();

    { // Textures
        for (const utils::Texture* textureData : m_Scene.textures)
        {
            nri::TextureDesc textureDesc = nri::Texture2D(textureData->GetFormat(),
                textureData->GetWidth(), textureData->GetHeight(), textureData->GetMipNum(), textureData->GetArraySize());

            nri::Texture* texture;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture(*m_Device, textureDesc, texture) );
            m_Textures.push_back(texture);
        }
    }

    // Depth attachment
    nri::Texture* depthTexture;
    {
        nri::TextureDesc textureDesc = nri::Texture2D(m_DepthFormat, (uint16_t)GetWindowResolution().x, (uint16_t)GetWindowResolution().y, 1, 1,
            nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT);

        NRI_ABORT_ON_FAILURE( NRI.CreateTexture(*m_Device, textureDesc, depthTexture) );
        m_Textures.push_back(depthTexture);
    }

    const uint32_t constantBufferSize = helper::Align((uint32_t)sizeof(GlobalConstantBufferLayout), deviceDesc.constantBufferOffsetAlignment);

    { // Buffers
        // Constant buffer
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = constantBufferSize * BUFFERED_FRAME_MAX_NUM;
        bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;
        nri::Buffer* buffer;
        NRI_ABORT_ON_FAILURE( NRI.CreateBuffer(*m_Device, bufferDesc, buffer) );
        m_Buffers.push_back(buffer);

        // Index buffer
        bufferDesc.size = helper::GetByteSizeOf(m_Scene.indices);
        bufferDesc.usageMask = nri::BufferUsageBits::INDEX_BUFFER;
        NRI_ABORT_ON_FAILURE( NRI.CreateBuffer(*m_Device, bufferDesc, buffer) );
        m_Buffers.push_back(buffer);

        // Vertex buffer
        bufferDesc.size = helper::GetByteSizeOf(m_Scene.vertices);
        bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER;
        NRI_ABORT_ON_FAILURE( NRI.CreateBuffer(*m_Device, bufferDesc, buffer) );
        m_Buffers.push_back(buffer);
    }

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_Buffers[0];

    size_t baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + 1, nullptr);
    NRI_ABORT_ON_FAILURE( NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation) );

    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 2;
    resourceGroupDesc.buffers = &m_Buffers[1];
    resourceGroupDesc.textureNum = (uint32_t)m_Textures.size();
    resourceGroupDesc.textures = m_Textures.data();

    baseAllocation = m_MemoryAllocations.size();
    m_MemoryAllocations.resize(baseAllocation + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE( NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + baseAllocation) );

    // Create descriptors
    nri::Descriptor* anisotropicSampler;
    nri::Descriptor* constantBufferViews[BUFFERED_FRAME_MAX_NUM];
    {
        // Material textures
        m_Descriptors.resize(textureNum);
        for (uint32_t i = 0; i < textureNum; i++)
        {
            const utils::Texture& texture = *m_Scene.textures[i];

            nri::Texture2DViewDesc texture2DViewDesc = {m_Textures[i], nri::Texture2DViewType::SHADER_RESOURCE_2D, texture.GetFormat()};
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(texture2DViewDesc, m_Descriptors[i]) );
        }

        // Sampler
        nri::SamplerDesc samplerDesc = {};
        samplerDesc.addressModes = {nri::AddressMode::REPEAT, nri::AddressMode::REPEAT};
        samplerDesc.filters = {nri::Filter::LINEAR, nri::Filter::LINEAR, nri::Filter::LINEAR};
        samplerDesc.anisotropy = 8;
        samplerDesc.mipMax = 16.0f;
        NRI_ABORT_ON_FAILURE( NRI.CreateSampler(*m_Device, samplerDesc, anisotropicSampler) );
        m_Descriptors.push_back(anisotropicSampler);

        // Constant buffer
        for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
        {
            m_Frames[i].globalConstantBufferViewOffsets = i * constantBufferSize;

            nri::BufferViewDesc bufferViewDesc = {};
            bufferViewDesc.buffer = m_Buffers[CONSTANT_BUFFER];
            bufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
            bufferViewDesc.offset = i * constantBufferSize;
            bufferViewDesc.size = constantBufferSize;
            NRI_ABORT_ON_FAILURE( NRI.CreateBufferView(bufferViewDesc, constantBufferViews[i]) );
            m_Descriptors.push_back(constantBufferViews[i]);
        }

        // Depth buffer
        nri::Texture2DViewDesc texture2DViewDesc = {depthTexture, nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT, m_DepthFormat};

        NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(texture2DViewDesc, m_DepthAttachment) );
        m_Descriptors.push_back(m_DepthAttachment);

        // Swap chain
        for (uint32_t i = 0; i < swapChainTextureNum; i++)
        {
            nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat};

            nri::Descriptor* colorAttachment;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(textureViewDesc, colorAttachment) );

            const BackBuffer backBuffer = { colorAttachment, swapChainTextures[i] };
            m_SwapChainBuffers.push_back(backBuffer);
        }
    }

    { // Descriptor pool
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = materialNum + BUFFERED_FRAME_MAX_NUM;
        descriptorPoolDesc.textureMaxNum = materialNum * TEXTURES_PER_MATERIAL;
        descriptorPoolDesc.samplerMaxNum = BUFFERED_FRAME_MAX_NUM;
        descriptorPoolDesc.constantBufferMaxNum = BUFFERED_FRAME_MAX_NUM;

        NRI_ABORT_ON_FAILURE( NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool) );
    }

    { // Descriptor sets
        m_DescriptorSets.resize(BUFFERED_FRAME_MAX_NUM + materialNum);

        // Global
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, GLOBAL_DESCRIPTOR_SET,
            &m_DescriptorSets[0], BUFFERED_FRAME_MAX_NUM, nri::ALL_NODES, 0) );

        for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
        {
            nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs[2] = {};
            descriptorRangeUpdateDescs[0].descriptorNum = 1;
            descriptorRangeUpdateDescs[0].descriptors = &constantBufferViews[i];
            descriptorRangeUpdateDescs[1].descriptorNum = 1;
            descriptorRangeUpdateDescs[1].descriptors = &anisotropicSampler;

            NRI.UpdateDescriptorRanges(*m_DescriptorSets[i], nri::ALL_NODES, 0, helper::GetCountOf(descriptorRangeUpdateDescs), descriptorRangeUpdateDescs);
        }

        // Material
        NRI_ABORT_ON_FAILURE( NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, MATERIAL_DESCRIPTOR_SET,
            &m_DescriptorSets[BUFFERED_FRAME_MAX_NUM], materialNum, nri::ALL_NODES, 0) );

        for (uint32_t i = 0; i < materialNum; i++)
        {
            const utils::Material& material = m_Scene.materials[i];

            nri::Descriptor* materialTextures[TEXTURES_PER_MATERIAL] =
            {
                m_Descriptors[material.baseColorTexIndex],
                m_Descriptors[material.roughnessMetalnessTexIndex],
                m_Descriptors[material.normalTexIndex],
                m_Descriptors[material.emissiveTexIndex],
            };

            nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs = {};
            descriptorRangeUpdateDescs.descriptorNum = helper::GetCountOf(materialTextures);
            descriptorRangeUpdateDescs.descriptors = materialTextures;
            NRI.UpdateDescriptorRanges(*m_DescriptorSets[BUFFERED_FRAME_MAX_NUM + i], nri::ALL_NODES, 0, 1, &descriptorRangeUpdateDescs);
        }
    }

    { // Upload data
        std::vector<nri::TextureUploadDesc> textureData(1 + textureNum);

        uint32_t subresourceNum = 0;
        for (uint32_t i = 0; i < textureNum; i++)
        {
            const utils::Texture& texture = *m_Scene.textures[i];
            subresourceNum += texture.GetArraySize() * texture.GetMipNum();
        }

        std::vector<nri::TextureSubresourceUploadDesc> subresources(subresourceNum);
        nri::TextureSubresourceUploadDesc* subresourceBegin = subresources.data();

        textureData[0] = {};
        textureData[0].subresources = nullptr;
        textureData[0].texture = depthTexture;
        textureData[0].after = {nri::AccessBits::DEPTH_STENCIL_WRITE, nri::Layout::DEPTH_STENCIL};

        for (uint32_t i = 0; i < textureNum; i++)
        {
            const utils::Texture& texture = *m_Scene.textures[i];

            for (uint32_t slice = 0; slice < texture.GetArraySize(); slice++)
            {
                for (uint32_t mip = 0; mip < texture.GetMipNum(); mip++)
                    texture.GetSubresource(subresourceBegin[slice * texture.GetMipNum() + mip], mip, slice);
            }

            const uint32_t j = i + 1;
            textureData[j] = {};
            textureData[j].subresources = subresourceBegin;
            textureData[j].mipNum = texture.GetMipNum();
            textureData[j].arraySize = texture.GetArraySize();
            textureData[j].texture = m_Textures[i];
            textureData[j].after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE};

            subresourceBegin += texture.GetArraySize() * texture.GetMipNum();
        }

        nri::BufferUploadDesc bufferData[] =
        {
            {m_Scene.vertices.data(), helper::GetByteSizeOf(m_Scene.vertices), m_Buffers[VERTEX_BUFFER], 0, nri::AccessBits::UNKNOWN, nri::AccessBits::VERTEX_BUFFER},
            {m_Scene.indices.data(), helper::GetByteSizeOf(m_Scene.indices), m_Buffers[INDEX_BUFFER], 0, nri::AccessBits::UNKNOWN, nri::AccessBits::INDEX_BUFFER},
        };



        NRI_ABORT_ON_FAILURE( NRI.UploadData(*m_CommandQueue, textureData.data(), (uint32_t)textureData.size(), bufferData, helper::GetCountOf(bufferData)) );
    }

    m_Scene.UnloadGeometryData();
    m_Scene.UnloadTextureData();

    return true;
}

void Sample::PrepareFrame(uint32_t frameIndex)
{
    CameraDesc desc = {};
    desc.aspectRatio = float( GetWindowResolution().x ) / float( GetWindowResolution().y );
    desc.horizontalFov = 90.0f;
    desc.nearZ = 0.1f;
    desc.isReversedZ = (CLEAR_DEPTH == 0.0f);
    GetCameraDescFromInputDevices(desc);

    m_Camera.Update(desc, frameIndex);
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
    const Frame& frame = m_Frames[bufferedFrameIndex];
    const uint32_t windowWidth = GetWindowResolution().x;
    const uint32_t windowHeight = GetWindowResolution().y;

    if (frameIndex >= BUFFERED_FRAME_MAX_NUM)
    {
        NRI.Wait(*m_FrameFence, 1 + frameIndex - BUFFERED_FRAME_MAX_NUM);
        NRI.ResetCommandAllocator(*frame.commandAllocator);
    }

    const uint32_t currentTextureIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain);
    BackBuffer& currentBackBuffer = m_SwapChainBuffers[currentTextureIndex];

    // Update constants
    const uint64_t rangeOffset = m_Frames[bufferedFrameIndex].globalConstantBufferViewOffsets;
    auto constants = (GlobalConstantBufferLayout*)NRI.MapBuffer(*m_Buffers[CONSTANT_BUFFER], rangeOffset, sizeof(GlobalConstantBufferLayout));
    if (constants)
    {
        constants->gWorldToClip = m_Camera.state.mWorldToClip * m_Scene.mSceneToWorld;
        constants->gCameraPos = m_Camera.state.position;

        NRI.UnmapBuffer(*m_Buffers[CONSTANT_BUFFER]);
    }

    // Render
    NRI.ResetCommandAllocator(*frame.commandAllocator);

    nri::CommandBuffer& commandBuffer = *frame.commandBuffer;
    NRI.BeginCommandBuffer(commandBuffer, m_DescriptorPool, 0);
    {
        helper::Annotation annotation(NRI, commandBuffer, "Scene");

        nri::TextureBarrierDesc textureBarrierDescs = {};
        textureBarrierDescs.texture = currentBackBuffer.texture;
        textureBarrierDescs.after = {nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT};
        textureBarrierDescs.arraySize = 1;
        textureBarrierDescs.mipNum = 1;

        nri::BarrierGroupDesc barrierGroupDesc = {};
        barrierGroupDesc.textureNum = 1;
        barrierGroupDesc.textures = &textureBarrierDescs;
        NRI.CmdBarrier(commandBuffer, barrierGroupDesc);

        nri::AttachmentsDesc attachmentsDesc = {};
        attachmentsDesc.colorNum = 1;
        attachmentsDesc.colors = &currentBackBuffer.colorAttachment;
        attachmentsDesc.depthStencil = m_DepthAttachment;

        NRI.CmdBeginRendering(commandBuffer, attachmentsDesc);
        {
            nri::ClearDesc clearDescs[2] = {};
            clearDescs[0].attachmentContentType = nri::AttachmentContentType::COLOR;
            clearDescs[0].value.color32f = {0.0f, 0.63f, 1.0f};
            clearDescs[1].attachmentContentType = nri::AttachmentContentType::DEPTH;
            clearDescs[1].value.depthStencil.depth = CLEAR_DEPTH;

            NRI.CmdClearAttachments(commandBuffer, clearDescs, helper::GetCountOf(clearDescs), nullptr, 0);

            const nri::Viewport viewport = { 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f };
            NRI.CmdSetViewports(commandBuffer,  &viewport, 1);

            const nri::Rect scissor = { 0, 0, (nri::Dim_t)windowWidth, (nri::Dim_t)windowHeight };
            NRI.CmdSetScissors(commandBuffer,  &scissor, 1);

            NRI.CmdSetIndexBuffer(commandBuffer, *m_Buffers[INDEX_BUFFER], 0, sizeof(utils::Index) == 2 ? nri::IndexType::UINT16 : nri::IndexType::UINT32);

            NRI.CmdSetPipelineLayout(commandBuffer, *m_PipelineLayout);
            NRI.CmdSetDescriptorSet(commandBuffer, GLOBAL_DESCRIPTOR_SET, *m_DescriptorSets[bufferedFrameIndex], nullptr);

            // TODO: no sorting per pipeline / material, transparency is not last
            for (const utils::Instance& instance :m_Scene.instances)
            {
                const utils::Material& material = m_Scene.materials[instance.materialIndex];
                uint32_t pipelineIndex = material.IsAlphaOpaque() ? 1 : (material.IsTransparent() ? 2 : 0);
                NRI.CmdSetPipeline(commandBuffer, *m_Pipelines[pipelineIndex]);

                constexpr uint64_t offset = 0;
                NRI.CmdSetVertexBuffers(commandBuffer, 0, 1, &m_Buffers[VERTEX_BUFFER], &offset);

                nri::DescriptorSet* descriptorSet = m_DescriptorSets[BUFFERED_FRAME_MAX_NUM + instance.materialIndex];
                NRI.CmdSetDescriptorSet(commandBuffer, MATERIAL_DESCRIPTOR_SET, *descriptorSet, nullptr);

                const utils::Mesh& mesh = m_Scene.meshes[instance.meshInstanceIndex];
                NRI.CmdDrawIndexed(commandBuffer, mesh.indexNum, 1, mesh.indexOffset, mesh.vertexOffset, 0);
            }
        }
        NRI.CmdEndRendering(commandBuffer);

        textureBarrierDescs.before = textureBarrierDescs.after;
        textureBarrierDescs.after = {nri::AccessBits::UNKNOWN, nri::Layout::PRESENT};

        NRI.CmdBarrier(commandBuffer, barrierGroupDesc);
    }
    NRI.EndCommandBuffer(commandBuffer);

    nri::QueueSubmitDesc queueSubmitDesc = {};
    queueSubmitDesc.commandBuffers = &frame.commandBuffer;
    queueSubmitDesc.commandBufferNum = 1;
    NRI.QueueSubmit(*m_CommandQueue, queueSubmitDesc);

    NRI.SwapChainPresent(*m_SwapChain);

    NRI.QueueSignal(*m_CommandQueue, *m_FrameFence, 1 + frameIndex);
}

SAMPLE_MAIN(Sample, 0);
