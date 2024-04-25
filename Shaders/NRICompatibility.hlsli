// © 2024 NVIDIA Corporation

#ifndef NRI_COMPATIBILITY_HLSLI
#define NRI_COMPATIBILITY_HLSLI

#ifndef __cplusplus
    #define NRI_MERGE_TOKENS(a, b) a##b
#endif

#define NRI_UV_TO_CLIP(uv) (uv * float2(2, -2) + float2(-1, 1))
#define NRI_CLIP_TO_UV(clip) (clip * float2(0.5, -0.5) + 0.5)

// Container detection
#ifdef __hlsl_dx_compiler
    #ifdef __spirv__
        #define NRI_SPIRV
        #define NRI_PRINTF_AVAILABLE
    #else
        #define NRI_DXIL
    #endif
#else
    #ifndef __cplusplus
        #define NRI_DXBC
    #endif
#endif

// Shader model
#ifdef NRI_DXBC
    #define NRI_SHADER_MODEL 50
#else
    #define NRI_SHADER_MODEL (__SHADER_TARGET_MAJOR * 10 + __SHADER_TARGET_MINOR)
#endif

// SPIRV
#ifdef NRI_SPIRV
    #define NRI_RESOURCE_ARRAY(resourceType, resourceName, regName, bindingIndex, setIndex) \
        resourceType resourceName[] : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    // Resources
    #define NRI_RESOURCE(resourceType, resourceName, regName, bindingIndex, setIndex) \
        resourceType resourceName : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    #define NRI_PUSH_CONSTANTS(structName, constantBufferName, bindingIndex) \
        [[vk::push_constant]] structName constantBufferName

    // Draw parameters (requires SPV_KHR_shader_draw_parameters)
    #define NRI_ENABLE_DRAW_PARAMETERS(bindingIndex)

    #define NRI_DECLARE_DRAW_PARAMETERS \
        int NRI_VERTEX_ID_OFFSET : SV_VertexID, \
        uint NRI_INSTANCE_ID_OFFSET : SV_InstanceID, \
        [[vk::builtin("BaseVertex")]] int NRI_BASE_VERTEX : _SV_Nothing, \
        [[vk::builtin("BaseInstance")]] uint NRI_BASE_INSTANCE : _SV_Nothing

    #define NRI_DRAW_INDEXED_COMMAND_SIZE 5 * 4
    #define NRI_FILL_DRAW_INDEXED_COMMAND(buffer, cmdIndex, indexCount, instanceCount, startIndex, baseVertex, startInstance) \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 0, indexCount); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 4, instanceCount); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 8, startIndex); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 12, baseVertex); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 16, startInstance)

    #define NRI_VERTEX_ID (NRI_VERTEX_ID_OFFSET - NRI_BASE_VERTEX)
    #define NRI_INSTANCE_ID (NRI_INSTANCE_ID_OFFSET - NRI_BASE_INSTANCE)
#endif

// DXIL
#ifdef NRI_DXIL
    #define NRI_RESOURCE_ARRAY(resourceType, resourceName, regName, bindingIndex, setIndex) \
        resourceType resourceName[] : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    // Resources
    #define NRI_RESOURCE(resourceType, resourceName, regName, bindingIndex, setIndex) \
        resourceType resourceName : register(NRI_MERGE_TOKENS(regName, bindingIndex), NRI_MERGE_TOKENS(space, setIndex))

    #define NRI_PUSH_CONSTANTS(structName, constantBufferName, bindingIndex) \
        ConstantBuffer<structName> constantBufferName : register(NRI_MERGE_TOKENS(b, bindingIndex), space0)

    // Draw parameters
    #if (NRI_SHADER_MODEL >= 68)
        #define NRI_ENABLE_DRAW_PARAMETERS(bindingIndex)

        #define NRI_DECLARE_DRAW_PARAMETERS \
            uint NRI_VERTEX_ID : SV_VertexID, \
            uint NRI_INSTANCE_ID : SV_InstanceID, \
            int NRI_BASE_VERTEX : SV_StartVertexLocation, \
            uint NRI_BASE_INSTANCE : SV_StartInstanceLocation

        #define NRI_DRAW_INDEXED_COMMAND_SIZE 5 * 4
        #define NRI_FILL_DRAW_INDEXED_COMMAND(buffer, cmdIndex, indexCount, instanceCount, startIndex, baseVertex, startInstance) \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 0, indexCount); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 4, instanceCount); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 8, startIndex); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 12, baseVertex); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 16, startInstance)
    #else
        #define NRI_ENABLE_DRAW_PARAMETERS(bindingIndex) \
            struct _BaseAttributeConstants { \
                uint BaseVertex; \
                uint BaseInstance; \
            }; \
            ConstantBuffer<_BaseAttributeConstants> _BaseAttributes : register(NRI_MERGE_TOKENS(b, bindingIndex), space0)

        #define NRI_BASE_VERTEX _BaseAttributes.BaseVertex
        #define NRI_BASE_INSTANCE _BaseAttributes.BaseInstance

        #define NRI_DECLARE_DRAW_PARAMETERS \
            uint NRI_VERTEX_ID : SV_VertexID, \
            uint NRI_INSTANCE_ID : SV_InstanceID

        #define NRI_DRAW_INDEXED_COMMAND_SIZE 7 * 4
        #define NRI_FILL_DRAW_INDEXED_COMMAND(buffer, cmdIndex, indexCount, instanceCount, startIndex, baseVertex, startInstance) \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 0, baseVertex);     /* root constant */ \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 4, startInstance);  /* root constant */ \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 8, indexCount); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 12, instanceCount); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 16, startIndex); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 20, baseVertex); \
            buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 24, startInstance)
    #endif

    #define NRI_VERTEX_ID_OFFSET (NRI_BASE_VERTEX + NRI_VERTEX_ID)
    #define NRI_INSTANCE_ID_OFFSET (NRI_BASE_INSTANCE + NRI_INSTANCE_ID)
#endif

// DXBC
#ifdef NRI_DXBC
    // Resources
    #define NRI_RESOURCE_ARRAY(resourceType, resourceName, regName, bindingIndex, setIndex) \
        resourceType resourceName : register(NRI_MERGE_TOKENS(regName, bindingIndex))

    #define NRI_RESOURCE(resourceType, resourceName, regName, bindingIndex, setIndex) \
        resourceType resourceName : register(NRI_MERGE_TOKENS(regName, bindingIndex))

    #define NRI_PUSH_CONSTANTS(structName, constantBufferName, bindingIndex) \
        cbuffer structName##_##constantBufferName : register(NRI_MERGE_TOKENS(b, bindingIndex)) { \
            structName constantBufferName; \
        }

    // Draw parameters (partially supported)
    #define NRI_ENABLE_DRAW_PARAMETERS(bindingIndex)

    #define NRI_DECLARE_DRAW_PARAMETERS \
        uint NRI_VERTEX_ID : SV_VertexID, \
        uint NRI_INSTANCE_ID : SV_InstanceID

    #define NRI_DRAW_INDEXED_COMMAND_SIZE 5 * 4
    #define NRI_FILL_DRAW_INDEXED_COMMAND(buffer, cmdIndex, indexCount, instanceCount, startIndex, baseVertex, startInstance) \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 0, indexCount); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 4, instanceCount); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 8, startIndex); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 12, baseVertex); \
        buffer.Store(cmdIndex * NRI_DRAW_INDEXED_COMMAND_SIZE + 16, startInstance)

    #define NRI_BASE_VERTEX NRI_BASE_VERTEX_is_unsupported
    #define NRI_BASE_INSTANCE NRI_BASE_INSTANCE_is_unsupported
    #define NRI_VERTEX_ID_OFFSET NRI_VERTEX_ID_OFFSET_is_unsupported
    #define NRI_INSTANCE_ID_OFFSET NRI_INSTANCE_ID_OFFSET_is_unsupported
#endif

// C/C++
#ifdef __cplusplus
    #define NRI_RESOURCE(resourceType, resourceName, regName, bindingIndex, setIndex) \
        struct resourceName
#endif

#endif