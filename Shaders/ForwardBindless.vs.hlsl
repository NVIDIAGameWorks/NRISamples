// © 2021 NVIDIA Corporation

#include "NRICompatibility.hlsli"

NRI_ENABLE_DRAW_PARAMETERS(1);

struct Input
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
};

struct Attributes
{
    float4 Position : SV_Position;
    float4 Normal : TEXCOORD0; //.w = TexCoord.x
    float4 View : TEXCOORD1; //.w = TexCoord.y
    float4 Tangent : TEXCOORD2;
    uint4 BaseAttributes : ATTRIBUTES;  // .x = firstInstance, .y = firstVertex, .z = instanceId, .w - vertexId
};

NRI_RESOURCE( cbuffer, Global, b, 0, 0 )
{
    float4x4 gWorldToClip;
    float3 gCameraPos;
};

#ifdef NRI_DXBC
Attributes main( in Input input )
#else
Attributes main( in Input input, NRI_DECLARE_DRAW_PARAMETERS )
#endif
{
    Attributes output;

    float3 N = input.Normal * 2.0 - 1.0;
    float4 T = input.Tangent * 2.0 - 1.0;
    float3 V = gCameraPos - input.Position;

    output.Position = mul( gWorldToClip, float4( input.Position, 1 ) );
    output.Normal = float4( N, input.TexCoord.x );
    output.View = float4( V, input.TexCoord.y );
    output.Tangent = T;
#ifndef NRI_DXBC
    output.BaseAttributes.x = (NRI_INSTANCE_ID_OFFSET);
    output.BaseAttributes.y = (NRI_VERTEX_ID_OFFSET);
    output.BaseAttributes.z = (NRI_INSTANCE_ID);
    output.BaseAttributes.w = (NRI_VERTEX_ID);
#else
    output.BaseAttributes = uint4(0,0,0,0);
#endif

    return output;
}
