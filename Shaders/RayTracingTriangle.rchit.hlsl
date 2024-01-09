// © 2021 NVIDIA Corporation

struct Payload
{
    float3 hitValue;
};

struct IntersectionAttributes
{
    float2 barycentrics;
};

[shader( "closesthit" )]
void closest_hit( inout Payload payload : SV_RayPayload, in IntersectionAttributes intersectionAttributes : SV_IntersectionAttributes )
{
    float2 barycentrics = intersectionAttributes.barycentrics;

    payload.hitValue = float3( 1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y );
}
