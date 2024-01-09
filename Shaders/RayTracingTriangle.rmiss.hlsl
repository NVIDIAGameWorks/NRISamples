// © 2021 NVIDIA Corporation

struct Payload
{
    float3 hitValue;
};

[shader( "miss" )]
void miss( inout Payload payload : SV_RayPayload )
{
    payload.hitValue = float3( 0.4, 0.3, 0.35 );
}
