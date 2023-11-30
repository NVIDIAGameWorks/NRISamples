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
    void PrepareFrame(uint32_t) override {}
    void RenderFrame(uint32_t frameIndex) override;

private:

    NRIInterface NRI = {};
    nri::Device* m_Device = nullptr;
    nri::SwapChain* m_SwapChain = nullptr;
    nri::CommandQueue* m_CommandQueue = nullptr;
    nri::Fence* m_FrameFence = nullptr;

    std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
    std::vector<BackBuffer> m_SwapChainBuffers;
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
    NRI_ABORT_ON_FAILURE( NRI.GetCommandQueue(*m_Device, nri::CommandQueueType::GRAPHICS, m_CommandQueue) );

    // Fences
    NRI_ABORT_ON_FAILURE( NRI.CreateFence(*m_Device, 0, m_FrameFence) );

    // Swap chain
    nri::Format swapChainFormat;
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
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandAllocator(*m_CommandQueue, nri::ALL_NODES, frame.commandAllocator) );
        NRI_ABORT_ON_FAILURE( NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffer) );
    }

    return true;
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
    const BackBuffer& backBuffer = m_SwapChainBuffers[backBufferIndex];

    nri::CommandBuffer& commandBuffer = *frame.commandBuffer;
    NRI.BeginCommandBuffer(commandBuffer, nullptr, 0);
    {
        nri::TextureTransitionBarrierDesc textureTransitionBarrierDesc = {};
        textureTransitionBarrierDesc.texture = backBuffer.texture;
        textureTransitionBarrierDesc.prevAccess = nri::AccessBits::UNKNOWN;
        textureTransitionBarrierDesc.nextAccess = nri::AccessBits::COLOR_ATTACHMENT;
        textureTransitionBarrierDesc.prevLayout = nri::TextureLayout::UNKNOWN;
        textureTransitionBarrierDesc.nextLayout = nri::TextureLayout::COLOR_ATTACHMENT;
        textureTransitionBarrierDesc.arraySize = 1;
        textureTransitionBarrierDesc.mipNum = 1;

        nri::TransitionBarrierDesc transitionBarriers = {};
        transitionBarriers.textureNum = 1;
        transitionBarriers.textures = &textureTransitionBarrierDesc;
        NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);

        nri::AttachmentsDesc attachmentsDesc = {};
        attachmentsDesc.colorNum = 1;
        attachmentsDesc.colors = &backBuffer.colorAttachment;

        NRI.CmdBeginRendering(commandBuffer, attachmentsDesc);
        {
            helper::Annotation annotation(NRI, commandBuffer, "Clear");

            nri::ClearDesc clearDesc = {};
            clearDesc.colorAttachmentIndex = 0;

            nri::Dim_t w = (nri::Dim_t)GetWindowResolution().x;
            nri::Dim_t h = (nri::Dim_t)GetWindowResolution().y;
            nri::Dim_t h3 = h / 3;
            int16_t y = (int16_t)h3;

            clearDesc.value.color32f = {1.0f, 0.0f, 0.0f, 1.0f};
            nri::Rect rect1 = { 0, 0, w, h3 };
            NRI.CmdClearAttachments(commandBuffer, &clearDesc, 1, &rect1, 1);

            clearDesc.value.color32f = {0.0f, 1.0f, 0.0f, 1.0f};
            nri::Rect rect2 = { 0, y, w, h3 };
            NRI.CmdClearAttachments(commandBuffer, &clearDesc, 1, &rect2, 1);

            clearDesc.value.color32f = {0.0f, 0.0f, 1.0f, 1.0f};
            nri::Rect rect3 = { 0, y * 2, w, h3 };
            NRI.CmdClearAttachments(commandBuffer, &clearDesc, 1, &rect3, 1);
        }
        NRI.CmdEndRendering(commandBuffer);

        textureTransitionBarrierDesc.prevAccess = textureTransitionBarrierDesc.nextAccess;
        textureTransitionBarrierDesc.nextAccess = nri::AccessBits::UNKNOWN;
        textureTransitionBarrierDesc.prevLayout = textureTransitionBarrierDesc.nextLayout;
        textureTransitionBarrierDesc.nextLayout = nri::TextureLayout::PRESENT;

        NRI.CmdPipelineBarrier(commandBuffer, &transitionBarriers, nullptr, nri::BarrierDependency::ALL_STAGES);
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
