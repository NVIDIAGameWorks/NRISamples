// © 2021 NVIDIA Corporation

float4 main( float4 inPixelCoord : SV_POSITION ) : SV_Target
{
    return float4( inPixelCoord.zzz, 1.0 );
}
