// © 2021 NVIDIA Corporation

#include "NRIFramework.h"

#include <array>

struct NRIInterface
    : public nri::CoreInterface
    , public nri::SwapChainInterface
    , public nri::HelperInterface
{};

struct Frame
{
    nri::CommandAllocator* commandAllocator;
    nri::CommandBuffer* commandBuffer;
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
    void ResizeSwapChain();

private:

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::Fence* m_FrameFence = nullptr;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
    std::vector<nri::Memory*> m_MemoryAllocations;
    std::vector<BackBuffer> m_SwapChainBuffers;

    nri::Format m_SwapChainFormat;
    uint2 m_PrevWindowResolution;
    bool m_IsFullscreen = false;
};

Sample::~Sample()
{
    NRI.WaitForIdle(*m_CommandQueue);

    for (Frame& frame : m_Frames)
    {
        NRI.DestroyCommandBuffer(*frame.commandBuffer);
        NRI.DestroyCommandAllocator(*frame.commandAllocator);
    }

    for (BackBuffer& backBuffer : m_SwapChainBuffers)
        NRI.DestroyDescriptor(*backBuffer.colorAttachment);

    NRI.DestroyFence(*m_FrameFence);
    NRI.DestroySwapChain(*m_SwapChain);

    for (size_t i = 0; i < m_MemoryAllocations.size(); i++)
        NRI.FreeMemory(*m_MemoryAllocations[i]);

    DestroyUserInterface();

    nri::nriDestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI)
{
    m_PrevWindowResolution = m_WindowResolution;

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

    { // Swap chain
        nri::SwapChainDesc swapChainDesc = {};
        swapChainDesc.window = GetWindow();
        swapChainDesc.commandQueue = m_CommandQueue;
        swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
        swapChainDesc.verticalSyncInterval = m_VsyncInterval;
        swapChainDesc.width = (uint16_t)m_WindowResolution.x;
        swapChainDesc.height = (uint16_t)m_WindowResolution.y;
        swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
        NRI_ABORT_ON_FAILURE( NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain) );

        uint32_t swapChainTextureNum;
        nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);
        m_SwapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

        for (uint32_t i = 0; i < swapChainTextureNum; i++)
        {
            nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, m_SwapChainFormat};

            nri::Descriptor* colorAttachment;
            NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(textureViewDesc, colorAttachment) );

            const BackBuffer backBuffer = { colorAttachment, swapChainTextures[i] };
            m_SwapChainBuffers.push_back(backBuffer);
        }
    }

    // Buffered resources
    for (Frame& frame : m_Frames)
    {
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueue, frame.commandAllocator) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffer) );
    }

    return CreateUserInterface(*m_Device, NRI, NRI, m_SwapChainFormat);
}

void Sample::PrepareFrame(uint32_t frameIndex)
{
    const uint32_t N = 10000;
    uint32_t n = N - 1 - (frameIndex % N);

    // Info text
    char s[64];
    if (m_IsFullscreen)
        snprintf(s, sizeof(s), "Going windowed in %u...", n / 1000);
    else
        snprintf(s, sizeof(s), "Going fullscreen in %u...", n / 1000);

    // Resize
    if (n == 0)
    {
        m_IsFullscreen = !m_IsFullscreen;

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* vidmode = glfwGetVideoMode(monitor);
        uint32_t w = (uint32_t)vidmode->width;
        uint32_t h = (uint32_t)vidmode->height;

        if (m_IsFullscreen)
            m_WindowResolution = uint2(w, h);
        else
            m_WindowResolution = m_PrevWindowResolution;

        uint32_t x = (w - m_WindowResolution.x) >> 1;
        uint32_t y = (h - m_WindowResolution.y) >> 1;

        glfwSetWindowAttrib(m_Window, GLFW_DECORATED, m_IsFullscreen ? 0 : 1);
        glfwSetWindowPos(m_Window, x, y);
        glfwSetWindowSize(m_Window, m_WindowResolution.x, m_WindowResolution.y);

        ResizeSwapChain();
    }

    // UI
    ImVec2 dims  = ImGui::CalcTextSize(s);

    ImVec2 p;
    p.x = ((float)m_WindowResolution.x - dims.x) * 0.5f;
    p.y = ((float)m_WindowResolution.y - dims.y) * 0.5f;

    ImGui::SetNextWindowPos(p);
    ImGui::Begin("Color", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    {
        ImGui::Text(s);
    }
    ImGui::End();
}

void Sample::ResizeSwapChain()
{
    // Wait for idle
    NRI.WaitForIdle(*m_CommandQueue);

    // Destroy old swapchain
    for (BackBuffer& backBuffer : m_SwapChainBuffers)
        NRI.DestroyDescriptor(*backBuffer.colorAttachment);

    NRI.DestroySwapChain(*m_SwapChain);

    // Create new swapchain
    nri::SwapChainDesc swapChainDesc = {};
    swapChainDesc.window = GetWindow();
    swapChainDesc.commandQueue = m_CommandQueue;
    swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
    swapChainDesc.verticalSyncInterval = m_VsyncInterval;
    swapChainDesc.width = (uint16_t)m_WindowResolution.x;
    swapChainDesc.height = (uint16_t)m_WindowResolution.y;
    swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
    NRI_ABORT_ON_FAILURE( NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain) );

    uint32_t swapChainTextureNum;
    nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);
    m_SwapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

    m_SwapChainBuffers.clear();
    for (uint32_t i = 0; i < swapChainTextureNum; i++)
    {
        nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT, m_SwapChainFormat};

        nri::Descriptor* colorAttachment;
        NRI_ABORT_ON_FAILURE( NRI.CreateTexture2DView(textureViewDesc, colorAttachment) );

        const BackBuffer backBuffer = { colorAttachment, swapChainTextures[i] };
        m_SwapChainBuffers.push_back(backBuffer);
    }
}

void Sample::RenderFrame(uint32_t frameIndex)
{
    const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
    const Frame& frame = m_Frames[bufferedFrameIndex];

    if (frameIndex >= BUFFERED_FRAME_MAX_NUM)
    {
        NRI.Wait(*m_FrameFence, 1 + frameIndex - BUFFERED_FRAME_MAX_NUM);
        NRI.ResetCommandAllocator(*frame.commandAllocator);
    }

    const uint32_t backBufferIndex = NRI.AcquireNextSwapChainTexture(*m_SwapChain);
    BackBuffer& backBuffer = m_SwapChainBuffers[backBufferIndex];

    nri::CommandBuffer& commandBuffer = *frame.commandBuffer;
    NRI.BeginCommandBuffer(commandBuffer, nullptr, 0);
    {
        nri::TextureBarrierDesc textureBarrierDescs = {};
        textureBarrierDescs.texture = backBuffer.texture;
        textureBarrierDescs.after = {nri::AccessBits::COPY_SOURCE, nri::Layout::COPY_SOURCE};
        textureBarrierDescs.arraySize = 1;
        textureBarrierDescs.mipNum = 1;

        nri::BarrierGroupDesc barrierGroupDesc = {};
        barrierGroupDesc.textureNum = 1;
        barrierGroupDesc.textures = &textureBarrierDescs;
        NRI.CmdBarrier(commandBuffer, barrierGroupDesc);

        nri::TextureDataLayoutDesc dstDataLayoutDesc = {};
        dstDataLayoutDesc.rowPitch = NRI.GetDeviceDesc(*m_Device).uploadBufferTextureRowAlignment;

        textureBarrierDescs.before = textureBarrierDescs.after;
        textureBarrierDescs.after = {nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT};
        NRI.CmdBarrier(commandBuffer, barrierGroupDesc);

        nri::AttachmentsDesc attachmentsDesc = {};
        attachmentsDesc.colorNum = 1;
        attachmentsDesc.colors = &backBuffer.colorAttachment;

        NRI.CmdBeginRendering(commandBuffer, attachmentsDesc);
        {
            helper::Annotation annotation(NRI, commandBuffer, "Clear");

            nri::ClearDesc clearDesc = {};
            if (m_IsFullscreen)
                clearDesc.value.color32f = {0.0f, 1.0f, 0.0f, 1.0f};
            else
                clearDesc.value.color32f = {1.0f, 0.0f, 0.0f, 1.0f};
            NRI.CmdClearAttachments(commandBuffer, &clearDesc, 1, nullptr, 0);

            RenderUserInterface(*m_Device, commandBuffer, 1.0f, true);
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
