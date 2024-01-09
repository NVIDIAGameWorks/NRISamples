// © 2021 NVIDIA Corporation

struct OutputVS
{
    float4 position : SV_Position;
};

OutputVS main( float3 inPos : POSITION0 )
{
    OutputVS output;
    output.position = float4( inPos, 1.0 );

    return output;
}
