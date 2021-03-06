/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "SampleBase.h"
#include "Extensions/NRIWrapperD3D11.h"
#include "Extensions/NRIWrapperD3D12.h"
#include "Extensions/NRIWrapperVK.h"

#if defined(_WIN32)
#include <d3d11.h>
#include <d3d12.h>
#define VK_USE_PLATFORM_WIN32_KHR 1
#else
#define VK_USE_PLATFORM_XLIB_KHR 1
#endif

#define VK_NO_PROTOTYPES 1
#include "vulkan/vulkan.h"

struct Library;
Library* LoadSharedLibrary(const char* path);
void* GetSharedLibraryFunction(Library& library, const char* name);
void UnloadSharedLibrary(Library& library);

#if defined(_WIN32)
const char* VULKAN_LOADER_NAME = "vulkan-1.dll";
#else
const char* VULKAN_LOADER_NAME = "libvulkan.so";
#endif

constexpr nri::Color<float> COLOR_0 = { 1.0f, 1.0f, 0.0f, 1.0f };
constexpr nri::Color<float> COLOR_1 = { 0.46f, 0.72f, 0.0f, 1.0f };

struct ConstantBufferLayout
{
    float color[3];
    float scale;
};

struct Vertex
{
    float position[2];
    float uv[2];
};

static const Vertex g_VertexData[] =
{
    {-0.71f, -0.50f, 0.0f, 0.0f},
    {0.00f,  0.71f, 1.0f, 1.0f},
    {0.71f, -0.50f, 0.0f, 1.0f}
};

static const uint16_t g_IndexData[] = { 0, 1, 2 };

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{
};

struct Frame
{
    nri::DeviceSemaphore* deviceSemaphore;
    nri::CommandAllocator* commandAllocator;
    nri::CommandBuffer* commandBuffer;
    nri::Descriptor* constantBufferView;
    nri::DescriptorSet* constantBufferDescriptorSet;
    uint64_t constantBufferViewOffset;
};

class Sample : public SampleBase
{
public:

    Sample()
    {
    }

    ~Sample();

    bool Initialize(nri::GraphicsAPI graphicsAPI);
    void PrepareFrame(uint32_t frameIndex);
    void RenderFrame(uint32_t frameIndex);

private:
    void CreateD3D11Device();
    void CreateD3D12Device();
    void CreateVulkanDevice();

    NRIInterface NRI = {};
    nri::Device* m_Device = {};
    nri::SwapChain* m_SwapChain = {};
    nri::CommandQueue* m_CommandQueue = {};
    nri::QueueSemaphore* m_AcquireSemaphore = nullptr;
    nri::QueueSemaphore* m_ReleaseSemaphore = nullptr;
    nri::DescriptorPool* m_DescriptorPool = {};
    nri::PipelineLayout* m_PipelineLayout = {};
    nri::Pipeline* m_Pipeline = {};
    nri::DescriptorSet* m_TextureDescriptorSet = {};
    nri::Descriptor* m_TextureShaderResource = {};
    nri::Descriptor* m_Sampler = {};
    nri::Buffer* m_ConstantBuffer = {};
    nri::Buffer* m_GeometryBuffer = {};
    nri::Texture* m_Texture = {};

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
    std::vector<BackBuffer> m_SwapChainBuffers;
    std::vector<nri::Memory*> m_MemoryAllocations;

    ID3D11Device* m_D3D11Device = nullptr;
    ID3D12Device* m_D3D12Device = nullptr;
    VkInstance m_VKInstance = VK_NULL_HANDLE;
    VkDevice m_VKDevice = VK_NULL_HANDLE;
    Library* m_VulkanLoader = nullptr;

    uint64_t m_GeometryOffset = 0;
    float m_Transparency = 1.0f;
    float m_Scale = 1.0f;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (Frame& frame : m_Frames)
    {
        NRI.DestroyCommandBuffer(*frame.commandBuffer);
        NRI.DestroyCommandAllocator(*frame.commandAllocator);
        NRI.DestroyDeviceSemaphore(*frame.deviceSemaphore);
        NRI.DestroyDescriptor(*frame.constantBufferView);
    }

    for (BackBuffer& backBuffer : m_SwapChainBuffers)
    {
        NRI.DestroyFrameBuffer(*backBuffer.frameBuffer);
        NRI.DestroyDescriptor(*backBuffer.colorAttachment);
    }

    NRI.DestroyPipeline(*m_Pipeline);
    NRI.DestroyPipelineLayout(*m_PipelineLayout);
    NRI.DestroyDescriptor(*m_TextureShaderResource);
    NRI.DestroyDescriptor(*m_Sampler);
    NRI.DestroyBuffer(*m_ConstantBuffer);
    NRI.DestroyBuffer(*m_GeometryBuffer);
    NRI.DestroyTexture(*m_Texture);
    NRI.DestroyDescriptorPool(*m_DescriptorPool);
    NRI.DestroyQueueSemaphore(*m_AcquireSemaphore);
    NRI.DestroyQueueSemaphore(*m_ReleaseSemaphore);
    NRI.DestroySwapChain(*m_SwapChain);

    for (nri::Memory* memory : m_MemoryAllocations)
        NRI.FreeMemory(*memory);

    DestroyUserInterface();

    nri::DestroyDevice(*m_Device);

    if (m_VulkanLoader != nullptr)
    {
        auto vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetSharedLibraryFunction(*m_VulkanLoader, "vkGetInstanceProcAddr");
        auto vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(m_VKInstance, "vkDestroyInstance");
        auto vkDestroyDevice = (PFN_vkDestroyDevice)vkGetInstanceProcAddr(m_VKInstance, "vkDestroyDevice");

        vkDestroyDevice(m_VKDevice, nullptr);
        vkDestroyInstance(m_VKInstance, nullptr);
        UnloadSharedLibrary(*m_VulkanLoader);
    }

    if (m_D3D11Device != nullptr)
        m_D3D11Device->Release();

    if (m_D3D12Device != nullptr)
        m_D3D12Device->Release();
}

void Sample::CreateD3D11Device()
{
    ID3D11DeviceContext* d3d11ImmediateContext = nullptr;

    const HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &m_D3D11Device,
        nullptr, &d3d11ImmediateContext);

    NRI_ABORT_ON_FALSE(SUCCEEDED(result));

    d3d11ImmediateContext->Release();

    nri::DeviceCreationD3D11Desc deviceDesc = {};
    deviceDesc.d3d11Device = m_D3D11Device;
    deviceDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    deviceDesc.enableAPIValidation = m_DebugAPI;
    deviceDesc.enableNRIValidation = m_DebugNRI;

    NRI_ABORT_ON_FAILURE(nri::CreateDeviceFromD3D11Device(deviceDesc, m_Device));
}

void Sample::CreateD3D12Device()
{
    const HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(m_D3D12Device), (void**)&m_D3D12Device);

    NRI_ABORT_ON_FALSE(SUCCEEDED(result));

    nri::DeviceCreationD3D12Desc deviceDesc = {};
    deviceDesc.d3d12Device = m_D3D12Device;
    deviceDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    deviceDesc.enableAPIValidation = m_DebugAPI;
    deviceDesc.enableNRIValidation = m_DebugNRI;

    NRI_ABORT_ON_FAILURE(nri::CreateDeviceFromD3D12Device(deviceDesc, m_Device));
}

void Sample::CreateVulkanDevice()
{
    m_VulkanLoader = LoadSharedLibrary(VULKAN_LOADER_NAME);

    auto vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetSharedLibraryFunction(*m_VulkanLoader, "vkGetInstanceProcAddr");
    auto vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");

    VkApplicationInfo applicationInfo = {};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.apiVersion = VK_API_VERSION_1_2;

#if defined(_WIN32)
    const char* instanceExtensions[] = { VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME };
#else
    const char* instanceExtensions[] = { VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
#endif

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &applicationInfo;
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions;
    instanceCreateInfo.enabledExtensionCount = helper::GetCountOf(instanceExtensions);
    instanceCreateInfo.ppEnabledLayerNames = layers;
    instanceCreateInfo.enabledLayerCount = m_DebugAPI ? 1 : 0;

    VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &m_VKInstance);
    NRI_ABORT_ON_FALSE(result == VK_SUCCESS);

    auto vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(m_VKInstance, "vkEnumeratePhysicalDevices");
    auto vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)vkGetInstanceProcAddr(m_VKInstance, "vkGetPhysicalDeviceFeatures");
    auto vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(m_VKInstance, "vkCreateDevice");

    uint32_t physicalDeviceNum = 0;
    vkEnumeratePhysicalDevices(m_VKInstance, &physicalDeviceNum, nullptr);

    NRI_ABORT_ON_FALSE(physicalDeviceNum != 0);

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceNum);
    vkEnumeratePhysicalDevices(m_VKInstance, &physicalDeviceNum, physicalDevices.data());

    VkPhysicalDevice physicalDevice = physicalDevices[0];

    VkPhysicalDeviceFeatures supportedFeatures = {};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

    VkPhysicalDeviceFeatures enabledFeatures = supportedFeatures;

    uint32_t queueFamilyIndices[1] = {};

    const float priority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.pQueuePriorities = &priority;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndices[0];

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pEnabledFeatures = &enabledFeatures;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.enabledExtensionCount = helper::GetCountOf(deviceExtensions);
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &m_VKDevice);
    NRI_ABORT_ON_FALSE(result == VK_SUCCESS);

    nri::DeviceCreationVulkanDesc deviceDesc = {};
    deviceDesc.vkInstance = (nri::NRIVkInstance)m_VKInstance;
    deviceDesc.vkPhysicalDevices = (nri::NRIVkPhysicalDevice*)&physicalDevice;
    deviceDesc.deviceGroupSize = 1;
    deviceDesc.queueFamilyIndices = queueFamilyIndices;
    deviceDesc.queueFamilyIndexNum = helper::GetCountOf(queueFamilyIndices);
    deviceDesc.vkDevice = (nri::NRIVkDevice)m_VKDevice;
    deviceDesc.memoryAllocatorInterface = m_MemoryAllocatorInterface;
    deviceDesc.enableAPIValidation = m_DebugAPI;
    deviceDesc.enableNRIValidation = m_DebugNRI;

    NRI_ABORT_ON_FAILURE(nri::CreateDeviceFromVkDevice(deviceDesc, m_Device));

    nri::WrapperSPIRVOffsetsInterface wrapperSPIRVOffsetsInterface = {};
    NRI_ABORT_ON_FAILURE(nri::GetInterface(*m_Device, NRI_INTERFACE(nri::WrapperSPIRVOffsetsInterface), &wrapperSPIRVOffsetsInterface));

    wrapperSPIRVOffsetsInterface.SetSPIRVBindingOffsets(*m_Device, SPIRV_BINDING_OFFSETS);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    switch (graphicsAPI)
    {
    case nri::GraphicsAPI::D3D11:
        CreateD3D11Device();
        break;
    case nri::GraphicsAPI::D3D12:
        CreateD3D12Device();
        break;
    case nri::GraphicsAPI::VULKAN:
        CreateVulkanDevice();
        break;
    }

    // NRI
    NRI_ABORT_ON_FAILURE(nri::GetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&NRI));
    NRI_ABORT_ON_FAILURE(nri::GetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI));
    NRI_ABORT_ON_FAILURE(nri::GetInterface(*m_Device, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI));

    // Command queue
    NRI_ABORT_ON_FAILURE(NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue));

    // Swap chain
    nri::Format swapChainFormat;
    {
        nri::SwapChainDesc swapChainDesc = {};
        swapChainDesc.windowSystemType = GetWindowSystemType();
        swapChainDesc.window = GetWindow();
        swapChainDesc.commandQueue = m_CommandQueue;
        swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
        swapChainDesc.verticalSyncInterval = m_SwapInterval;
        swapChainDesc.width = GetWindowWidth();
        swapChainDesc.height = GetWindowHeight();
        swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
        NRI_ABORT_ON_FAILURE(NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain));

        uint32_t swapChainTextureNum;
        nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum, swapChainFormat);

        for (uint32_t i = 0; i < swapChainTextureNum; i++)
        {
            nri::Texture2DViewDesc textureViewDesc = { swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat };

            nri::Descriptor* colorAttachment;
            NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(textureViewDesc, colorAttachment));

            nri::ClearValueDesc clearColor = {};
            clearColor.rgba32f = COLOR_0;

            nri::FrameBufferDesc frameBufferDesc = {};
            frameBufferDesc.colorAttachmentNum = 1;
            frameBufferDesc.colorAttachments = &colorAttachment;
            frameBufferDesc.colorClearValues = &clearColor;
            nri::FrameBuffer* frameBuffer;
            NRI_ABORT_ON_FAILURE(NRI.CreateFrameBuffer(*m_Device, frameBufferDesc, frameBuffer));

            const BackBuffer backBuffer = { frameBuffer, frameBuffer, colorAttachment, swapChainTextures[i] };
            m_SwapChainBuffers.push_back(backBuffer);
        }
    }

    NRI_ABORT_ON_FAILURE(NRI.CreateQueueSemaphore(*m_Device, m_AcquireSemaphore));
    NRI_ABORT_ON_FAILURE(NRI.CreateQueueSemaphore(*m_Device, m_ReleaseSemaphore));

    // Buffered resources
    for (Frame& frame : m_Frames)
    {
        NRI_ABORT_ON_FAILURE(NRI.CreateDeviceSemaphore(*m_Device, true, frame.deviceSemaphore));
        NRI_ABORT_ON_FAILURE(NRI.CreateCommandAllocator(*m_CommandQueue, nri::WHOLE_DEVICE_GROUP, frame.commandAllocator));
        NRI_ABORT_ON_FAILURE(NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffer));
    }

    // Pipeline
    const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);
    utils::ShaderCodeStorage shaderCodeStorage;
    {
        nri::DescriptorRangeDesc descriptorRangeConstant[1];
        descriptorRangeConstant[0] = { 0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::ShaderStage::ALL };

        nri::DescriptorRangeDesc descriptorRangeTexture[2];
        descriptorRangeTexture[0] = { 0, 1, nri::DescriptorType::TEXTURE, nri::ShaderStage::FRAGMENT };
        descriptorRangeTexture[1] = { 0, 1, nri::DescriptorType::SAMPLER, nri::ShaderStage::FRAGMENT };

        nri::DescriptorSetDesc descriptorSetDescs[] =
        {
            {descriptorRangeConstant, helper::GetCountOf(descriptorRangeConstant)},
            {descriptorRangeTexture, helper::GetCountOf(descriptorRangeTexture)},
        };

        nri::PushConstantDesc pushConstant = { 1, sizeof(float), nri::ShaderStage::FRAGMENT };

        nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        pipelineLayoutDesc.descriptorSetNum = helper::GetCountOf(descriptorSetDescs);
        pipelineLayoutDesc.descriptorSets = descriptorSetDescs;
        pipelineLayoutDesc.pushConstantNum = 1;
        pipelineLayoutDesc.pushConstants = &pushConstant;
        pipelineLayoutDesc.stageMask = nri::PipelineLayoutShaderStageBits::VERTEX | nri::PipelineLayoutShaderStageBits::FRAGMENT;

        NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc, m_PipelineLayout));

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;
        vertexStreamDesc.stride = sizeof(Vertex);

        nri::VertexAttributeDesc vertexAttributeDesc[2] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[0].streamIndex = 0;
            vertexAttributeDesc[0].offset = helper::GetOffsetOf(&Vertex::position);
            vertexAttributeDesc[0].d3d = { "POSITION", 0 };
            vertexAttributeDesc[0].vk.location = { 0 };

            vertexAttributeDesc[1].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[1].streamIndex = 0;
            vertexAttributeDesc[1].offset = helper::GetOffsetOf(&Vertex::uv);
            vertexAttributeDesc[1].d3d = { "TEXCOORD", 0 };
            vertexAttributeDesc[1].vk.location = { 1 };
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
        colorAttachmentDesc.blendEnabled = true;
        colorAttachmentDesc.colorBlend = { nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendFunc::ADD };

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.color = &colorAttachmentDesc;

        nri::ShaderDesc shaderStages[] =
        {
            utils::LoadShader(deviceDesc.graphicsAPI, "01_Triangle.vs", shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, "01_Triangle.fs", shaderCodeStorage),
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

    // Descriptor pool
    {
        nri::DescriptorPoolDesc descriptorPoolDesc = {};
        descriptorPoolDesc.descriptorSetMaxNum = BUFFERED_FRAME_MAX_NUM + 1;
        descriptorPoolDesc.constantBufferMaxNum = BUFFERED_FRAME_MAX_NUM;
        descriptorPoolDesc.textureMaxNum = 1;
        descriptorPoolDesc.samplerMaxNum = 1;

        NRI_ABORT_ON_FAILURE(NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc, m_DescriptorPool));
    }

    // Load texture
    utils::Texture texture;
    std::string path = utils::GetFullPath("wood.dds", utils::DataFolder::TEXTURES);
    if (!utils::LoadTexture(path, texture))
        return false;

    // Resources
    const uint32_t constantBufferSize = helper::Align((uint32_t)sizeof(ConstantBufferLayout), deviceDesc.constantBufferOffsetAlignment);
    const uint64_t indexDataSize = sizeof(g_IndexData);
    const uint64_t indexDataAlignedSize = helper::Align(indexDataSize, 16);
    const uint64_t vertexDataSize = sizeof(g_VertexData);
    {
        // Texture
        nri::CTextureDesc textureDesc = nri::CTextureDesc::Texture2D(texture.GetFormat(),
            texture.GetWidth(), texture.GetHeight(), texture.GetMipNum());
        NRI_ABORT_ON_FAILURE(NRI.CreateTexture(*m_Device, textureDesc, m_Texture));

        // Constant buffer
        {
            nri::BufferDesc bufferDesc = {};
            bufferDesc.size = constantBufferSize * BUFFERED_FRAME_MAX_NUM;
            bufferDesc.usageMask = nri::BufferUsageBits::CONSTANT_BUFFER;
            NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_ConstantBuffer));
        }

        // Geometry buffer
        {
            nri::BufferDesc bufferDesc = {};
            bufferDesc.size = indexDataAlignedSize + vertexDataSize;
            bufferDesc.usageMask = nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER;
            NRI_ABORT_ON_FAILURE(NRI.CreateBuffer(*m_Device, bufferDesc, m_GeometryBuffer));
        }
        m_GeometryOffset = indexDataAlignedSize;
    }

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_ConstantBuffer;

    m_MemoryAllocations.resize(1, nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data()));

    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.bufferNum = 1;
    resourceGroupDesc.buffers = &m_GeometryBuffer;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &m_Texture;

    m_MemoryAllocations.resize(1 + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
    NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + 1));

    // Descriptors
    {
        // Texture
        nri::Texture2DViewDesc texture2DViewDesc = { m_Texture, nri::Texture2DViewType::SHADER_RESOURCE_2D, texture.GetFormat() };
        NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(texture2DViewDesc, m_TextureShaderResource));

        // Sampler
        nri::SamplerDesc samplerDesc = {};
        samplerDesc.anisotropy = 4;
        samplerDesc.addressModes = { nri::AddressMode::MIRRORED_REPEAT, nri::AddressMode::MIRRORED_REPEAT };
        samplerDesc.minification = nri::Filter::LINEAR;
        samplerDesc.magnification = nri::Filter::LINEAR;
        samplerDesc.mip = nri::Filter::LINEAR;
        samplerDesc.mipMax = 16.0f;
        NRI_ABORT_ON_FAILURE(NRI.CreateSampler(*m_Device, samplerDesc, m_Sampler));

        // Constant buffer
        for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++)
        {
            nri::BufferViewDesc bufferViewDesc = {};
            bufferViewDesc.buffer = m_ConstantBuffer;
            bufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
            bufferViewDesc.offset = i * constantBufferSize;
            bufferViewDesc.size = constantBufferSize;
            NRI_ABORT_ON_FAILURE(NRI.CreateBufferView(bufferViewDesc, m_Frames[i].constantBufferView));

            m_Frames[i].constantBufferViewOffset = bufferViewDesc.offset;
        }
    }

    // Descriptor sets
    {
        // Texture
        NRI_ABORT_ON_FAILURE(NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 1,
            &m_TextureDescriptorSet, 1, nri::WHOLE_DEVICE_GROUP, 0));

        nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs[2] = {};
        descriptorRangeUpdateDescs[0].descriptorNum = 1;
        descriptorRangeUpdateDescs[0].descriptors = &m_TextureShaderResource;

        descriptorRangeUpdateDescs[1].descriptorNum = 1;
        descriptorRangeUpdateDescs[1].descriptors = &m_Sampler;
        NRI.UpdateDescriptorRanges(*m_TextureDescriptorSet, nri::WHOLE_DEVICE_GROUP, 0, helper::GetCountOf(descriptorRangeUpdateDescs), descriptorRangeUpdateDescs);

        // Constant buffer
        for (Frame& frame : m_Frames)
        {
            NRI_ABORT_ON_FAILURE(NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0, &frame.constantBufferDescriptorSet, 1, nri::WHOLE_DEVICE_GROUP, 0));

            nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = { &frame.constantBufferView, 1 };
            NRI.UpdateDescriptorRanges(*frame.constantBufferDescriptorSet, nri::WHOLE_DEVICE_GROUP, 0, 1, &descriptorRangeUpdateDesc);
        }
    }

    // Upload data
    {
        std::vector<uint8_t> geometryBufferData(indexDataAlignedSize + vertexDataSize);
        memcpy(&geometryBufferData[0], g_IndexData, indexDataSize);
        memcpy(&geometryBufferData[indexDataAlignedSize], g_VertexData, vertexDataSize);

        std::array<nri::TextureSubresourceUploadDesc, 16> subresources;
        for (uint32_t mip = 0; mip < texture.GetMipNum(); mip++)
            texture.GetSubresource(subresources[mip], mip);

        nri::TextureUploadDesc textureData = {};
        textureData.subresources = subresources.data();
        textureData.mipNum = texture.GetMipNum();
        textureData.arraySize = 1;
        textureData.texture = m_Texture;
        textureData.nextLayout = nri::TextureLayout::SHADER_RESOURCE;
        textureData.nextAccess = nri::AccessBits::SHADER_RESOURCE;

        nri::BufferUploadDesc bufferData = {};
        bufferData.buffer = m_GeometryBuffer;
        bufferData.data = &geometryBufferData[0];
        bufferData.dataSize = geometryBufferData.size();
        bufferData.nextAccess = nri::AccessBits::INDEX_BUFFER | nri::AccessBits::VERTEX_BUFFER;

        NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_CommandQueue, &textureData, 1, &bufferData, 1));
    }

    // User interface
    bool initialized = CreateUserInterface(*m_Device, NRI, NRI, GetWindowWidth(), GetWindowHeight(), swapChainFormat);

    return initialized;
}

void Sample::PrepareFrame(uint32_t frameIndex)
{
    PrepareUserInterface();

    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize);
    {
        ImGui::SliderFloat("Transparency", &m_Transparency, 0.0f, 1.0f);
        ImGui::SliderFloat("Scale", &m_Scale, 0.75f, 1.25f);
    }
    ImGui::End();
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    const uint32_t windowWidth = GetWindowWidth();
    const uint32_t windowHeight = GetWindowHeight();
    const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
    const Frame& frame = m_Frames[bufferedFrameIndex];

    const uint32_t currentTextureIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain, *m_AcquireSemaphore);
    BackBuffer& currentBackBuffer = m_SwapChainBuffers[currentTextureIndex];

    NRI.WaitForSemaphore(*m_CommandQueue, *frame.deviceSemaphore);
    NRI.ResetCommandAllocator(*frame.commandAllocator);

    ConstantBufferLayout* commonConstants = (ConstantBufferLayout*)NRI.MapBuffer(*m_ConstantBuffer, frame.constantBufferViewOffset, sizeof(ConstantBufferLayout));
    if (commonConstants)
    {
        commonConstants->color[0] = 0.8f;
        commonConstants->color[1] = 0.5f;
        commonConstants->color[2] = 0.1f;
        commonConstants->scale = m_Scale;

        NRI.UnmapBuffer(*m_ConstantBuffer);
    }

    nri::TextureTransitionBarrierDesc textureTransitionBarrierDesc = {};
    textureTransitionBarrierDesc.texture = currentBackBuffer.texture;
    textureTransitionBarrierDesc.prevAccess = nri::AccessBits::UNKNOWN;
    textureTransitionBarrierDesc.nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
    textureTransitionBarrierDesc.prevLayout = nri::TextureLayout::UNKNOWN;
    textureTransitionBarrierDesc.nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
    textureTransitionBarrierDesc.arraySize = 1;
    textureTransitionBarrierDesc.mipNum = 1;

    nri::CommandBuffer* commandBuffer = frame.commandBuffer;
    NRI.BeginCommandBuffer(*commandBuffer, m_DescriptorPool, 0);
    {
        nri::TransitionBarrierDesc transitionBarriers = {};
        transitionBarriers.textureNum = 1;
        transitionBarriers.textures = &textureTransitionBarrierDesc;
        NRI.CmdPipelineBarrier(*commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);

        NRI.CmdBeginRenderPass(*commandBuffer, *currentBackBuffer.frameBuffer, nri::RenderPassBeginFlag::NONE);
        {
            {
                helper::Annotation annotation(NRI, *commandBuffer, "Clear");

                uint32_t halfWidth = windowWidth / 2;
                uint32_t halfHeight = windowHeight / 2;

                nri::ClearDesc clearDesc = {};
                clearDesc.colorAttachmentIndex = 0;
                clearDesc.value.rgba32f = COLOR_1;
                nri::Rect rects[2];
                rects[0] = { 0, 0, halfWidth, halfHeight };
                rects[1] = { (int32_t)halfWidth, (int32_t)halfHeight, halfWidth, halfHeight };
                NRI.CmdClearAttachments(*commandBuffer, &clearDesc, 1, rects, helper::GetCountOf(rects));
            }

            {
                helper::Annotation annotation(NRI, *commandBuffer, "Triangle");

                const nri::Viewport viewport = { 0.0f, 0.0f, (float)windowWidth, (float)windowHeight, 0.0f, 1.0f };
                NRI.CmdSetViewports(*commandBuffer, &viewport, 1);

                NRI.CmdSetPipelineLayout(*commandBuffer, *m_PipelineLayout);
                NRI.CmdSetPipeline(*commandBuffer, *m_Pipeline);
                NRI.CmdSetConstants(*commandBuffer, 0, &m_Transparency, 4);
                NRI.CmdSetIndexBuffer(*commandBuffer, *m_GeometryBuffer, 0, nri::IndexType::UINT16);
                NRI.CmdSetVertexBuffers(*commandBuffer, 0, 1, &m_GeometryBuffer, &m_GeometryOffset);

                nri::DescriptorSet* sets[2] = { frame.constantBufferDescriptorSet, m_TextureDescriptorSet };
                NRI.CmdSetDescriptorSets(*commandBuffer, 0, helper::GetCountOf(sets), sets, nullptr);

                nri::Rect scissor = { 0, 0, windowWidth / 2, windowHeight };
                NRI.CmdSetScissors(*commandBuffer, &scissor, 1);
                NRI.CmdDrawIndexed(*commandBuffer, 3, 1, 0, 0, 0);

                scissor = { (int32_t)windowWidth / 2, (int32_t)windowHeight / 2, windowWidth / 2, windowHeight / 2 };
                NRI.CmdSetScissors(*commandBuffer, &scissor, 1);
                NRI.CmdDraw(*commandBuffer, 3, 1, 0, 0);
            }

            RenderUserInterface(*commandBuffer);
        }
        NRI.CmdEndRenderPass(*commandBuffer);

        textureTransitionBarrierDesc.prevAccess = textureTransitionBarrierDesc.nextAccess;
        textureTransitionBarrierDesc.nextAccess = nri::AccessBits::UNKNOWN;
        textureTransitionBarrierDesc.prevLayout = textureTransitionBarrierDesc.nextLayout;
        textureTransitionBarrierDesc.nextLayout = nri::TextureLayout::PRESENT;

        NRI.CmdPipelineBarrier(*commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
    }
    NRI.EndCommandBuffer(*commandBuffer);

    nri::WorkSubmissionDesc workSubmissionDesc = {};
    workSubmissionDesc.commandBufferNum = 1;
    workSubmissionDesc.commandBuffers = &commandBuffer;
    workSubmissionDesc.wait = &m_AcquireSemaphore;
    workSubmissionDesc.waitNum = 1;
    workSubmissionDesc.signal = &m_ReleaseSemaphore;
    workSubmissionDesc.signalNum = 1;
    NRI.SubmitQueueWork(*m_CommandQueue, workSubmissionDesc, frame.deviceSemaphore);

    NRI.SwapChainPresent(*m_SwapChain, *m_ReleaseSemaphore);
}

#if defined(_WIN32)
#include <windows.h>
#undef LoadLibrary

Library* LoadSharedLibrary(const char* path)
{
    return (Library*)LoadLibraryA(path);
}

void* GetSharedLibraryFunction(Library& library, const char* name)
{
    return (void*)GetProcAddress((HMODULE)&library, name);
}

void UnloadSharedLibrary(Library& library)
{
    FreeLibrary((HMODULE)&library);
}
#elif defined(__linux__)
#include <dlfcn.h>

Library* LoadSharedLibrary(const char* path)
{
    return (Library*)dlopen(path, RTLD_NOW);
}

void* GetSharedLibraryFunction(Library& library, const char* name)
{
    return dlsym((void*)&library, name);
}

void UnloadSharedLibrary(Library& library)
{
    dlclose((void*)&library);
}
#else 
#error unknown platform
#endif

SAMPLE_MAIN(Sample, 0);

