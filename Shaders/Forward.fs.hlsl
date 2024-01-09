// © 2021 NVIDIA Corporation

#include "BindingBridge.hlsli"
#include "ForwardResources.hlsli"

[earlydepthstencil]
float4 main( in Attributes input ) : SV_Target
{
    PS_INPUT;
    float4 output = Shade( float4( albedo, diffuse.w ), Rf0, roughness, emissive, N, L, V, Clight, FAKE_AMBIENT );

    output.xyz = STL::Color::HdrToLinear( output.xyz * exposure );
    return output;
}
