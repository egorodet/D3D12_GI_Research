#include "stdafx.h"
#include "RTReflections.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/ShaderBindingTable.h"
#include "Graphics/RHI/ResourceViews.h"
#include "Graphics/RHI/StateObject.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/SceneView.h"

RTReflections::RTReflections(GraphicsDevice* pDevice)
{
	if (pDevice->GetCapabilities().SupportsRaytracing())
	{
		m_pGlobalRS = new RootSignature(pDevice);
		m_pGlobalRS->AddRootConstants(0, 1);
		m_pGlobalRS->AddConstantBufferView(100);
		m_pGlobalRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4);
		m_pGlobalRS->AddDescriptorTableSimple(0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4);
		m_pGlobalRS->Finalize("Global");

		StateObjectInitializer stateDesc;
		stateDesc.Name = "RT Reflections";
		stateDesc.RayGenShader = "RayGen";
		stateDesc.AddLibrary("RayTracing/RTReflections.hlsl");
		stateDesc.AddLibrary("RayTracing/SharedRaytracingLib.hlsl", { "OcclusionMS", "MaterialCHS", "MaterialAHS", "MaterialMS" });
		stateDesc.AddHitGroup("ReflectionHitGroup", "MaterialCHS", "MaterialAHS");
		stateDesc.AddMissShader("MaterialMS");
		stateDesc.AddMissShader("OcclusionMiss");
		stateDesc.MaxPayloadSize = 6 * sizeof(float);
		stateDesc.MaxAttributeSize = 2 * sizeof(float);
		stateDesc.MaxRecursion = 2;
		stateDesc.pGlobalRootSignature = m_pGlobalRS;
		m_pRtSO = pDevice->CreateStateObject(stateDesc);
	}
}

void RTReflections::Execute(RGGraph& graph, const SceneView* pView, SceneTextures& sceneTextures)
{
	RGTexture* pReflectionsTarget = graph.Create("Scene Color", sceneTextures.pColorTarget->GetDesc());

	graph.AddPass("RT Reflections", RGPassFlag::Compute)
		.Read({ sceneTextures.pNormals, sceneTextures.pDepth, sceneTextures.pRoughness, sceneTextures.pColorTarget })
		.Write(pReflectionsTarget)
		.Bind([=](CommandContext& context)
			{
				Texture* pTarget = pReflectionsTarget->Get();

				context.SetComputeRootSignature(m_pGlobalRS);
				context.SetPipelineState(m_pRtSO);

				struct
				{
					float ViewPixelSpreadAngle;
				} parameters;

				parameters.ViewPixelSpreadAngle = atanf(2.0f * tanf(pView->View.FoV / 2) / (float)pTarget->GetHeight());

				ShaderBindingTable bindingTable(m_pRtSO);
				bindingTable.BindRayGenShader("RayGen");
				bindingTable.BindMissShader("MaterialMS", 0);
				bindingTable.BindMissShader("OcclusionMS", 1);
				bindingTable.BindHitGroup("ReflectionHitGroup", 0);

				context.SetRootConstants(0, parameters);
				context.SetRootCBV(1, Renderer::GetViewUniforms(pView, pTarget));
				context.BindResources(2, pTarget->GetUAV());
				context.BindResources(3, {
					sceneTextures.pDepth->Get()->GetSRV(),
					sceneTextures.pColorTarget->Get()->GetSRV(),
					sceneTextures.pNormals->Get()->GetSRV(),
					sceneTextures.pRoughness->Get()->GetSRV(),
					});

				context.DispatchRays(bindingTable, pTarget->GetWidth(), pTarget->GetHeight());
			});

	sceneTextures.pColorTarget = pReflectionsTarget;
}

