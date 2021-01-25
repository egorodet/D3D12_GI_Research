#pragma once
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
class Graphics;
class RootSignature;
class PipelineState;
class Texture;
class Camera;
struct Batch;
class CommandContext;
class Buffer;
class UnorderedAccessView;
class RGGraph;
struct ShadowData;
struct SceneData;

class TiledForward
{
public:
	TiledForward(Graphics* pGraphics);

	void OnSwapchainCreated(int windowWidth, int windowHeight);

	void Execute(RGGraph& graph, const SceneData& resources);
	void VisualizeLightDensity(RGGraph& graph, Camera& camera, Texture* pTarget, Texture* pDepth);

private:
	void SetupResources(Graphics* pGraphics);
	void SetupPipelines(Graphics* pGraphics);

	Graphics* m_pGraphics;

	//Light Culling
	std::unique_ptr<RootSignature> m_pComputeLightCullRS;
	PipelineState* m_pComputeLightCullPSO = nullptr;
	std::unique_ptr<Buffer> m_pLightIndexCounter;
	UnorderedAccessView* m_pLightIndexCounterRawUAV = nullptr;
	std::unique_ptr<Buffer> m_pLightIndexListBufferOpaque;
	std::unique_ptr<Texture> m_pLightGridOpaque;
	std::unique_ptr<Buffer> m_pLightIndexListBufferTransparant;
	std::unique_ptr<Texture> m_pLightGridTransparant;

	//Diffuse
	std::unique_ptr<RootSignature> m_pDiffuseRS;
	PipelineState* m_pDiffusePSO = nullptr;
	PipelineState* m_pDiffuseAlphaPSO = nullptr;

	//Visualize Light Count
	std::unique_ptr<RootSignature> m_pVisualizeLightsRS;
	PipelineState* m_pVisualizeLightsPSO = nullptr;
	std::unique_ptr<Texture> m_pVisualizationIntermediateTexture;
};