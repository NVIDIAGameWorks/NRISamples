// © 2021 NVIDIA Corporation

#include <stdlib.h>

#include "NRI.h"

#include "Extensions/NRIDeviceCreation.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIMeshShader.h"
#include "Extensions/NRIRayTracing.h"
#include "Extensions/NRISwapChain.h"

#define NRI_ABORT_ON_FAILURE(result) \
    if (result != NriResult_SUCCESS) \
        exit(1);

#if _WIN32
#    define ALLOCA _alloca
#else
#    define ALLOCA alloca
#endif

int main() {
    // Creation
    NriDevice* device = NULL;
    NRI_ABORT_ON_FAILURE(nriCreateDevice(&(NriDeviceCreationDesc){
                                             .graphicsAPI = NriGraphicsAPI_D3D12},
        &device));

    // Interfaces
    NriCoreInterface nriCore = {0};
    NRI_ABORT_ON_FAILURE(nriGetInterface(device, NRI_INTERFACE(NriCoreInterface), &nriCore));

    NriHelperInterface nriHelper = {0};
    NRI_ABORT_ON_FAILURE(nriGetInterface(device, NRI_INTERFACE(NriHelperInterface), &nriHelper));

    NriSwapChainInterface nriSwapChain = {0};
    NRI_ABORT_ON_FAILURE(nriGetInterface(device, NRI_INTERFACE(NriSwapChainInterface), &nriSwapChain));

    // NRI usage
    NriBuffer* buffer = NULL;
    NRI_ABORT_ON_FAILURE(nriCore.CreateBuffer(device,
        &(NriBufferDesc){
            .size = 1024,
            .structureStride = 0,
            .usage = NriBufferUsageBits_SHADER_RESOURCE,
        },
        &buffer));

    NriTextureDesc textureDesc = {0};
    textureDesc.type = NriTextureType_TEXTURE_2D;
    textureDesc.usage = NriTextureUsageBits_SHADER_RESOURCE;
    textureDesc.format = NriFormat_RGBA8_UNORM;
    textureDesc.width = 32;
    textureDesc.height = 32;
    textureDesc.depth = 1;
    textureDesc.mipNum = 1;
    textureDesc.layerNum = 1;
    textureDesc.sampleNum = 1;

    NriTexture* texture = NULL;
    NRI_ABORT_ON_FAILURE(nriCore.CreateTexture(device, &textureDesc, &texture));

    NriResourceGroupDesc resourceGroupDesc = {
        .bufferNum = 1,
        .buffers = &buffer,
        .textureNum = 1,
        .textures = &texture,
        .memoryLocation = NriMemoryLocation_DEVICE,
    };
    uint32_t allocationNum = nriHelper.CalculateAllocationNumber(device, &resourceGroupDesc);

    NriMemory** memories = (NriMemory**)ALLOCA(allocationNum * sizeof(NriMemory*));
    NRI_ABORT_ON_FAILURE(nriHelper.AllocateAndBindMemory(device, &resourceGroupDesc, memories));

    nriCore.DestroyTexture(texture);
    nriCore.DestroyBuffer(buffer);

    for (uint32_t i = 0; i < allocationNum; i++)
        nriCore.FreeMemory(memories[i]);

    // Destroy
    nriDestroyDevice(device);

    return 0;
}
