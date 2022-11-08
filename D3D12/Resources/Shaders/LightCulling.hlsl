#include "Common.hlsli"
#include "Constants.hlsli"

#define MAX_LIGHTS_PER_TILE 256
#define BLOCK_SIZE 16
#define SPLITZ_CULLING 1

Texture2D tDepthTexture : register(t0);

globallycoherent RWStructuredBuffer<uint> uLightIndexCounter : register(u0);
RWStructuredBuffer<uint> uOpaqueLightIndexList : register(u1);
RWTexture2D<uint2> uOpaqueOutLightGrid : register(u2);

RWStructuredBuffer<uint> uTransparantLightIndexList : register(u3);
RWTexture2D<uint2> uTransparantOutLightGrid : register(u4);

groupshared uint MinDepth;
groupshared uint MaxDepth;
groupshared Frustum GroupFrustum;
groupshared AABB GroupAABB;

groupshared uint OpaqueLightCount;
groupshared uint OpaqueLightIndexStartOffset;
groupshared uint OpaqueLightList[MAX_LIGHTS_PER_TILE];

groupshared uint TransparantLightCount;
groupshared uint TransparantLightIndexStartOffset;
groupshared uint TransparantLightList[MAX_LIGHTS_PER_TILE];

#if SPLITZ_CULLING
groupshared uint DepthMask;
#endif

void AddLightForOpaque(uint lightIndex)
{
	uint index;
	InterlockedAdd(OpaqueLightCount, 1, index);
	if (index < MAX_LIGHTS_PER_TILE)
	{
		OpaqueLightList[index] = lightIndex;
	}
}

void AddLightForTransparant(uint lightIndex)
{
	uint index;
	InterlockedAdd(TransparantLightCount, 1, index);
	if (index < MAX_LIGHTS_PER_TILE)
	{
		TransparantLightList[index] = lightIndex;
	}
}

uint CreateLightMask(float depthRangeMin, float depthRange, Sphere sphere)
{
	float fMin = sphere.Position.z - sphere.Radius;
	float fMax = sphere.Position.z + sphere.Radius;
	uint maskIndexStart = max(0, min(31, floor((fMin - depthRangeMin) * depthRange)));
	uint maskIndexEnd = max(0, min(31, floor((fMax - depthRangeMin) * depthRange)));

	uint mask = 0xFFFFFFFF;
	mask >>= 31 - (maskIndexEnd - maskIndexStart);
	mask <<= maskIndexStart;
	return mask;
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 threadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	int2 uv = threadID.xy;
	float fDepth = tDepthTexture[uv].r;

	//Convert to uint because you can't used interlocked functions on floats
	uint depth = asuint(fDepth);

	//Initialize the groupshared data only on the first thread of the group
	if (groupIndex == 0)
	{
		MinDepth = 0xffffffff;
		MaxDepth = 0;
		OpaqueLightCount = 0;
		TransparantLightCount = 0;
#if SPLITZ_CULLING
		DepthMask = 0;
#endif
	}

	//Wait for thread 0 to finish with initializing the groupshared data
	GroupMemoryBarrierWithGroupSync();

	//Find the min and max depth values in the threadgroup
	InterlockedMin(MinDepth, depth);
	InterlockedMax(MaxDepth, depth);

	//Wait for all the threads to finish
	GroupMemoryBarrierWithGroupSync();

	float fMinDepth = asfloat(MaxDepth);
	float fMaxDepth = asfloat(MinDepth);

	if(groupIndex == 0)
	{
		float3 viewSpace[8];
		viewSpace[0] = ScreenToView(float4(groupId.xy * BLOCK_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[1] = ScreenToView(float4(float2(groupId.x + 1, groupId.y) * BLOCK_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[2] = ScreenToView(float4(float2(groupId.x, groupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[3] = ScreenToView(float4(float2(groupId.x + 1, groupId.y + 1) * BLOCK_SIZE, fMinDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[4] = ScreenToView(float4(groupId.xy * BLOCK_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[5] = ScreenToView(float4(float2(groupId.x + 1, groupId.y) * BLOCK_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[6] = ScreenToView(float4(float2(groupId.x, groupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;
		viewSpace[7] = ScreenToView(float4(float2(groupId.x + 1, groupId.y + 1) * BLOCK_SIZE, fMaxDepth, 1.0f), cView.TargetDimensionsInv, cView.ProjectionInverse).xyz;

		GroupFrustum.Planes[0] = CalculatePlane(float3(0, 0, 0), viewSpace[6], viewSpace[4]);
		GroupFrustum.Planes[1] = CalculatePlane(float3(0, 0, 0), viewSpace[5], viewSpace[7]);
		GroupFrustum.Planes[2] = CalculatePlane(float3(0, 0, 0), viewSpace[4], viewSpace[5]);
		GroupFrustum.Planes[3] = CalculatePlane(float3(0, 0, 0), viewSpace[7], viewSpace[6]);

		float3 minAABB = 1000000;
		float3 maxAABB = -1000000;
		[unroll]
		for(uint i = 0; i < 8; ++i)
		{
			minAABB = min(minAABB, viewSpace[i]);
			maxAABB = max(maxAABB, viewSpace[i]);
		}
		AABBFromMinMax(GroupAABB, minAABB, maxAABB);
	}

	// Convert depth values to view space.
	float minDepthVS = ScreenToView(float4(0, 0, fMinDepth, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;
	float maxDepthVS = ScreenToView(float4(0, 0, fMaxDepth, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;
	float nearClipVS = ScreenToView(float4(0, 0, 1, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;

#if SPLITZ_CULLING
	float depthVS = ScreenToView(float4(0, 0, fDepth, 1), cView.TargetDimensionsInv, cView.ProjectionInverse).z;
	float depthRange = 31.0f / (maxDepthVS - minDepthVS);
	uint cellIndex = max(0, min(31, floor((depthVS - minDepthVS) * depthRange)));
	InterlockedOr(DepthMask, 1u << cellIndex);
#endif

	// Clipping plane for minimum depth value
	Plane minPlane;
	minPlane.Normal = float3(0.0f, 0.0f, 1.0f);
	minPlane.DistanceToOrigin = minDepthVS;

	GroupMemoryBarrierWithGroupSync();

	//Perform the light culling
	[loop]
	for(uint i = groupIndex; i < cView.LightCount; i += BLOCK_SIZE * BLOCK_SIZE)
	{
		Light light = GetLight(i);

		if(light.IsPoint)
		{
			Sphere sphere = (Sphere)0;
			sphere.Radius = light.Range;
			sphere.Position = mul(float4(light.Position, 1.0f), cView.View).xyz;
			if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
			{
				AddLightForTransparant(i);

				if(SphereInAABB(sphere, GroupAABB))
				{
#if SPLITZ_CULLING
					if(DepthMask & CreateLightMask(minDepthVS, depthRange, sphere))
#endif
					{
						AddLightForOpaque(i);
					}
				}
			}
		}
		else if(light.IsSpot)
		{
			Sphere sphere;
			sphere.Radius = light.Range * 0.5f / pow(light.SpotlightAngles.y, 2);
			sphere.Position = mul(float4(light.Position, 1), cView.View).xyz + mul(light.Direction, (float3x3)cView.View) * sphere.Radius;
			if (SphereInFrustum(sphere, GroupFrustum, nearClipVS, maxDepthVS))
			{
				AddLightForTransparant(i);

				if(SphereInAABB(sphere, GroupAABB))
				{
#if SPLITZ_CULLING
					if(DepthMask & CreateLightMask(minDepthVS, depthRange, sphere))
#endif
					{
						AddLightForOpaque(i);
					}
				}
			}
		}
		else
		{
			AddLightForTransparant(i);
			AddLightForOpaque(i);
		}
	}

	GroupMemoryBarrierWithGroupSync();

	//Populate the light grid only on the first thread in the group
	if (groupIndex == 0)
	{
		InterlockedAdd(uLightIndexCounter[0], OpaqueLightCount, OpaqueLightIndexStartOffset);
		uOpaqueOutLightGrid[groupId.xy] = uint2(OpaqueLightIndexStartOffset, OpaqueLightCount);

		InterlockedAdd(uLightIndexCounter[1], TransparantLightCount, TransparantLightIndexStartOffset);
		uTransparantOutLightGrid[groupId.xy] = uint2(TransparantLightIndexStartOffset, TransparantLightCount);
	}

	GroupMemoryBarrierWithGroupSync();

	//Distribute populating the light index light amonst threads in the thread group
	for (uint i = groupIndex; i < OpaqueLightCount; i += BLOCK_SIZE * BLOCK_SIZE)
	{
		uOpaqueLightIndexList[OpaqueLightIndexStartOffset + i] = OpaqueLightList[i];
	}
	for (uint i = groupIndex; i < TransparantLightCount; i += BLOCK_SIZE * BLOCK_SIZE)
	{
		uTransparantLightIndexList[TransparantLightIndexStartOffset + i] = TransparantLightList[i];
	}
}
