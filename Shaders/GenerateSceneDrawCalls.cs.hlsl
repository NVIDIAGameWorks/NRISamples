
#include "NRICompatibility.hlsli"

#define THREAD_COUNT 256

struct CullingConstants
{
	uint DrawCount;
	uint EnableCulling;
	uint ScreenWidth;
	uint ScreenHeight;
	float4 Frustum;
};

struct MaterialData
{
    float4 baseColorAndMetallic;
    float4 emissiveColorAndRoughness;
    uint baseColorTexIndex;
    uint roughnessMetalnessTexIndex;
    uint normalTexIndex;
    uint emissiveTexIndex;
};

struct MeshData
{
    uint vtxOffset;
    uint vtxCount;
    uint idxOffset;
    uint idxCount;
};

struct InstanceData
{
	uint padding1;
	uint padding2;
    uint meshIndex;
    uint materialIndex;
};  

NRI_PUSH_CONSTANTS(CullingConstants, Constants, 0);
NRI_RESOURCE(StructuredBuffer<MaterialData>, Materials, t, 0, 0);
NRI_RESOURCE(StructuredBuffer<MeshData>, Meshes, t, 1, 0);
NRI_RESOURCE(StructuredBuffer<InstanceData>, Instances, t, 2, 0);
NRI_RESOURCE(RWByteAddressBuffer, Commands, u, 0, 0);

groupshared uint DrawCount = 0;

[numthreads(THREAD_COUNT, 1, 1)]
void main(uint ThreadID : SV_DispatchThreadId)
{
	uint InstanceIndex = ThreadID.x;
	if (InstanceIndex >= Constants.DrawCount) {
		return;
	}

	uint DrawIndex = 0;
	InterlockedAdd(DrawCount, 1, DrawIndex);

    uint MeshIndex = Instances[InstanceIndex].meshIndex;
	NRI_FILL_DRAW_INDEXED_COMMAND(Commands, DrawIndex, 
		Meshes[MeshIndex].idxCount,
		1,	// TODO: batch draw instances with same mesh into one draw call
		Meshes[MeshIndex].idxOffset,
		Meshes[MeshIndex].vtxOffset,
		InstanceIndex
	);
}