// © 2021 NVIDIA Corporation

#include "NRICompatibility.hlsli"
#define DONT_DECLARE_RESOURCES
#include "ForwardResources.hlsli"

NRI_RESOURCE(SamplerState, AnisotropicSampler, s, 0, 0 );
NRI_RESOURCE(StructuredBuffer<MaterialData>, Materials, t, 0, 0);
NRI_RESOURCE(StructuredBuffer<MeshData>, Meshes, t, 1, 0);
NRI_RESOURCE(StructuredBuffer<InstanceData>, Instances, t, 2, 0);
NRI_RESOURCE_ARRAY(Texture2D, Textures, t, 0, 1);

struct BindlessAttributes
{
    float4 Position : SV_Position;
    float4 Normal : TEXCOORD0; //.w = TexCoord.x
    float4 View : TEXCOORD1; //.w = TexCoord.y
    float4 Tangent : TEXCOORD2;
    uint4 BaseAttributes : ATTRIBUTES;  // .x = firstInstance, .y = firstVertex, .z = instanceId, .w - vertexId
};

uint XORShift(inout uint rng_state)
{
    // Xorshift algorithm from George Marsaglia's paper.
    rng_state ^= (rng_state << 13);
    rng_state ^= (rng_state >> 17);
    rng_state ^= (rng_state << 5);
    return rng_state;
}

float Random01(inout uint rng_state)
{
    return asfloat(0x3f800000 | XORShift(rng_state) >> 9) - 1.0;
}

uint Random(inout uint rng_state, uint minimum, uint maximum)
{
    return minimum + uint(float(maximum - minimum + 1) * Random01(rng_state));
}

float3 RandomColor(inout uint rng_state)
{
    return float3(Random01(rng_state), Random01(rng_state), Random01(rng_state));
}

uint SeedThread(uint seed)
{
#if 0
    //Wang hash to initialize the seed
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
#else
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
#endif
}

float4 VisualizeId(uint Id)
{
    uint Seed = SeedThread(Id);
    return float4(
        RandomColor(Seed),
        1.0f
    );
}

[earlydepthstencil]
float4 main( in BindlessAttributes input ) : SV_Target
{
#ifndef NRI_DXBC
    uint instanceIndex = input.BaseAttributes.x + input.BaseAttributes.z;
    uint vertexIndex = input.BaseAttributes.y + input.BaseAttributes.w;
    uint materialIndex = Instances[instanceIndex].materialIndex;

    uint baseColorTexIndex = Materials[materialIndex].baseColorTexIndex;
    uint roughnessMetalnessTexIndex = Materials[materialIndex].roughnessMetalnessTexIndex;
    uint normalTexIndex = Materials[materialIndex].normalTexIndex;
    uint emissiveTexIndex = Materials[materialIndex].emissiveTexIndex;

    Texture2D DiffuseMap = Textures[baseColorTexIndex];
    Texture2D SpecularMap = Textures[roughnessMetalnessTexIndex];
    Texture2D NormalMap = Textures[normalTexIndex];
    Texture2D EmissiveMap = Textures[emissiveTexIndex];

    float2 uv = float2( input.Normal.w, input.View.w ); 
    float3 V = normalize( input.View.xyz ); 
    float3 Nvertex = input.Normal.xyz; 
    Nvertex = normalize( Nvertex ); 
    float4 T = input.Tangent; 
    T.xyz = normalize( T.xyz ); 

    float4 diffuse = DiffuseMap.Sample( AnisotropicSampler, uv ); 
    float3 materialProps = SpecularMap.Sample( AnisotropicSampler, uv ).xyz; 
    float3 emissive = EmissiveMap.Sample( AnisotropicSampler, uv ).xyz; 
    float2 packedNormal = NormalMap.Sample( AnisotropicSampler, uv ).xy; 
    
    float3 N = STL::Geometry::TransformLocalNormal( packedNormal, T, Nvertex ); 
    float3 albedo, Rf0; 
    STL::BRDF::ConvertBaseColorMetalnessToAlbedoRf0( diffuse.xyz, materialProps.z, albedo, Rf0 ); 
    float roughness = materialProps.y; 
    const float3 sunDirection = normalize( float3( -0.8, -0.8, 1.0 ) ); 
    float3 L = STL::ImportanceSampling::CorrectDirectionToInfiniteSource( N, sunDirection, V, tan( SUN_ANGULAR_SIZE ) ); 
    const float3 Clight = 80000.0;
    const float exposure = 0.00025;

    float4 output = Shade( float4( albedo, diffuse.w ), Rf0, roughness, emissive, N, L, V, Clight, FAKE_AMBIENT );
    output.xyz = STL::Color::HdrToLinear( output.xyz * exposure );
    return output;
#else
    return float4(1,0,0,1);
#endif
}
