// © 2021 NVIDIA Corporation

#if( defined(COMPILER_FXC) )

    #define NRI_RESOURCE( resourceType, resourceName, regName, bindingIndex, setIndex ) \
        resourceType resourceName : register( regName ## bindingIndex )

    #define NRI_PUSH_CONSTANTS( structName, constantBufferName, bindingIndex ) \
        cbuffer structName ## _ ## constantBufferName : register( b ## bindingIndex ) \
        { \
            structName constantBufferName; \
        }

#elif( defined(COMPILER_DXC) && !defined(VULKAN) )

    #define NRI_RESOURCE( resourceType, resourceName, regName, bindingIndex, setIndex ) \
        resourceType resourceName : register( regName ## bindingIndex, space ## setIndex )

    #define NRI_PUSH_CONSTANTS( structName, constantBufferName, bindingIndex ) \
        ConstantBuffer<structName> constantBufferName : register( b ## bindingIndex, space0 )

#elif( defined(COMPILER_DXC) && defined(VULKAN) )

    #define NRI_RESOURCE( resourceType, resourceName, regName, bindingIndex, setIndex ) \
        resourceType resourceName : register( regName ## bindingIndex, space ## setIndex )

    #define NRI_PUSH_CONSTANTS( structName, constantBufferName, bindingIndex ) \
        [[vk::push_constant]] structName constantBufferName

    // TODO: how to support FP16 in VK?
    #define min16float float
    #define min16float2 float2
    #define min16float3 float3
    #define min16float4 float4

#else
    #error "Unknown shader compiler. Please define COMPILER_FXC or COMPILER_DXC."
#endif
