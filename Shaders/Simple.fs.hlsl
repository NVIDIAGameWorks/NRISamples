// © 2021 NVIDIA Corporation

struct OutputVS
{
    float4 position : SV_Position;
    float2 texCoords : TEXCOORD0;
};

float4 main( in OutputVS input ) : SV_Target
{
    return float4( input.texCoords, 0, 1 );
}
