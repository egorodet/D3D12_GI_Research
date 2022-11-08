#include "stdafx.h"
#include "SSAO.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

SSAO::SSAO(GraphicsDevice* pDevice)
{
	m_pSSAORS = new RootSignature(pDevice);
	m_pSSAORS->AddRootConstants(0, 4);
	m_pSSAORS->AddConstantBufferView(100);
	m_pSSAORS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2);
	m_pSSAORS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2);
	m_pSSAORS->Finalize("SSAO");

	m_pSSAOPSO = pDevice->CreateComputePipeline(m_pSSAORS, "SSAO.hlsl", "CSMain");
	m_pSSAOBlurPSO = pDevice->CreateComputePipeline(m_pSSAORS, "SSAOBlur.hlsl", "CSMain");
}

void SSAO::Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
{
	static float g_AoPower = 1.2f;
	static float g_AoThreshold = 0.0025f;
	static float g_AoRadius = 0.3f;
	static int g_AoSamples = 16;

	if (ImGui::Begin("Parameters"))
	{
		if (ImGui::CollapsingHeader("Ambient Occlusion"))
		{
			ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
			ImGui::SliderFloat("Threshold", &g_AoThreshold, 0.0001f, 0.01f);
			ImGui::SliderFloat("Radius", &g_AoRadius, 0, 2);
			ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
		}
	}
	ImGui::End();

	RG_GRAPH_SCOPE("Ambient Occlusion", graph);

	graph.AddPass("SSAO", RGPassFlag::Compute)
		.Read(sceneTextures.pDepth)
		.Write(sceneTextures.pAmbientOcclusion)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = sceneTextures.pAmbientOcclusion->Get();

				context.SetComputeRootSignature(m_pSSAORS);
				context.SetPipelineState(m_pSSAOPSO);

				struct
				{
					float Power;
					float Radius;
					float Threshold;
					int Samples;
				} shaderParameters{};

				shaderParameters.Power = g_AoPower;
				shaderParameters.Radius = g_AoRadius;
				shaderParameters.Threshold = g_AoThreshold;
				shaderParameters.Samples = g_AoSamples;

				context.SetRootConstants(0, shaderParameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, sceneTextures.pDepth->Get()->GetSRV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pTarget->GetWidth(), 16, pTarget->GetHeight(), 16));
			});

	RGTexture* pAoIntermediate = graph.Create("Intermediate AO", sceneTextures.pAmbientOcclusion->GetDesc());

	graph.AddPass("Blur SSAO - Horizonal", RGPassFlag::Compute)
		.Read({ sceneTextures.pAmbientOcclusion, sceneTextures.pDepth })
		.Write(pAoIntermediate)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = sceneTextures.pAmbientOcclusion->Get();
				Texture* pBlurTarget = pAoIntermediate->Get();

				context.SetComputeRootSignature(m_pSSAORS);
				context.SetPipelineState(m_pSSAOBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.Horizontal = 1;
				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());

				context.SetRootConstants(0, shaderParameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pBlurTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pDepth->Get()->GetSRV(),
					pTarget->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pBlurTarget->GetWidth(), 256, pBlurTarget->GetHeight(), 1));
			});

	graph.AddPass("Blur SSAO - Vertical", RGPassFlag::Compute)
		.Read({ pAoIntermediate, sceneTextures.pDepth })
		.Write(sceneTextures.pAmbientOcclusion)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = sceneTextures.pAmbientOcclusion->Get();
				Texture* pBlurSource = pAoIntermediate->Get();

				context.SetComputeRootSignature(m_pSSAORS);
				context.SetPipelineState(m_pSSAOBlurPSO);

				struct
				{
					Vector2 DimensionsInv;
					uint32 Horizontal;
				} shaderParameters;

				shaderParameters.DimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
				shaderParameters.Horizontal = 0;

				context.SetRootConstants(0, shaderParameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pDepth->Get()->GetSRV(),
					pBlurSource->GetSRV(),
					});

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pBlurSource->GetWidth(), 1, pBlurSource->GetHeight(), 256));
			});
}
