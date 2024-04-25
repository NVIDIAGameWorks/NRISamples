// © 2021 NVIDIA Corporation

#include "NRICompatibility.hlsli"
#include "ForwardResources.hlsli"

float4 main( in Attributes input, bool isFrontFace : SV_IsFrontFace ) : SV_Target
{
    PS_INPUT;
    N = isFrontFace ? N : -N;

    float4 output = Shade( float4( albedo, diffuse.w ), Rf0, roughness, emissive, N, L, V, Clight, FAKE_AMBIENT | GLASS_HACK );

    output.xyz = STL::Color::HdrToLinear( output.xyz * exposure );
    return output;
}
