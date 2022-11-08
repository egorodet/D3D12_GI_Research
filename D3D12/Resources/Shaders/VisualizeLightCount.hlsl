#include "Common.hlsli"

#define BLOCK_SIZE 16

struct PassParameters
{
	int2 ClusterDimensions;
	int2 ClusterSize;
	float2 LightGridParams;
};

ConstantBuffer<PassParameters> cPass : register(b0);
Texture2D<float4> tInput : register(t0);
Texture2D<float> tSceneDepth : register(t1);
RWTexture2D<float4> uOutput : register(u0);

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t2);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t2);
#endif

static float4 DEBUG_COLORS[] = {
	float4(0,4,141, 255) / 255,
	float4(5,10,255, 255) / 255,
	float4(0,164,255, 255) / 255,
	float4(0,255,189, 255) / 255,
	float4(0,255,41, 255) / 255,
	float4(117,254,1, 255) / 255,
	float4(255,239,0, 255) / 255,
	float4(255,86,0, 255) / 255,
	float4(204,3,0, 255) / 255,
	float4(65,0,1, 255) / 255,
};

float EdgeDetection(uint2 index, uint width, uint height)
{
	float reference = LinearizeDepth(tSceneDepth.Load(uint3(index, 0)));
	uint2 offsets[8] = {
		uint2(-1, -1),
		uint2(-1, 0),
		uint2(-1, 1),
		uint2(0, -1),
		uint2(0, 1),
		uint2(1, -1),
		uint2(1, 0),
		uint2(1, 1)
	};
	float sampledValue = 0;
	for(int j = 0; j < 8; j++)
	{
		sampledValue += LinearizeDepth(tSceneDepth.Load(uint3(index + offsets[j], 0)));
	}
	sampledValue /= 8;
	return lerp(1, 0, step(0.05f, length(reference - sampledValue)));
}

[numthreads(16, 16, 1)]
void DebugLightDensityCS(uint3 threadId : SV_DispatchThreadID)
{
	uint width, height;
	tInput.GetDimensions(width, height);
	if(threadId.x < width && threadId.y < height)
	{

#if TILED_FORWARD
		uint2 tileIndex = uint2(floor(threadId.xy / BLOCK_SIZE));
		uint lightCount = tLightGrid[tileIndex].y;
		uOutput[threadId.xy] = EdgeDetection(threadId.xy, width, height) * DEBUG_COLORS[min(9, lightCount)];
#elif CLUSTERED_FORWARD
		float depth = tSceneDepth.Load(uint3(threadId.xy, 0));
		float viewDepth = LinearizeDepth(depth, cView.NearZ, cView.FarZ);
		uint slice = floor(log(viewDepth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
		uint3 clusterIndex3D = uint3(floor(threadId.xy / cPass.ClusterSize), slice);
		uint clusterIndex1D = clusterIndex3D.x + (cPass.ClusterDimensions.x * (clusterIndex3D.y + cPass.ClusterDimensions.y * clusterIndex3D.z));
		uint lightCount = tLightGrid[clusterIndex1D].y;
		uOutput[threadId.xy] = EdgeDetection(threadId.xy, width, height) * DEBUG_COLORS[min(9, lightCount)];
#endif
	}
}
