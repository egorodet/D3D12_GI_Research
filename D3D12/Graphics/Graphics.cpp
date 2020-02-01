#include "stdafx.h"
#include "Graphics.h"
#include "CommandAllocatorPool.h"
#include "CommandQueue.h"
#include "CommandContext.h"
#include "DescriptorAllocator.h"
#include "GraphicsResource.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Mesh.h"
#include "DynamicResourceAllocator.h"
#include "ImGuiRenderer.h"
#include "Core/Input.h"
#include "Texture.h"
#include "GraphicsBuffer.h"
#include "Profiler.h"
#include "ClusteredForward.h"
#include "Scene/Camera.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/Blackboard.h"
#include "RenderGraph/ResourceAllocator.h"

const DXGI_FORMAT Graphics::DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
const DXGI_FORMAT Graphics::DEPTH_STENCIL_SHADOW_FORMAT = DXGI_FORMAT_D16_UNORM;
const DXGI_FORMAT Graphics::RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

bool gSortOpaqueMeshes = true;
bool gSortTransparentMeshes = true;

Graphics::Graphics(uint32 width, uint32 height, int sampleCount /*= 1*/)
	: m_WindowWidth(width), m_WindowHeight(height), m_SampleCount(sampleCount)
{

}

Graphics::~Graphics()
{
}

void Graphics::Initialize(HWND window)
{
	m_pWindow = window;

	m_pCamera = std::make_unique<FreeCamera>(this);
	m_pCamera->SetPosition(Vector3(0, 100, -15));
	m_pCamera->SetRotation(Quaternion::CreateFromYawPitchRoll(XM_PIDIV4, XM_PIDIV4, 0));
	m_pCamera->SetNearPlane(500.0f);
	m_pCamera->SetFarPlane(2.0f);
	m_pCamera->SetViewport(0, 0, 1, 1);

	Shader::AddGlobalShaderDefine("BLOCK_SIZE", std::to_string(FORWARD_PLUS_BLOCK_SIZE));
	Shader::AddGlobalShaderDefine("SHADOWMAP_DX", std::to_string(1.0f / SHADOW_MAP_SIZE));
	Shader::AddGlobalShaderDefine("PCF_KERNEL_SIZE", std::to_string(5));
	Shader::AddGlobalShaderDefine("MAX_SHADOW_CASTERS", std::to_string(MAX_SHADOW_CASTERS));

	InitD3D();
	InitializeAssets();

	RandomizeLights(m_DesiredLightCount);
}

void Graphics::RandomizeLights(int count)
{
	m_Lights.resize(count);

	BoundingBox sceneBounds;
	sceneBounds.Center = Vector3(0, 70, 0);
	sceneBounds.Extents = Vector3(140, 70, 60);

	int lightIndex = 0;
	m_Lights[lightIndex] = Light::Point(Vector3(0, 20, 0), 200);
	m_Lights[lightIndex].ShadowIndex = lightIndex;

	int randomLightsStartIndex = lightIndex + 1;

	for (int i = randomLightsStartIndex; i < m_Lights.size(); ++i)
	{
		Vector3 c = Vector3(Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f), Math::RandomRange(0.0f, 1.0f));
		Vector4 color(c.x, c.y, c.z, 1);

		Vector3 position;
		position.x = Math::RandomRange(-sceneBounds.Extents.x, sceneBounds.Extents.x) + sceneBounds.Center.x;
		position.y = Math::RandomRange(-sceneBounds.Extents.y, sceneBounds.Extents.y) + sceneBounds.Center.y;
		position.z = Math::RandomRange(-sceneBounds.Extents.z, sceneBounds.Extents.z) + sceneBounds.Center.z;

		const float range = Math::RandomRange(7.0f, 12.0f);
		const float angle = Math::RandomRange(30.0f, 60.0f);

		Light::Type type = rand() % 2 == 0 ? Light::Type::Point : Light::Type::Spot;
		switch (type)
		{
		case Light::Type::Point:
			m_Lights[i] = Light::Point(position, range, 1.0f, 0.5f, color);
			break;
		case Light::Type::Spot:
			m_Lights[i] = Light::Spot(position, range, Math::RandVector(), angle, 1.0f, 0.5f, color);
			break;
		case Light::Type::Directional:
		case Light::Type::MAX:
		default:
			assert(false);
			break;
		}
	}

	//It's a bit weird but I don't sort the lights that I manually created because I access them by their original index during the update function
	std::sort(m_Lights.begin() + randomLightsStartIndex, m_Lights.end(), [](const Light& a, const Light& b) { return (int)a.LightType < (int)b.LightType; });

	IdleGPU();
	if (m_pLightBuffer->GetElementCount() != count)
	{
		m_pLightBuffer->Create(this, sizeof(Light), count);
		m_pLightBuffer->SetName("Light Buffer");
	}
	CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_pLightBuffer->SetData(pContext, m_Lights.data(), sizeof(Light) * m_Lights.size());
	pContext->Execute(true);
}

void Graphics::Update()
{
	Profiler::Instance()->Begin("Update Game State");
	//Render forward+ tiles

	m_pCamera->Update();
	if (Input::Instance().IsKeyPressed('P'))
	{
		m_UseDebugView = !m_UseDebugView;
	}
	if (Input::Instance().IsKeyPressed('O'))
	{
		RandomizeLights(m_DesiredLightCount);
	}

	for (Light& light : m_Lights)
	{
		float length = light.Position.Length();
	}

	std::sort(m_TransparantBatches.begin(), m_TransparantBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist > bDist;
		});

	std::sort(m_OpaqueBatches.begin(), m_OpaqueBatches.end(), [this](const Batch& a, const Batch& b) {
		float aDist = Vector3::DistanceSquared(a.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b.pMesh->GetBounds().Center, m_pCamera->GetPosition());
		return aDist < bDist;
		});

	//PER FRAME CONSTANTS
	/////////////////////////////////////////
	struct PerFrameData
	{
		Matrix ViewInverse;
	} frameData;

	//Camera constants
	frameData.ViewInverse = m_pCamera->GetViewInverse();

	// SHADOW MAP PARTITIONING
	/////////////////////////////////////////

	struct LightData
	{
		Matrix LightViewProjections[MAX_SHADOW_CASTERS];
		Vector4 ShadowMapOffsets[MAX_SHADOW_CASTERS];
	} lightData;

	Matrix projection = XMMatrixPerspectiveFovLH(Math::PIDIV2, 1.0f, m_Lights[0].Range, 0.1f);
	
	m_ShadowCasters = 0;
	/*lightData.LightViewProjections[m_ShadowCasters] = Matrix::CreateLookAt(m_Lights[0].Position, m_Lights[0].Position + Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)) * projection;
	lightData.ShadowMapOffsets[m_ShadowCasters].x = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].y = 0.0f;
	lightData.ShadowMapOffsets[m_ShadowCasters].z = 1.0f;
	++m_ShadowCasters;*/

	////////////////////////////////
	// LET THE RENDERING BEGIN!
	////////////////////////////////
	
	Profiler::Instance()->End();

	BeginFrame();

	uint64 nextFenceValue = 0;
	uint64 lightCullingFence = 0;

	if (m_RenderPath == RenderPath::Tiled)
	{
		RG::RenderGraph graph(m_pGraphAllocator.get());
		RG::Blackboard MainBlackboard;
		struct MainData
		{
			RG_BLACKBOARD_DATA(MainData)
			RG::ResourceHandleMutable DepthStencil;
			RG::ResourceHandleMutable DepthStencilResolved;
		};
		MainData& Data = MainBlackboard.Add<MainData>();
		Data.DepthStencil = graph.ImportTexture("Depth Stencil", GetDepthStencil());
		Data.DepthStencilResolved = graph.ImportTexture("Depth Stencil Target", GetResolvedDepthStencil());

		Profiler::Instance()->Begin("Forward+");
		//1. DEPTH PREPASS
		// - Depth only pass that renders the entire scene
		// - Optimization that prevents wasteful lighting calculations during the base pass
		// - Required for light culling
		{
			struct DepthPrepassData
			{
				RG::ResourceHandleMutable StencilTarget;
			};

			RG::RenderPass<DepthPrepassData>& prepass = graph.AddCallbackPass<DepthPrepassData>("Depth Prepass", [&](RG::RenderPassBuilder& builder, DepthPrepassData& data)
				{
					MainData& Main = MainBlackboard.Get<MainData>();
					data.StencilTarget = builder.Write(Main.DepthStencil);
					Main.DepthStencil = data.StencilTarget;
				},
				[=](CommandContext& renderContext, const RG::RenderPassResources& resources, const DepthPrepassData& data)
				{
					Texture* pDepthStencil = resources.GetResource<Texture>(data.StencilTarget);
					const TextureDesc& desc = pDepthStencil->GetDesc();
					renderContext.InsertResourceBarrier(pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE);

					renderContext.BeginRenderPass(RenderPassInfo(pDepthStencil, RenderPassAccess::Clear_Store));

					renderContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					renderContext.SetViewport(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));
					renderContext.SetScissorRect(FloatRect(0, 0, (float)desc.Width, (float)desc.Height));

					struct PerObjectData
					{
						Matrix WorldViewProjection;
					} ObjectData;

					renderContext.SetGraphicsPipelineState(m_pDepthPrepassPSO.get());
					renderContext.SetGraphicsRootSignature(m_pDepthPrepassRS.get());
					for (const Batch& b : m_OpaqueBatches)
					{
						ObjectData.WorldViewProjection = m_pCamera->GetViewProjection();
						renderContext.SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
						b.pMesh->Draw(&renderContext);
					}
					renderContext.EndRenderPass();
				});
		}


		//2. [OPTIONAL] DEPTH RESOLVE
		// - If MSAA is enabled, run a compute shader to resolve the depth buffer
		if (m_SampleCount > 1)
		{
			struct DepthResolveData
			{
				RG::ResourceHandle StencilSource;
				RG::ResourceHandleMutable StencilTarget;
			};

			graph.AddCallbackPass<DepthResolveData>("Depth Resolve", [&](RG::RenderPassBuilder& builder, DepthResolveData& data)
				{
					MainData& Main = MainBlackboard.Get<MainData>();
					data.StencilSource = builder.Read(Main.DepthStencil);
					data.StencilTarget = builder.Write(Main.DepthStencilResolved);
					Main.DepthStencilResolved = data.StencilTarget;
				},
				[=](CommandContext& renderContext, const RG::RenderPassResources& resources, const DepthResolveData& data)
				{
					renderContext.InsertResourceBarrier(resources.GetResource<Texture>(data.StencilSource), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
					renderContext.InsertResourceBarrier(resources.GetResource<Texture>(data.StencilTarget), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false);

					renderContext.SetComputeRootSignature(m_pResolveDepthRS.get());
					renderContext.SetComputePipelineState(m_pResolveDepthPSO.get());

					renderContext.SetDynamicDescriptor(0, 0, resources.GetResource<Texture>(data.StencilTarget)->GetUAV());
					renderContext.SetDynamicDescriptor(1, 0, resources.GetResource<Texture>(data.StencilSource)->GetSRV());

					int dispatchGroupsX = Math::RoundUp((float)m_WindowWidth / 16);
					int dispatchGroupsY = Math::RoundUp((float)m_WindowHeight / 16);
					renderContext.Dispatch(dispatchGroupsX, dispatchGroupsY, 1);

					renderContext.InsertResourceBarrier(resources.GetResource<Texture>(data.StencilTarget), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
				});
		}

		graph.Compile();
		int64 fence = graph.Execute(this);

		static bool written = false;
		if (written == false)
		{
			graph.DumpGraphMermaid("graph.html");
			written = true;
		}

		WaitForFence(fence);

		//3. LIGHT CULLING
		// - Compute shader to buckets lights in tiles depending on their screen position.
		// - Requires a depth buffer 
		// - Outputs a: - Texture containing a count and an offset of lights per tile.
		//				- uint[] index buffer to indicate what lights are visible in each tile.
		{
			CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COMPUTE);
			Profiler::Instance()->Begin("Light Culling", pContext);
			Profiler::Instance()->Begin("Setup Light Data", pContext);
			uint32 zero[] = { 0, 0 };
			m_pLightIndexCounter->SetData(pContext, &zero, sizeof(uint32) * 2);
			m_pLightBuffer->SetData(pContext, m_Lights.data(), (uint32)m_Lights.size() * sizeof(Light));
			Profiler::Instance()->End(pContext);

			pContext->SetComputePipelineState(m_pComputeLightCullPSO.get());
			pContext->SetComputeRootSignature(m_pComputeLightCullRS.get());

			struct ShaderParameters
			{
				Matrix CameraView;
				Matrix ProjectionInverse;
				uint32 NumThreadGroups[4];
				Vector2 ScreenDimensions;
				uint32 LightCount;
			} Data;

			Data.CameraView = m_pCamera->GetView();
			Data.NumThreadGroups[0] = Math::RoundUp((float)m_WindowWidth / FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups[1] = Math::RoundUp((float)m_WindowHeight / FORWARD_PLUS_BLOCK_SIZE);
			Data.NumThreadGroups[2] = 1;
			Data.ScreenDimensions.x = (float)m_WindowWidth;
			Data.ScreenDimensions.y = (float)m_WindowHeight;
			Data.LightCount = (uint32)m_Lights.size();
			Data.ProjectionInverse = m_pCamera->GetProjectionInverse();

			pContext->SetComputeDynamicConstantBufferView(0, &Data, sizeof(ShaderParameters));
			pContext->SetDynamicDescriptor(1, 0, m_pLightIndexCounter->GetUAV());
			pContext->SetDynamicDescriptor(1, 1, m_pLightIndexListBufferOpaque->GetUAV());
			pContext->SetDynamicDescriptor(1, 2, m_pLightGridOpaque->GetUAV());
			pContext->SetDynamicDescriptor(1, 3, m_pLightIndexListBufferTransparant->GetUAV());
			pContext->SetDynamicDescriptor(1, 4, m_pLightGridTransparant->GetUAV());
			pContext->SetDynamicDescriptor(2, 0, GetResolvedDepthStencil()->GetSRV());
			pContext->SetDynamicDescriptor(2, 1, m_pLightBuffer->GetSRV());

			pContext->Dispatch(Data.NumThreadGroups[0], Data.NumThreadGroups[1], Data.NumThreadGroups[2]);
			Profiler::Instance()->End(pContext);

			lightCullingFence = pContext->Execute(false);
		}

		//4. SHADOW MAPPING
		// - Renders the scene depth onto a separate depth buffer from the light's view
		if (m_ShadowCasters > 0)
		{
			CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);

			Profiler::Instance()->Begin("Shadows", pContext);
			pContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

			pContext->BeginRenderPass(RenderPassInfo(m_pShadowMap.get(), RenderPassAccess::Clear_Store));

			pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			for (int i = 0; i < m_ShadowCasters; ++i)
			{
				Profiler::Instance()->Begin("Light View", pContext);
				const Vector4& shadowOffset = lightData.ShadowMapOffsets[i];
				FloatRect viewport;
				viewport.Left = shadowOffset.x * (float)m_pShadowMap->GetWidth();
				viewport.Top = shadowOffset.y * (float)m_pShadowMap->GetHeight();
				viewport.Right = viewport.Left + shadowOffset.z * (float)m_pShadowMap->GetWidth();
				viewport.Bottom = viewport.Top + shadowOffset.z * (float)m_pShadowMap->GetHeight();
				pContext->SetViewport(viewport);
				pContext->SetScissorRect(viewport);

				struct PerObjectData
				{
					Matrix WorldViewProjection;
				} ObjectData;
				ObjectData.WorldViewProjection = lightData.LightViewProjections[i];

				//Opaque
				{
					Profiler::Instance()->Begin("Opaque", pContext);
					pContext->SetGraphicsPipelineState(m_pShadowsOpaquePSO.get());
					pContext->SetGraphicsRootSignature(m_pShadowsOpaqueRS.get());

					pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
					for (const Batch& b : m_OpaqueBatches)
					{
						b.pMesh->Draw(pContext);
					}
					Profiler::Instance()->End(pContext);
				}
				//Transparant
				{
					Profiler::Instance()->Begin("Transparant", pContext);
					pContext->SetGraphicsPipelineState(m_pShadowsAlphaPSO.get());
					pContext->SetGraphicsRootSignature(m_pShadowsAlphaRS.get());

					pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
					for (const Batch& b : m_TransparantBatches)
					{
						pContext->SetDynamicDescriptor(1, 0, b.pMaterial->pDiffuseTexture->GetSRV());
						b.pMesh->Draw(pContext);
					}
					Profiler::Instance()->End(pContext);
				}
				Profiler::Instance()->End(pContext);
			}

			pContext->EndRenderPass();

			Profiler::Instance()->End(pContext);
			pContext->Execute(false);
		}

		//Can't do the lighting until the light culling is complete
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->InsertWaitForFence(lightCullingFence);

		//5. BASE PASS
		// - Render the scene using the shadow mapping result and the light culling buffers
		{
			CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
			Profiler::Instance()->Begin("3D", pContext);

			pContext->SetViewport(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));
			pContext->SetScissorRect(FloatRect(0, 0, (float)m_WindowWidth, (float)m_WindowHeight));

			pContext->InsertResourceBarrier(m_pShadowMap.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pContext->InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pContext->InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pContext->InsertResourceBarrier(GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_READ);
			pContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

			pContext->BeginRenderPass(RenderPassInfo(GetCurrentRenderTarget(), RenderPassAccess::Clear_Store, GetDepthStencil(), RenderPassAccess::Load_DontCare));

			pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			struct PerObjectData
			{
				Matrix World;
				Matrix WorldViewProjection;
			} ObjectData;

			//Opaque
			{
				Profiler::Instance()->Begin("Opaque", pContext);
				pContext->SetGraphicsPipelineState(m_UseDebugView ? m_pDiffuseDebugPSO.get() : m_pDiffuseOpaquePSO.get());
				pContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

				pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
				pContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
				pContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
				pContext->SetDynamicDescriptor(4, 1, m_pLightGridOpaque->GetSRV());
				pContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferOpaque->GetSRV());
				pContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

				for (const Batch& b : m_OpaqueBatches)
				{
					ObjectData.World = XMMatrixIdentity();
					ObjectData.WorldViewProjection = ObjectData.World * m_pCamera->GetViewProjection();
					pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
					pContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					pContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
					pContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
					b.pMesh->Draw(pContext);
				}
				Profiler::Instance()->End(pContext);
			}

			//Transparant
			{
				Profiler::Instance()->Begin("Transparant", pContext);
				pContext->SetGraphicsPipelineState(m_UseDebugView ? m_pDiffuseDebugPSO.get() : m_pDiffuseAlphaPSO.get());
				pContext->SetGraphicsRootSignature(m_pDiffuseRS.get());

				pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
				pContext->SetDynamicConstantBufferView(1, &frameData, sizeof(PerFrameData));
				pContext->SetDynamicConstantBufferView(2, &lightData, sizeof(LightData));
				pContext->SetDynamicDescriptor(4, 0, m_pShadowMap->GetSRV());
				pContext->SetDynamicDescriptor(4, 1, m_pLightGridTransparant->GetSRV());
				pContext->SetDynamicDescriptor(4, 2, m_pLightIndexListBufferTransparant->GetSRV());
				pContext->SetDynamicDescriptor(4, 3, m_pLightBuffer->GetSRV());

				for (const Batch& b : m_TransparantBatches)
				{
					ObjectData.World = XMMatrixIdentity();
					ObjectData.WorldViewProjection = ObjectData.World * m_pCamera->GetViewProjection();
					pContext->SetDynamicConstantBufferView(0, &ObjectData, sizeof(PerObjectData));
					pContext->SetDynamicDescriptor(3, 0, b.pMaterial->pDiffuseTexture->GetSRV());
					pContext->SetDynamicDescriptor(3, 1, b.pMaterial->pNormalTexture->GetSRV());
					pContext->SetDynamicDescriptor(3, 2, b.pMaterial->pSpecularTexture->GetSRV());
					b.pMesh->Draw(pContext);
				}
				Profiler::Instance()->End(pContext);
			}

			Profiler::Instance()->End(pContext);

			pContext->InsertResourceBarrier(m_pLightGridOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pContext->InsertResourceBarrier(m_pLightIndexListBufferOpaque.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pContext->InsertResourceBarrier(m_pLightGridTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pContext->InsertResourceBarrier(m_pLightIndexListBufferTransparant.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			pContext->EndRenderPass();
			pContext->Execute(false);
		}
		Profiler::Instance()->End();

	}
	else if (m_RenderPath == RenderPath::Clustered)
	{
		Profiler::Instance()->Begin("Clustered Forward");
		ClusteredForwardInputResources resources;
		resources.pOpaqueBatches = &m_OpaqueBatches;
		resources.pTransparantBatches = &m_TransparantBatches;
		resources.pRenderTarget = GetCurrentRenderTarget();
		resources.pLightBuffer = m_pLightBuffer.get();
		resources.pCamera = m_pCamera.get();
		m_pClusteredForward->Execute(resources);
		Profiler::Instance()->End();
	}

	{
		CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		Profiler::Instance()->Begin("UI", pContext);
		//6. UI
		// - ImGui render, pretty straight forward
		{
			UpdateImGui();
			m_pImGuiRenderer->Render(*pContext);
		}
		Profiler::Instance()->End(pContext);

		//7. MSAA Render Target Resolve
		// - We have to resolve a MSAA render target ourselves. Unlike D3D11, this is not done automatically by the API.
		//	Luckily, there's a method that does it for us!
		{
			if (m_SampleCount > 1)
			{
				Profiler::Instance()->Begin("Resolve", pContext);
				pContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
				pContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_RESOLVE_DEST);
				pContext->FlushResourceBarriers();
				pContext->GetCommandList()->ResolveSubresource(GetCurrentBackbuffer()->GetResource(), 0, GetCurrentRenderTarget()->GetResource(), 0, RENDER_TARGET_FORMAT);
				Profiler::Instance()->End(pContext);
			}
			pContext->InsertResourceBarrier(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
			pContext->InsertResourceBarrier(GetCurrentBackbuffer(), D3D12_RESOURCE_STATE_PRESENT);
		}
		nextFenceValue = pContext->Execute(false);
	}

	//8. PRESENT
	//	- Set fence for the currently queued frame
	//	- Present the frame buffer
	//	- Wait for the next frame to be finished to start queueing work for it
	EndFrame(nextFenceValue);
}

void Graphics::Shutdown()
{
	// Wait for the GPU to be done with all resources.
	IdleGPU();
}

void Graphics::BeginFrame()
{
	m_pImGuiRenderer->NewFrame();
}

void Graphics::EndFrame(uint64 fenceValue)
{
	//This always gets me confused!
	//The 'm_CurrentBackBufferIndex' is the frame that just got queued so we set the fence value on that frame
	//We present and request the new backbuffer index and wait for that one to finish on the GPU before starting to queue work for that frame.

	++m_Frame;
	Profiler::Instance()->BeginReadback(m_Frame);
	m_FenceValues[m_CurrentBackBufferIndex] = fenceValue;
	m_pSwapchain->Present(1, 0);
	m_CurrentBackBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	WaitForFence(m_FenceValues[m_CurrentBackBufferIndex]);
	Profiler::Instance()->EndReadBack(m_Frame);
}

void Graphics::InitD3D()
{
	E_LOG(Info, "Graphics::InitD3D()");
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	//Enable debug
	ComPtr<ID3D12Debug> pDebugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));
	pDebugController->EnableDebugLayer();

	/*ComPtr<ID3D12Debug1> pDebugController1;
	HR(pDebugController->QueryInterface(IID_PPV_ARGS(&pDebugController1)));
	pDebugController1->SetEnableGPUBasedValidation(true);*/

	// Enable additional debug layers.
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	//Create the factory
	HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pFactory)));

	IDXGIAdapter* pAdapter = nullptr;
	uint32 adapterIndex = 0;
	/*while (m_pFactory->EnumAdapters(adapterIndex++, &pAdapter) == S_OK)
	{
	}*/

	//Create the device
	HR(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));

#ifdef _DEBUG
	ID3D12InfoQueue* pInfoQueue = nullptr;
	if (HR(m_pDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
	{
		// Suppress whole categories of messages
		//D3D12_MESSAGE_CATEGORY Categories[] = {};

		// Suppress messages based on their severity level
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};

		// Suppress individual messages by their ID
		D3D12_MESSAGE_ID DenyIds[] =
		{
			// This occurs when there are uninitialized descriptors in a descriptor table, even when a
			// shader does not access the missing descriptors.  I find this is common when switching
			// shader permutations and not wanting to change much code to reorder resources.
			D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,
		};

		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		//NewFilter.DenyList.NumCategories = _countof(Categories);
		//NewFilter.DenyList.pCategoryList = Categories;
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;

		pInfoQueue->PushStorageFilter(&NewFilter);
		pInfoQueue->Release();
	}
#endif

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupport{};
	if (m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupport, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5)) == S_OK)
	{
		m_RenderPassTier = featureSupport.RenderPassesTier;
	}

	//Check MSAA support
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = m_SampleCount;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	m_SampleQuality = qualityLevels.NumQualityLevels - 1;

	//Create all the required command queues
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COMPUTE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_CommandQueues[D3D12_COMMAND_LIST_TYPE_COPY] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_COPY);
	//m_CommandQueues[D3D12_COMMAND_LIST_TYPE_BUNDLE] = std::make_unique<CommandQueue>(this, D3D12_COMMAND_LIST_TYPE_BUNDLE);

	assert(m_DescriptorHeaps.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
	for (size_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		m_DescriptorHeaps[i] = std::make_unique<DescriptorAllocator>(m_pDevice.Get(), (D3D12_DESCRIPTOR_HEAP_TYPE)i);
	}

	m_pDynamicAllocationManager = std::make_unique<DynamicAllocationManager>(this);
	Profiler::Instance()->Initialize(this);

	m_pSwapchain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = m_WindowWidth;
	swapchainDesc.Height = m_WindowHeight;
	swapchainDesc.Format = RENDER_TARGET_FORMAT;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = FRAME_COUNT;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Stereo = false;
	ComPtr<IDXGISwapChain1> swapChain;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc{};
	fsDesc.RefreshRate.Denominator = 60;
	fsDesc.RefreshRate.Numerator = 1;
	fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	fsDesc.Windowed = true;
	HR(m_pFactory->CreateSwapChainForHwnd(
		m_CommandQueues[D3D12_COMMAND_LIST_TYPE_DIRECT]->GetCommandQueue(), 
		m_pWindow, 
		&swapchainDesc, 
		&fsDesc, 
		nullptr, 
		&swapChain));

	swapChain.As(&m_pSwapchain);

	//Create the textures but don't create the resources themselves yet.
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargets[i] = std::make_unique<Texture>();
	}
	m_pDepthStencil = std::make_unique<Texture>();

	if (m_SampleCount > 1)
	{
		m_pResolvedDepthStencil = std::make_unique<Texture>();
		m_pMultiSampleRenderTarget = std::make_unique<Texture>();
	}

	m_pLightGridOpaque = std::make_unique<Texture>();
	m_pLightGridTransparant = std::make_unique<Texture>();

	m_pClusteredForward = std::make_unique<ClusteredForward>(this);

	OnResize(m_WindowWidth, m_WindowHeight);

	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(this);
	m_pGraphAllocator = std::make_unique<RG::ResourceAllocator>(this);
}

void Graphics::OnResize(int width, int height)
{
	E_LOG(Info, "Graphics::OnResize()");
	m_WindowWidth = width;
	m_WindowHeight = height;

	IdleGPU();

	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		m_RenderTargets[i]->Release();
	}
	m_pDepthStencil->Release();

	//Resize the buffers
	HR(m_pSwapchain->ResizeBuffers(
		FRAME_COUNT, 
		m_WindowWidth, 
		m_WindowHeight, 
		RENDER_TARGET_FORMAT,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_CurrentBackBufferIndex = 0;

	//Recreate the render target views
	for (int i = 0; i < FRAME_COUNT; ++i)
	{
		ID3D12Resource* pResource = nullptr;
		HR(m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pResource)));
		m_RenderTargets[i]->CreateForSwapchain(this, pResource);
		m_RenderTargets[i]->SetName("Rendertarget");
	}
	if (m_SampleCount > 1)
	{
		m_pDepthStencil->Create(this, TextureDesc::CreateDepth(width, height, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
		m_pDepthStencil->SetName("Depth Stencil");
		m_pResolvedDepthStencil->Create(this, TextureDesc::Create2D(width, height, DXGI_FORMAT_R32_FLOAT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess));
		m_pResolvedDepthStencil->SetName("Resolve Depth Stencil");

		m_pMultiSampleRenderTarget->Create(this, TextureDesc::CreateRenderTarget(width, height, RENDER_TARGET_FORMAT, TextureUsage::RenderTarget, m_SampleCount, ClearBinding(Color(0, 0, 0, 0))));
		m_pMultiSampleRenderTarget->SetName("Multisample Rendertarget");
	}
	else
	{
		m_pDepthStencil->Create(this, TextureDesc::CreateDepth(width, height, DEPTH_STENCIL_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, m_SampleCount, ClearBinding(0.0f, 0)));
		m_pDepthStencil->SetName("Depth Stencil");
	}

	int frustumCountX = (int)(ceil((float)width / FORWARD_PLUS_BLOCK_SIZE));
	int frustumCountY = (int)(ceil((float)height / FORWARD_PLUS_BLOCK_SIZE));
	m_pLightGridOpaque->Create(this, TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess));
	m_pLightGridTransparant->Create(this, TextureDesc::Create2D(frustumCountX, frustumCountY, DXGI_FORMAT_R32G32_UINT, TextureUsage::ShaderResource | TextureUsage::UnorderedAccess));

	m_pClusteredForward->OnSwapchainCreated(width, height);
}

void Graphics::InitializeAssets()
{
	//Input layout
	//UNIVERSAL
	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_INPUT_ELEMENT_DESC depthOnlyInputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_INPUT_ELEMENT_DESC depthOnlyAlphaInputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	//Diffuse passes
	{
		//Shaders
		Shader vertexShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::VertexShader, "VSMain", { /*"SHADOW"*/ });
		Shader pixelShader("Resources/Shaders/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { /*"SHADOW"*/ });

		//Rootsignature
		m_pDiffuseRS = std::make_unique<RootSignature>();
		m_pDiffuseRS->FinalizeFromShader("Diffuse", vertexShader, m_pDevice.Get());

		{
			//Opaque
			m_pDiffuseOpaquePSO = std::make_unique<GraphicsPipelineState>();
			m_pDiffuseOpaquePSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
			m_pDiffuseOpaquePSO->SetRootSignature(m_pDiffuseRS->GetRootSignature());
			m_pDiffuseOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pDiffuseOpaquePSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pDiffuseOpaquePSO->SetRenderTargetFormat(RENDER_TARGET_FORMAT, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
			m_pDiffuseOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			m_pDiffuseOpaquePSO->SetDepthWrite(false);
			m_pDiffuseOpaquePSO->Finalize("Diffuse (Opaque) Pipeline", m_pDevice.Get());

			//Transparant
			m_pDiffuseAlphaPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffuseOpaquePSO.get());
			m_pDiffuseAlphaPSO->SetBlendMode(BlendMode::Alpha, false);
			m_pDiffuseAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
			m_pDiffuseAlphaPSO->Finalize("Diffuse (Alpha) Pipeline", m_pDevice.Get());

			//Debug version
			m_pDiffuseDebugPSO = std::make_unique<GraphicsPipelineState>(*m_pDiffuseOpaquePSO.get());
			m_pDiffuseDebugPSO->SetDepthTest(D3D12_COMPARISON_FUNC_EQUAL);
			Shader debugPixelShader = Shader("Resources/Shaders/Diffuse.hlsl", Shader::Type::PixelShader, "PSMain", { "DEBUG_VISUALIZE" });
			m_pDiffuseDebugPSO->SetPixelShader(debugPixelShader.GetByteCode(), debugPixelShader.GetByteCodeSize());
			m_pDiffuseDebugPSO->Finalize("Diffuse (Debug) Pipeline", m_pDevice.Get());
		}
	}

	//Shadow mapping
	//Vertex shader-only pass that writes to the depth buffer using the light matrix
	{
		//Opaque
		{
			Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

			//Rootsignature
			m_pShadowsOpaqueRS = std::make_unique<RootSignature>();
			m_pShadowsOpaqueRS->FinalizeFromShader("Shadow Mapping (Opaque)", vertexShader, m_pDevice.Get());

			//Pipeline state
			m_pShadowsOpaquePSO = std::make_unique<GraphicsPipelineState>();
			m_pShadowsOpaquePSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
			m_pShadowsOpaquePSO->SetRootSignature(m_pShadowsOpaqueRS->GetRootSignature());
			m_pShadowsOpaquePSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowsOpaquePSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowsOpaquePSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowsOpaquePSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowsOpaquePSO->SetDepthBias(-1, -5.0f, -4.0f);
			m_pShadowsOpaquePSO->Finalize("Shadow Mapping (Opaque) Pipeline", m_pDevice.Get());
		}

		//Transparant
		{
			Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain", { "ALPHA_BLEND" });
			Shader pixelShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::PixelShader, "PSMain", { "ALPHA_BLEND" });

			//Rootsignature
			m_pShadowsAlphaRS = std::make_unique<RootSignature>();
			m_pShadowsAlphaRS->FinalizeFromShader("Shadow Mapping (Transparant)", vertexShader, m_pDevice.Get());

			//Pipeline state
			m_pShadowsAlphaPSO = std::make_unique<GraphicsPipelineState>();
			m_pShadowsAlphaPSO->SetInputLayout(depthOnlyAlphaInputElements, sizeof(depthOnlyAlphaInputElements) / sizeof(depthOnlyAlphaInputElements[0]));
			m_pShadowsAlphaPSO->SetRootSignature(m_pShadowsAlphaRS->GetRootSignature());
			m_pShadowsAlphaPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
			m_pShadowsAlphaPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
			m_pShadowsAlphaPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_SHADOW_FORMAT, 1, 0);
			m_pShadowsAlphaPSO->SetCullMode(D3D12_CULL_MODE_NONE);
			m_pShadowsAlphaPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
			m_pShadowsAlphaPSO->SetDepthBias(0, 0.0f, 0.0f);
			m_pShadowsAlphaPSO->Finalize("Shadow Mapping (Alpha) Pipeline", m_pDevice.Get());
		}

		m_pShadowMap = std::make_unique<Texture>();
		m_pShadowMap->Create(this, TextureDesc::CreateDepth(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, DEPTH_STENCIL_SHADOW_FORMAT, TextureUsage::DepthStencil | TextureUsage::ShaderResource, 1, ClearBinding(1.0f, 0)));
	}

	//Depth prepass
	//Simple vertex shader to fill the depth buffer to optimize later passes
	{
		Shader vertexShader("Resources/Shaders/DepthOnly.hlsl", Shader::Type::VertexShader, "VSMain");

		//Rootsignature
		m_pDepthPrepassRS = std::make_unique<RootSignature>();
		m_pDepthPrepassRS->FinalizeFromShader("Depth Prepass", vertexShader, m_pDevice.Get());

		//Pipeline state
		m_pDepthPrepassPSO = std::make_unique<GraphicsPipelineState>();
		m_pDepthPrepassPSO->SetInputLayout(depthOnlyInputElements, sizeof(depthOnlyInputElements) / sizeof(depthOnlyInputElements[0]));
		m_pDepthPrepassPSO->SetRootSignature(m_pDepthPrepassRS->GetRootSignature());
		m_pDepthPrepassPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
		m_pDepthPrepassPSO->SetRenderTargetFormats(nullptr, 0, DEPTH_STENCIL_FORMAT, m_SampleCount, m_SampleQuality);
		m_pDepthPrepassPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER);
		m_pDepthPrepassPSO->Finalize("Depth Prepass Pipeline", m_pDevice.Get());
	}

	//Depth resolve
	//Resolves a multisampled depth buffer to a normal depth buffer
	//Only required when the sample count > 1
	if(m_SampleCount > 1)
	{
		Shader computeShader("Resources/Shaders/ResolveDepth.hlsl", Shader::Type::ComputeShader, "CSMain", { "DEPTH_RESOLVE_MIN" });

		m_pResolveDepthRS = std::make_unique<RootSignature>();
		m_pResolveDepthRS->FinalizeFromShader("Depth Resolve", computeShader, m_pDevice.Get());

		m_pResolveDepthPSO = std::make_unique<ComputePipelineState>();
		m_pResolveDepthPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pResolveDepthPSO->SetRootSignature(m_pResolveDepthRS->GetRootSignature());
		m_pResolveDepthPSO->Finalize("Resolve Depth Pipeline", m_pDevice.Get());
	}

	//Light culling
	//Compute shader that requires depth buffer and light data to place lights into tiles
	{
		Shader computeShader("Resources/Shaders/LightCulling.hlsl", Shader::Type::ComputeShader, "CSMain");

		m_pComputeLightCullRS = std::make_unique<RootSignature>();
		m_pComputeLightCullRS->FinalizeFromShader("Light Culling", computeShader, m_pDevice.Get());

		m_pComputeLightCullPSO = std::make_unique<ComputePipelineState>();
		m_pComputeLightCullPSO->SetComputeShader(computeShader.GetByteCode(), computeShader.GetByteCodeSize());
		m_pComputeLightCullPSO->SetRootSignature(m_pComputeLightCullRS->GetRootSignature());
		m_pComputeLightCullPSO->Finalize("Compute Light Culling Pipeline", m_pDevice.Get());

		m_pLightIndexCounter = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexCounter->Create(this, sizeof(uint32), 2);
		m_pLightIndexListBufferOpaque = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexListBufferOpaque->Create(this, sizeof(uint32), MAX_LIGHT_DENSITY);
		m_pLightIndexListBufferTransparant = std::make_unique<StructuredBuffer>(this);
		m_pLightIndexListBufferTransparant->Create(this, sizeof(uint32), MAX_LIGHT_DENSITY);
		m_pLightBuffer = std::make_unique<StructuredBuffer>(this);
	}

	//Geometry
	{
		CommandContext* pContext = AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_COPY);
		m_pMesh = std::make_unique<Mesh>();
		m_pMesh->Load("Resources/sponza/sponza.dae", this, pContext);
		pContext->Execute(true);

		for (int i = 0; i < m_pMesh->GetMeshCount(); ++i)
		{
			Batch b;
			b.pMesh = m_pMesh->GetMesh(i);
			b.pMaterial = &m_pMesh->GetMaterial(b.pMesh->GetMaterialId());
			b.WorldMatrix = Matrix::Identity;
			if (b.pMaterial->IsTransparent)
			{
				m_TransparantBatches.push_back(b);
			}
			else
			{
				m_OpaqueBatches.push_back(b);
			}
		}
	}
}

void Graphics::UpdateImGui()
{
	m_FrameTimes[m_Frame % m_FrameTimes.size()] = GameTimer::DeltaTime();

	ImGui::SetNextWindowPos(ImVec2(0, 0), 0, ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(250, (float)m_WindowHeight));
	ImGui::Begin("GPU Stats", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("MS: %.4f", GameTimer::DeltaTime() * 1000.0f);
	ImGui::SameLine(100);
	ImGui::Text("FPS: %.1f", 1.0f / GameTimer::DeltaTime());
	ImGui::PlotLines("Frametime", m_FrameTimes.data(), (int)m_FrameTimes.size(), m_Frame % m_FrameTimes.size(), 0, 0.0f, 0.03f, ImVec2(200, 100));

	if (ImGui::TreeNodeEx("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Combo("Render Path", (int*)& m_RenderPath, [](void* data, int index, const char** outText)
			{
				RenderPath p = (RenderPath)index;
				switch (p)
				{
				case RenderPath::Tiled:
					*outText = "Tiled";
					break;
				case RenderPath::Clustered:
					*outText = "Clustered";
					break;
				default:
					break;
				}
				return true;
			}, nullptr, 2);
		extern bool gUseAlternativeLightCulling;
		ImGui::Checkbox("Alternative Light Culling", &gUseAlternativeLightCulling);
		extern bool gVisualizeClusters;
		ImGui::Checkbox("Visualize Clusters", &gVisualizeClusters);

		ImGui::Separator();
		ImGui::SliderInt("Lights", &m_DesiredLightCount, 10, 16384);
		if (ImGui::Button("Generate Lights"))
		{
			RandomizeLights(m_DesiredLightCount);
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Descriptor Heaps", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("Used CPU Descriptor Heaps");
		for (const auto& pAllocator : m_DescriptorHeaps)
		{
			switch (pAllocator->GetType())
			{
			case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
				ImGui::TextWrapped("Constant/Shader/Unordered Access Views");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
				ImGui::TextWrapped("Samplers");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
				ImGui::TextWrapped("Render Target Views");
				break;
			case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
				ImGui::TextWrapped("Depth Stencil Views");
				break;
			default:
				break;
			}
			uint32 totalDescriptors = pAllocator->GetHeapCount() * DescriptorAllocator::DESCRIPTORS_PER_HEAP;
			uint32 usedDescriptors = pAllocator->GetNumAllocatedDescriptors();
			std::stringstream str;
			str << usedDescriptors << "/" << totalDescriptors;
			ImGui::ProgressBar((float)usedDescriptors / totalDescriptors, ImVec2(-1, 0), str.str().c_str());
		}
		ImGui::TreePop();
	}
	ImGui::End();

	static bool showOutputLog = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(250, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
	ImGui::SetNextWindowSize(ImVec2(showOutputLog ? (float)(m_WindowWidth - 250) * 0.5f : m_WindowWidth - 250, 250));
	ImGui::SetNextWindowCollapsed(!showOutputLog);

	showOutputLog = ImGui::Begin("Output Log", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (showOutputLog)
	{
		ImGui::SetScrollHereY(1.0f);
		for (const Console::LogEntry& entry : Console::GetHistory())
		{
			switch (entry.Type)
			{
			case LogType::VeryVerbose:
			case LogType::Verbose:
			case LogType::Info:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
				ImGui::TextWrapped("[Info] %s", entry.Message.c_str());
				break;
			case LogType::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
				ImGui::TextWrapped("[Warning] %s", entry.Message.c_str());
				break;
			case LogType::Error:
			case LogType::FatalError:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
				ImGui::TextWrapped("[Error] %s", entry.Message.c_str());
				break;
			}
			ImGui::PopStyleColor();
		}
	}
	ImGui::End();

	if (showOutputLog)
	{
		ImGui::SetNextWindowPos(ImVec2(250 + (m_WindowWidth - 250) / 2.0f, showOutputLog ? (float)m_WindowHeight - 250 : (float)m_WindowHeight - 20));
		ImGui::SetNextWindowSize(ImVec2((float)(m_WindowWidth - 250) * 0.5f, 250));
		ImGui::SetNextWindowCollapsed(!showOutputLog);
		ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ProfileNode* pRootNode = Profiler::Instance()->GetRootNode();
		pRootNode->RenderImGui(m_Frame);
		ImGui::End();
	}
	ImGui::PopStyleVar();
}

CommandQueue* Graphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	return m_CommandQueues.at(type).get();
}

CommandContext* Graphics::AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type)
{
	int typeIndex = (int)type;

	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	if (m_FreeCommandLists[typeIndex].size() > 0)
	{
		CommandContext* pCommandList = m_FreeCommandLists[typeIndex].front();
		m_FreeCommandLists[typeIndex].pop();
		pCommandList->Reset();
		return pCommandList;
	}
	else
	{
		ComPtr<ID3D12CommandList> pCommandList;
		ID3D12CommandAllocator* pAllocator = m_CommandQueues[type]->RequestAllocator();
		m_pDevice->CreateCommandList(0, type, pAllocator, nullptr, IID_PPV_ARGS(pCommandList.GetAddressOf()));
		m_CommandLists.push_back(std::move(pCommandList));
		m_CommandListPool[typeIndex].emplace_back(std::make_unique<CommandContext>(this, static_cast<ID3D12GraphicsCommandList*>(m_CommandLists.back().Get()), pAllocator, type));
		return m_CommandListPool[typeIndex].back().get();
	}
}

bool Graphics::IsFenceComplete(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	return pQueue->IsFenceComplete(fenceValue);
}

void Graphics::WaitForFence(uint64 fenceValue)
{
	D3D12_COMMAND_LIST_TYPE type = (D3D12_COMMAND_LIST_TYPE)(fenceValue >> 56);
	CommandQueue* pQueue = GetCommandQueue(type);
	pQueue->WaitForFence(fenceValue);
}

void Graphics::FreeCommandList(CommandContext* pCommandList)
{
	std::lock_guard<std::mutex> lockGuard(m_ContextAllocationMutex);
	m_FreeCommandLists[(int)pCommandList->GetType()].push(pCommandList);
}

bool Graphics::CheckTypedUAVSupport(DXGI_FORMAT format) const
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS featureData{};
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData)));

	switch (format)
	{
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
		// Unconditionally supported.
		return true;

	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SINT:
		// All these are supported if this optional feature is set.
		return featureData.TypedUAVLoadAdditionalFormats;

	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		// Conditionally supported by specific pDevices.
		if (featureData.TypedUAVLoadAdditionalFormats)
		{
			D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
			HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)));
			const DWORD mask = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
			return ((formatSupport.Support2 & mask) == mask);
		}
		return false;

	default:
		return false;
	}
}

bool Graphics::UseRenderPasses() const
{
	return m_RenderPassTier > D3D12_RENDER_PASS_TIER::D3D12_RENDER_PASS_TIER_0;
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::AllocateCpuDescriptors(int count, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	assert((int)type < m_DescriptorHeaps.size());
	return m_DescriptorHeaps[type]->AllocateDescriptors(count);
}

void Graphics::IdleGPU()
{
	for (auto& pCommandQueue : m_CommandQueues)
	{
		if (pCommandQueue)
		{
			pCommandQueue->WaitForIdle();
		}
	}
}

uint32 Graphics::GetMultiSampleQualityLevel(uint32 msaa)
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
	qualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	qualityLevels.Format = RENDER_TARGET_FORMAT;
	qualityLevels.NumQualityLevels = 0;
	qualityLevels.SampleCount = msaa;
	HR(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(qualityLevels)));
	return qualityLevels.NumQualityLevels - 1;
}

ID3D12Resource* Graphics::CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue /*= nullptr*/)
{
	ID3D12Resource* pResource;
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(heapType);
	HR(m_pDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, initialState, pClearValue, IID_PPV_ARGS(&pResource)));
	return pResource;
}