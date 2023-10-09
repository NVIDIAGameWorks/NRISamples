/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <stdlib.h>

#include "NRI.h"
#include "Extensions/NRIDeviceCreation.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIMeshShader.h"
#include "Extensions/NRIRayTracing.h"
#include "Extensions/NRISwapChain.h"

#define NRI_ABORT_ON_FAILURE(result) \
    if (result != nri_Result_SUCCESS) \
        exit(1);

#if _WIN32
    #define ALLOCA _alloca
#else
    #define ALLOCA alloca
#endif

int main()
{
    // Creation
    nri_Device* device = NULL;
    NRI_ABORT_ON_FAILURE( nri_CreateDevice(&(nri_DeviceCreationDesc){
        .graphicsAPI = nri_GraphicsAPI_D3D12
    }, &device) );

    // Interfaces
    nri_CoreInterface nriCore = {0};
    NRI_ABORT_ON_FAILURE( nri_GetInterface(device, NRI_INTERFACE(nri_CoreInterface), &nriCore) );

    nri_HelperInterface nriHelper = {0};
    NRI_ABORT_ON_FAILURE( nri_GetInterface(device, NRI_INTERFACE(nri_HelperInterface), &nriHelper) );

    nri_SwapChainInterface nriSwapChain = {0};
    NRI_ABORT_ON_FAILURE( nri_GetInterface(device, NRI_INTERFACE(nri_SwapChainInterface), &nriSwapChain) );

    // NRI usage
    nri_Buffer* buffer = NULL;
    NRI_ABORT_ON_FAILURE( nriCore.CreateBuffer(device, &(nri_BufferDesc){
        .size = 1024,
        .structureStride = 0,
        .usageMask = nri_BufferUsageBits_SHADER_RESOURCE,
        .physicalDeviceMask = 0
    }, &buffer) );

    nri_Texture* texture = NULL;
    nri_TextureDesc textureDesc = nri_Texture2D(nri_Format_RGBA8_UNORM, 32, 32, 1, 1, nri_TextureUsageBits_SHADER_RESOURCE, 1);
    NRI_ABORT_ON_FAILURE( nriCore.CreateTexture(device, &textureDesc, &texture) );

    nri_ResourceGroupDesc resourceGroupDesc = {
        .bufferNum = 1,
        .buffers = &buffer,
        .textureNum = 1,
        .textures = &texture,
        .memoryLocation = nri_MemoryLocation_DEVICE,
    };
    uint32_t allocationNum = nriHelper.CalculateAllocationNumber(device, &resourceGroupDesc);

    nri_Memory** memories = (nri_Memory**)ALLOCA(allocationNum * sizeof(nri_Memory*));
    NRI_ABORT_ON_FAILURE( nriHelper.AllocateAndBindMemory(device, &resourceGroupDesc, memories) );

    nriCore.DestroyTexture(texture);
    nriCore.DestroyBuffer(buffer);
    
    for (uint32_t i = 0; i < allocationNum; i++ )
        nriCore.FreeMemory(memories[i]);

    // Destroy
    nri_DestroyDevice(device);

    return 0;
}
