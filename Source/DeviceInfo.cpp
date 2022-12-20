/*
Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <stdio.h>
#include <malloc.h>

#define NRI_FORCE_C

#include "NRI.h"
#include "Extensions/NRIDeviceCreation.h"

static const char* deviceTypes[] =
{
    "unknown",
    "integrated",
    "descrete"
};

static const char* vendors[] =
{
    "unknown",
    "NVIDIA",
    "AMD",
    "INTEL"
};

bool EnumeratePhysicalDeviceGroups()
{
    // Query device groups number
    uint32_t deviceGroupNum = 0;
    nri_Result result = nri_GetPhysicalDevices(NULL, &deviceGroupNum);
    if (result != NRI_RESULT_SUCCESS)
        return false;

    printf("nri_GetPhysicalDevices: %u groups reported\n", deviceGroupNum);
    if (!deviceGroupNum)
        return true;

    // Query device groups
    size_t bytes = deviceGroupNum * sizeof(nri_PhysicalDeviceGroup);
    nri_PhysicalDeviceGroup* groups = (nri_PhysicalDeviceGroup*)_alloca(bytes);
    result = nri_GetPhysicalDevices(groups, &deviceGroupNum);
    if (result != NRI_RESULT_SUCCESS)
        return false;

    // Print information
    for (uint32_t i = 0; i < deviceGroupNum; i++)
    {
        const nri_PhysicalDeviceGroup* p = groups + i;

        printf("\nGroup #%u\n", i + 1);
        printf("\tDescription: %s\n", p->description);
        printf("\tLUID: 0x%016llX\n", p->luid);
        printf("\tVideo memory (Mb): %llu\n", p->dedicatedVideoMemoryMB);
        printf("\tID: 0x%08X\n", p->deviceID);
        printf("\tPhysical device group size: %u\n", p->physicalDeviceGroupSize);
        printf("\tType: %s\n", deviceTypes[p->type]);
        printf("\tVendor: %s\n", vendors[p->vendor]);
    }

    return true;
}

int main()
{
    return EnumeratePhysicalDeviceGroups() ? 0 : 1;
}