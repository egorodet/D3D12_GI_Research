#include "Common.hlsli"

#define BLOCK_SIZE 16
#define THREAD_COUNT (BLOCK_SIZE * BLOCK_SIZE)

#if WITH_MSAA
Texture2DMS<float> tDepthMap : register(t0);
#else
Texture2D<float> tDepthMap : register(t0);
#endif

Texture2D<float2> tReductionMap : register(t0);
RWTexture2D<float2> uOutputMap : register(u0);

groupshared float2 gsDepthSamples[BLOCK_SIZE * BLOCK_SIZE];

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void PrepareReduceDepth(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
	uint2 samplePos = groupId.xy * BLOCK_SIZE + groupThreadId.xy;
#if WITH_MSAA
	uint2 dimensions;
	uint sampleCount;
	tDepthMap.GetDimensions(dimensions.x, dimensions.y, sampleCount);
	samplePos = min(samplePos, dimensions - 1);

	float depthMin = 10000000000000000.0f;
	float depthMax = 0.0f;

	for(uint sampleIdx = 0; sampleIdx < sampleCount; ++sampleIdx)
	{
		float depth = tDepthMap.Load(samplePos, sampleIdx);
		if(depth > 0.0f)
		{
			depth = LinearizeDepth01(depth);
			depthMin = min(depthMin, depth);
			depthMax = max(depthMax, depth);
		}
	}
	gsDepthSamples[groupIndex] = float2(depthMin, depthMax);
#else
	uint2 dimensions;
	tDepthMap.GetDimensions(dimensions.x, dimensions.y);
	samplePos = min(samplePos, dimensions - 1);
	float depth = tDepthMap[samplePos];
	if(depth > 0.0f)
	{
		depth = LinearizeDepth01(depth);
	}
	gsDepthSamples[groupIndex] = float2(depth, depth);

#endif

	GroupMemoryBarrierWithGroupSync();

	for(uint s = THREAD_COUNT / 2; s > 0; s >>= 1)
	{
		if(groupIndex < s)
		{
			gsDepthSamples[groupIndex].x = min(gsDepthSamples[groupIndex].x, gsDepthSamples[groupIndex + s].x);
			gsDepthSamples[groupIndex].y = max(gsDepthSamples[groupIndex].y, gsDepthSamples[groupIndex + s].y);
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if(groupIndex == 0)
	{
		uOutputMap[groupId.xy] = gsDepthSamples[0];
	}
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void ReduceDepth(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
	uint2 dimensions;
	tReductionMap.GetDimensions(dimensions.x, dimensions.y);

	uint2 samplePos = groupId.xy * BLOCK_SIZE + groupThreadId.xy;
	samplePos = min(samplePos, dimensions - 1);

	float minDepth = tReductionMap[samplePos].x;
	float maxDepth = tReductionMap[samplePos].y;
	gsDepthSamples[groupIndex] = float2(minDepth, maxDepth);

	GroupMemoryBarrierWithGroupSync();

	for(uint s = THREAD_COUNT / 2; s > 0; s >>= 1)
	{
		if(groupIndex < s)
		{
			gsDepthSamples[groupIndex].x = min(gsDepthSamples[groupIndex].x, gsDepthSamples[groupIndex + s].x);
			gsDepthSamples[groupIndex].y = max(gsDepthSamples[groupIndex].y, gsDepthSamples[groupIndex + s].y);
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if(groupIndex == 0)
	{
		uOutputMap[groupId.xy] = gsDepthSamples[0];
	}
}
