#include "Common.hlsli"
#include "Random.hlsli"
#include "CBT.hlsli"

#define MESH_SHADER_THREAD_GROUP_SIZE 32
#define COMPUTE_THREAD_GROUP_SIZE 256

#ifndef DEBUG_ALWAYS_SUBDIVIDE
#define DEBUG_ALWAYS_SUBDIVIDE 0
#endif

#ifndef RENDER_WIREFRAME
#define RENDER_WIREFRAME 1
#endif

#ifndef FRUSTUM_CULL
#define FRUSTUM_CULL 1
#endif

#ifndef DISPLACEMENT_LOD
#define DISPLACEMENT_LOD 1
#endif

#ifndef DISTANCE_LOD
#define DISTANCE_LOD 1
#endif

#define RootSig "CBV(b0), " \
				"CBV(b1), " \
				"DescriptorTable(UAV(u0, numDescriptors = 2)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1)), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR)"

RWByteAddressBuffer uCBT : register(u0);
RWByteAddressBuffer uIndirectArgs : register(u1);
Texture2D tHeightmap : register(t0);
SamplerState sSampler : register(s0);

struct CommonArgs
{
	uint NumElements;
};

struct SumReductionData
{
	uint Depth;
};

struct UpdateData
{
	float4x4 World;
	float4x4 WorldView;
	float4x4 ViewProjection;
	float4x4 WorldViewProjection;
	float4 FrustumPlanes[6];
	float HeightmapSizeInv;
	float ScreenSizeBias;
	float HeightmapVarianceBias;
};

ConstantBuffer<CommonArgs> cCommonArgs : register(b0);
ConstantBuffer<SumReductionData> cSumReductionData : register(b1);
ConstantBuffer<UpdateData> cUpdateData : register(b1);

[numthreads(1, 1, 1)]
void PrepareDispatchArgsCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);

	uint offset = 0;

	uint numThreads = ceil((float)cbt.NumNodes() / COMPUTE_THREAD_GROUP_SIZE);
	uIndirectArgs.Store(offset + 0, numThreads);
	uIndirectArgs.Store(offset + 4, 1);
	uIndirectArgs.Store(offset + 8, 1);

	offset += sizeof(uint3);
	
	uint numMeshThreads = ceil((float)cbt.NumNodes() / MESH_SHADER_THREAD_GROUP_SIZE);
	uIndirectArgs.Store(offset + 0, numMeshThreads);
	uIndirectArgs.Store(offset + 4, 1);
	uIndirectArgs.Store(offset + 8, 1);

	offset += sizeof(uint3);

	uint numVertices = 3;
	uint numInstances = cbt.NumNodes();
	uIndirectArgs.Store(offset + 0, numVertices);
	uIndirectArgs.Store(offset + 4, numInstances);
	uIndirectArgs.Store(offset + 8, 0);
	uIndirectArgs.Store(offset + 12, 0);

	offset += sizeof(uint4);
}

[RootSignature(RootSig)]
[numthreads(COMPUTE_THREAD_GROUP_SIZE, 1, 1)]
void SumReductionCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uint count = 1u << cSumReductionData.Depth;
	uint index = threadID;
	if(index < count)
	{
		index += count;
		uint leftChild = cbt.GetData(cbt.LeftChildIndex(index));
		uint rightChild = cbt.GetData(cbt.RightChildIndex(index));
		cbt.SetData(index, leftChild + rightChild);
	}
}

[numthreads(COMPUTE_THREAD_GROUP_SIZE, 1, 1)]
void SumReductionFirstPassCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	uint depth = cSumReductionData.Depth;
	uint count = 1u << depth;
	uint thread = threadID << 5u;
	if(thread < count)
	{
		uint nodeIndex = thread + count;
		uint bitOffset = cbt.BitIndexFromHeap(nodeIndex, depth);
		uint elementIndex = bitOffset >> 5u;

		uint bitField = cbt.Storage.Load(4 * elementIndex);

		// Sum of 1 bit pairs -> 16 * 2 bit
		depth -= 1;
		nodeIndex >>= 1;
		bitField = (bitField & 0x55555555u) + ((bitField >> 1u) & 0x55555555u);
		uint data = bitField;
		cbt.Storage.Store(4 * ((bitOffset - count) >> 5u), data);

		// Sum of 2 bit pairs -> 8 * 3 bits
		depth -= 1;
		nodeIndex >>= 1;
		bitField = (bitField & 0x33333333u) + ((bitField >> 2u) & 0x33333333u);
		data = 	((bitField >> 0u) & (7u << 0u)) |
				((bitField >> 1u) & (7u << 3u)) |
				((bitField >> 2u) & (7u << 6u)) |
				((bitField >> 3u) & (7u << 9u)) |
				((bitField >> 4u) & (7u << 12u)) |
				((bitField >> 5u) & (7u << 15u)) |
				((bitField >> 6u) & (7u << 18u)) |
				((bitField >> 7u) & (7u << 21u));
		cbt.BinaryHeapSet(cbt.BitIndexFromHeap(nodeIndex, depth), 24, data);

		// Sum of 3 bit pairs -> 4 * 4 bits
		depth -= 1;
		nodeIndex >>= 1;
		bitField = (bitField & 0x0F0F0F0Fu) + ((bitField >> 4u) & 0x0F0F0F0Fu);
		data = 	((bitField >> 0u) & (15u << 0u)) |
				((bitField >> 4u) & (15u << 4u)) |
				((bitField >> 8u) & (15u << 8u)) |
				((bitField >> 12u) & (15u << 12u));
		cbt.BinaryHeapSet(cbt.BitIndexFromHeap(nodeIndex, depth), 16, data);

		// Sum of 4 bit pairs -> 2 * 5 bits
		depth -= 1;
		nodeIndex >>= 1;
		bitField = (bitField & 0x00FF00FFu) + ((bitField >> 8u) & 0x00FF00FFu);
		data = 	((bitField >> 0u) & (31u << 0u)) |
				((bitField >> 11u) & (31u << 5u));
		cbt.BinaryHeapSet(cbt.BitIndexFromHeap(nodeIndex, depth), 10, data);

		// Sum of 5 bit pairs -> 1 * 6 bits	
		depth -= 1;
		nodeIndex >>= 1;	
		bitField = (bitField & 0x0000FFFFu) + ((bitField >> 16u) & 0x0000FFFFu);
		data = bitField;
		cbt.BinaryHeapSet(cbt.BitIndexFromHeap(nodeIndex, depth), 6, data);
	}
}

bool HeightmapFlatness(float3x3 tri)
{
    float2 center = (tri[0].xz + tri[1].xz + tri[2].xz) / 3.0f;
    float2 dx = tri[0].xz - tri[1].xz;
    float2 dy = tri[0].xz - tri[2].xz;
    float height = tHeightmap.SampleGrad(sSampler, center, dx, dy).x;
    float heightVariance = saturate(height - Square(height));
    return heightVariance >= cUpdateData.HeightmapVarianceBias;
}

bool BoxPlaneIntersect(AABB aabb, float4 plane)
{
	float4 dist = dot(float4(aabb.Center.xyz, 1), plane);
	float radius = dot(aabb.Extents.xyz, abs(plane.xyz));
	return dot(dist <= radius, 1);
}

bool BoxFrustumIntersect(AABB aabb, float4 planes[6])
{
    for (int i = 0; i < 6; ++i) 
	{
		if(!BoxPlaneIntersect(aabb, planes[i]))
		{
			return false;
		}
    }
	return true;
}

bool TriangleFrustumIntersect(float3x3 tri)
{
    float3 bmin = mul(float4(min(min(tri[0], tri[1]), tri[2]), 1), cUpdateData.World).xyz;
    float3 bmax = mul(float4(max(max(tri[0], tri[1]), tri[2]), 1), cUpdateData.World).xyz;
	AABB aabb;
	AABBFromMinMax(aabb, bmin, bmax);
    return BoxFrustumIntersect(aabb, cUpdateData.FrustumPlanes);
}

float2 TriangleLOD(float3x3 tri)
{
    float3 p0 = mul(float4(tri[0], 1), cUpdateData.WorldView).xyz;
    float3 p2 = mul(float4(tri[2], 1), cUpdateData.WorldView).xyz;

    float3 c = (p0 + p2) * 0.5f;
    float3 v = (p2 - p0);
    float distSq = dot(c, c);
    float lenSq = dot(v, v);

    return float2(cUpdateData.ScreenSizeBias + log2(lenSq / distSq), 1.0f);
}

float2 GetLOD(float3x3 tri)
{
#if DEBUG_ALWAYS_SUBDIVIDE
	return 1;
#endif

#if FRUSTUM_CULL
	if(!TriangleFrustumIntersect(tri))
	{
		return float2(0, 0);
	}
#endif

#if DISPLACEMENT_LOD
	if(!HeightmapFlatness(tri))
	{
		return float2(0, 1);
	}
#endif

#if DISTANCE_LOD
	return TriangleLOD(tri);
#endif

	return float2(1, 1);
}

float3x3 GetVertices(uint heapIndex)
{
	float3x3 baseTriangle = float3x3(
		0, 0, 1,
		0, 0, 0,
		1, 0, 0
	);

	float3x3 tri = LEB::GetTriangleVertices(heapIndex, baseTriangle);
	for(int i = 0; i < 3; ++i)
	{
		tri[i].y += tHeightmap.SampleLevel(sSampler, tri[i].xz, 0).r;
	}
	return tri;
}

[numthreads(COMPUTE_THREAD_GROUP_SIZE, 1, 1)]
void UpdateCS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	if(threadID < cbt.NumNodes())
	{
		uint heapIndex = cbt.LeafToHeapIndex(threadID);
		
		float3x3 tri = GetVertices(heapIndex);
		float2 lod = GetLOD(tri);

		if(lod.x >= 1)
		{
			LEB::CBTSplitConformed(cbt, heapIndex);
		}

		if(heapIndex > 1)
		{
			LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
			bool mergeTop = GetLOD(GetVertices(diamond.Top)).x < 1.0;
			bool mergeBase = GetLOD(GetVertices(diamond.Base)).x < 1.0;
			if(mergeTop && mergeBase)
			{
				LEB::CBTMergeConformed(cbt, heapIndex);
			}
		}
	}
}

struct VertexOut
{
	float4 Position : SV_POSITION;
	float2 UV : TEXCOORD;
};

// Must be a multiple of 2 to avoid cracks
// MS max number of triangles is 256. To solve this, execute more mesh shader groups from AS
#ifndef MESH_SHADER_SUBD_LEVEL
#define MESH_SHADER_SUBD_LEVEL 6
#endif

#ifndef AMPLIFICATION_SHADER_SUBD_LEVEL
#define AMPLIFICATION_SHADER_SUBD_LEVEL 0
#endif

#define NUM_MESH_SHADER_TRIANGLES (1u << MESH_SHADER_SUBD_LEVEL)

struct ASPayload
{
	uint IDs[MESH_SHADER_THREAD_GROUP_SIZE];
};

groupshared ASPayload gsPayload;

[numthreads(MESH_SHADER_THREAD_GROUP_SIZE, 1, 1)]
void UpdateAS(uint threadID : SV_DispatchThreadID)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);
	bool isVisible = false;
	float3x3 tri = 0;
	uint heapIndex = 0;

	if(threadID < cbt.NumNodes())
	{
		heapIndex = cbt.LeafToHeapIndex(threadID);
		
		tri = GetVertices(heapIndex);
		float2 lod = GetLOD(tri);

		if(lod.x >= 1)
		{
			LEB::CBTSplitConformed(cbt, heapIndex);
		}

		if(heapIndex > 1)
		{
			LEB::DiamondIDs diamond = LEB::GetDiamond(heapIndex);
			bool mergeTop = GetLOD(GetVertices(diamond.Top)).x < 1.0;
			bool mergeBase = GetLOD(GetVertices(diamond.Base)).x < 1.0;
			if(mergeTop && mergeBase)
			{
				LEB::CBTMergeConformed(cbt, heapIndex);
			}
		}

		isVisible = lod.y > 0;
	}

	if(isVisible)
	{
		uint laneIndex = WavePrefixCountBits(isVisible);
		gsPayload.IDs[laneIndex] = heapIndex;
	}

	uint count = WaveActiveCountBits(isVisible);
	DispatchMesh((1u << AMPLIFICATION_SHADER_SUBD_LEVEL) * count, 1, 1, gsPayload);
}

[outputtopology("triangle")]
[numthreads(NUM_MESH_SHADER_TRIANGLES, 1, 1)]
void RenderMS(
	uint groupThreadID : SV_GroupThreadID,
	uint groupID : SV_GroupID,
	in payload ASPayload payload,
	out vertices VertexOut vertices[NUM_MESH_SHADER_TRIANGLES * 3],
	out indices uint3 triangles[NUM_MESH_SHADER_TRIANGLES])
{
	SetMeshOutputCounts(NUM_MESH_SHADER_TRIANGLES * 3, NUM_MESH_SHADER_TRIANGLES * 1);
	uint outputIndex = groupThreadID;
	uint heapIndex = payload.IDs[groupID / (1u << AMPLIFICATION_SHADER_SUBD_LEVEL)];
	float3x3 tri = GetVertices((((heapIndex << MESH_SHADER_SUBD_LEVEL) | outputIndex) << AMPLIFICATION_SHADER_SUBD_LEVEL) | groupID % (1u << AMPLIFICATION_SHADER_SUBD_LEVEL));

	for(uint i = 0; i < 3; ++i)
	{
		vertices[outputIndex * 3 + i].Position = mul(float4(tri[i], 1), cUpdateData.WorldViewProjection);
		vertices[outputIndex * 3 + i].UV = tri[i].xz;
	}
	triangles[outputIndex] = uint3(
		outputIndex * 3 + 0, 
		outputIndex * 3 + 1, 
		outputIndex * 3 + 2);
}

void RenderVS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID, out VertexOut vertex)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);

	uint heapIndex = cbt.LeafToHeapIndex(instanceID);
	float3 tri = GetVertices(heapIndex)[vertexID];

	vertex.UV = tri.xz;
	vertex.Position = mul(float4(tri, 1), cUpdateData.WorldViewProjection);
}

float4 RenderPS(
	VertexOut vertex, 
	float3 bary : SV_Barycentrics) : SV_TARGET
{
	float tl = tHeightmap.Sample(sSampler, vertex.UV, uint2(-1, -1)).r;
	float t  = tHeightmap.Sample(sSampler, vertex.UV, uint2( 0, -1)).r;
	float tr = tHeightmap.Sample(sSampler, vertex.UV, uint2( 1, -1)).r;
	float l  = tHeightmap.Sample(sSampler, vertex.UV, uint2(-1,  0)).r;
	float r  = tHeightmap.Sample(sSampler, vertex.UV, uint2( 1,  0)).r;
	float bl = tHeightmap.Sample(sSampler, vertex.UV, uint2(-1,  1)).r;
	float b  = tHeightmap.Sample(sSampler, vertex.UV, uint2( 0,  1)).r;
	float br = tHeightmap.Sample(sSampler, vertex.UV, uint2( 1,  1)).r;

	float dX = tr + 2 * r + br - tl - 2 * l - bl;
	float dY = bl + 2 * b + br - tl - 2 * t - tr;
	float3 normal = normalize(float3(dX, 1.0f / 20, dY));

	float3 dir = normalize(float3(1, 1, 1));
	float4 color = float4(saturate(dot(dir, normalize(normal)).xxx), 1);

#if RENDER_WIREFRAME
	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	color.xyz *= saturate(minBary + 0.5f);
#endif
	return color;
}

void DebugVisualizeVS(
	uint vertexID : SV_VertexID, 
	uint instanceID : SV_InstanceID,
	out float4 pos : SV_POSITION, 
	out float4 color : COLOR)
{
	CBT cbt;
	cbt.Init(uCBT, cCommonArgs.NumElements);

	uint heapIndex = cbt.LeafToHeapIndex(instanceID);

	float3x3 baseTriangle = float3x3(
		0, 1, 0,
		0, 0, 0,
		1, 0, 0
	);

	float3 tri = LEB::GetTriangleVertices(heapIndex, baseTriangle)[vertexID];
	tri.xy = tri.xy * 2 - 1;
	pos = float4(tri, 1);
	
	uint state = SeedThread(firstbithigh(heapIndex));
	color = float4(Random01(state), Random01(state), Random01(state), 1);
}

float4 DebugVisualizePS(
	float4 position : SV_POSITION, 
	float4 color : COLOR, 
	float3 bary : SV_Barycentrics) : SV_TARGET
{
	float3 deltas = fwidth(bary);
	float3 smoothing = deltas * 1;
	float3 thickness = deltas * 0.2;
	bary = smoothstep(thickness, thickness + smoothing, bary);
	float minBary = min(bary.x, min(bary.y, bary.z));
	return float4(color.xyz * saturate(minBary + 0.7), 1);
}