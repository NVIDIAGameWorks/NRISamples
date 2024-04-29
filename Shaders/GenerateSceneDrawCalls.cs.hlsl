#define NRI_ENABLE_DRAW_PARAMETERS_EMULATION

#include "NRICompatibility.hlsli"
#include "SceneViewerBindlessStructs.h"

#define THREAD_COUNT 256

NRI_PUSH_CONSTANTS(CullingConstants, Constants, 0);
NRI_RESOURCE(StructuredBuffer<MaterialData>, Materials, t, 0, 0);
NRI_RESOURCE(StructuredBuffer<MeshData>, Meshes, t, 1, 0);
NRI_RESOURCE(StructuredBuffer<InstanceData>, Instances, t, 2, 0);
NRI_RESOURCE(RWBuffer<uint>, Commands, u, 0, 0);

groupshared uint DrawCount;

[numthreads(THREAD_COUNT, 1, 1)]
void main(uint ThreadID : SV_DispatchThreadId)
{
	if (ThreadID == 0)
		DrawCount = 0;

	GroupMemoryBarrierWithGroupSync();

	uint InstanceIndex = ThreadID.x;
	if (InstanceIndex >= Constants.DrawCount)
		return;

	uint DrawIndex = 0;
	InterlockedAdd(DrawCount, 1, DrawIndex);

    uint MeshIndex = Instances[InstanceIndex].meshIndex;
	NRI_FILL_DRAW_INDEXED_DESC(Commands, DrawIndex,
		Meshes[MeshIndex].idxCount,
		1,	// TODO: batch draw instances with same mesh into one draw call
		Meshes[MeshIndex].idxOffset,
		Meshes[MeshIndex].vtxOffset,
		InstanceIndex
	);
}