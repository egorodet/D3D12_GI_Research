#pragma once
#include "GraphicsResource.h"
#include "OnlineDescriptorAllocator.h"

class GraphicsResource;
class Texture;
class OnlineDescriptorAllocator;
class RootSignature;
class PipelineState;
class StateObject;
class DynamicResourceAllocator;
class DynamicAllocationManager;
class Buffer;
class CommandSignature;
class ShaderBindingTable;
class ResourceView;
struct VertexBufferView;
struct IndexBufferView;
struct BufferView;
struct DynamicAllocation;

enum class CommandListContext
{
	Graphics,
	Compute,
	Invalid,
};

enum class RenderTargetLoadAction : uint8
{
	DontCare,
	Load,
	Clear,
	NoAccess
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetLoadAction)

enum class RenderTargetStoreAction : uint8
{
	DontCare,
	Store,
	Resolve,
	NoAccess,
};
DEFINE_ENUM_FLAG_OPERATORS(RenderTargetStoreAction)

enum class RenderPassAccess : uint8
{
#define COMBINE_ACTIONS(load, store) (uint8)RenderTargetLoadAction::load << 4 | (uint8)RenderTargetStoreAction::store
	DontCare_DontCare = COMBINE_ACTIONS(DontCare, DontCare),
	DontCare_Store = COMBINE_ACTIONS(DontCare, Store),
	Clear_Store = COMBINE_ACTIONS(Clear, Store),
	Load_Store = COMBINE_ACTIONS(Load, Store),
	Clear_DontCare = COMBINE_ACTIONS(Clear, DontCare),
	Load_DontCare = COMBINE_ACTIONS(Load, DontCare),
	Clear_Resolve = COMBINE_ACTIONS(Clear, Resolve),
	Load_Resolve = COMBINE_ACTIONS(Load, Resolve),
	DontCare_Resolve = COMBINE_ACTIONS(DontCare, Resolve),
	NoAccess = COMBINE_ACTIONS(NoAccess, NoAccess),
#undef COMBINE_ACTIONS
};

struct RenderPassInfo
{
	struct RenderTargetInfo
	{
		RenderPassAccess Access = RenderPassAccess::DontCare_DontCare;
		Texture* Target = nullptr;
		Texture* ResolveTarget = nullptr;
		int MipLevel = 0;
		int ArrayIndex = 0;
	};

	struct DepthTargetInfo
	{
		RenderPassAccess Access = RenderPassAccess::DontCare_DontCare;
		RenderPassAccess StencilAccess = RenderPassAccess::DontCare_DontCare;
		Texture* Target = nullptr;
		bool Write = true;
	};

	RenderPassInfo() = default;

	RenderPassInfo(Texture* pDepthBuffer, RenderPassAccess access, bool uavWrites = false)
		: RenderTargetCount(0)
	{
		DepthStencilTarget.Access = access;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = RenderPassAccess::NoAccess;
		DepthStencilTarget.Write = true;
		WriteUAVs = uavWrites;
	}

	RenderPassInfo(Texture* pRenderTarget, RenderPassAccess renderTargetAccess, Texture* pDepthBuffer, RenderPassAccess depthAccess, bool depthWrite, bool uavWrites = false, RenderPassAccess stencilAccess = RenderPassAccess::NoAccess)
		: RenderTargetCount(1)
	{
		RenderTargets[0].Access = renderTargetAccess;
		RenderTargets[0].Target = pRenderTarget;
		DepthStencilTarget.Access = depthAccess;
		DepthStencilTarget.Target = pDepthBuffer;
		DepthStencilTarget.StencilAccess = stencilAccess;
		DepthStencilTarget.Write = depthWrite;
		WriteUAVs = uavWrites;
	}

	static RenderTargetLoadAction GetBeginAccess(RenderPassAccess access)
	{
		return (RenderTargetLoadAction)((uint8)access >> 4);
	}

	static RenderTargetStoreAction GetEndAccess(RenderPassAccess access)
	{
		return (RenderTargetStoreAction)((uint8)access & 0b1111);
	}

	bool WriteUAVs = false;
	uint32 RenderTargetCount = 0;
	std::array<RenderTargetInfo, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> RenderTargets{};
	DepthTargetInfo DepthStencilTarget{};
};

class ResourceBarrierBatcher
{
public:
	void AddTransition(ID3D12Resource* pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState, uint32 subResource);
	void AddUAV(ID3D12Resource* pResource);
	void Flush(ID3D12GraphicsCommandList* pCmdList);
	void Reset();
	bool HasWork() const { return m_QueuedBarriers.size() > 0; }

private:
	std::vector<D3D12_RESOURCE_BARRIER> m_QueuedBarriers;
};

namespace ComputeUtils
{
	inline IntVector3 GetNumThreadGroups(uint32 threadsX = 1, uint32 groupSizeX = 1, uint32 threadsY = 1, uint32 groupSizeY = 1, uint32 threadsZ = 1, uint32 groupSizeZ = 1)
	{
		IntVector3 groups;
		groups.x = Math::DivideAndRoundUp(threadsX, groupSizeX);
		groups.y = Math::DivideAndRoundUp(threadsY, groupSizeY);
		groups.z = Math::DivideAndRoundUp(threadsZ, groupSizeZ);
		return groups;
	}
}

class CommandContext : public GraphicsObject
{
public:

	CommandContext(GraphicsDevice* pParent, ID3D12GraphicsCommandList* pCommandList, D3D12_COMMAND_LIST_TYPE type, GlobalOnlineDescriptorHeap* pDescriptorHeap, DynamicAllocationManager* pDynamicMemoryManager, ID3D12CommandAllocator* pAllocator);
	~CommandContext();

	void Reset();
	uint64 Execute(bool wait);
	static uint64 Execute(CommandContext** pContexts, uint32 numContexts, bool wait);
	void Free(uint64 fenceValue);

	void InsertResourceBarrier(GraphicsResource* pBuffer, D3D12_RESOURCE_STATES state, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	void InsertUavBarrier(GraphicsResource* pBuffer = nullptr);
	void FlushResourceBarriers();

	void CopyTexture(GraphicsResource* pSource, GraphicsResource* pTarget);
	void CopyTexture(Texture* pSource, Buffer* pDestination, const D3D12_BOX& sourceRegion, uint32 sourceSubregion = 0, uint32 destinationOffset = 0);
	void CopyTexture(Texture* pSource, Texture* pDestination, const D3D12_BOX& sourceRegion, const D3D12_BOX& destinationRegion, uint32 sourceSubregion = 0, uint32 destinationSubregion = 0);
	void CopyBuffer(Buffer* pSource, Buffer* pDestination, uint64 size, uint64 sourceOffset, uint64 destinationOffset);
	void InitializeBuffer(Buffer* pResource, const void* pData, uint64 dataSize, uint64 offset = 0);
	void InitializeTexture(Texture* pResource, D3D12_SUBRESOURCE_DATA* pSubResourceDatas, uint32 firstSubResource, uint32 subResourceCount);

	void Dispatch(uint32 groupCountX, uint32 groupCountY = 1, uint32 groupCountZ = 1);
	void Dispatch(const IntVector3& groupCounts);
	void DispatchMesh(uint32 groupCountX, uint32 groupCountY = 1, uint32 groupCountZ = 1);
	void DispatchMesh(const IntVector3& groupCounts);
	void ExecuteIndirect(CommandSignature* pCommandSignature, uint32 maxCount, Buffer* pIndirectArguments, Buffer* pCountBuffer, uint32 argumentsOffset = 0, uint32 countOffset = 0);
	void Draw(uint32 vertexStart, uint32 vertexCount);
	void DrawIndexed(uint32 indexCount, uint32 indexStart, uint32 minVertex = 0);
	void DrawIndexedInstanced(uint32 indexCount, uint32 indexStart, uint32 instanceCount, uint32 minVertex = 0, uint32 instanceStart = 0);
	void DispatchRays(ShaderBindingTable& table, uint32 width = 1, uint32 height = 1, uint32 depth = 1);

	void ClearColor(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const Color& color = Color(0.0f, 0.0f, 0.0f, 1.0f));
	void ClearDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, float depth = 1.0f, unsigned char stencil = 0);
	void ResolveResource(Texture* pSource, uint32 sourceSubResource, Texture* pTarget, uint32 targetSubResource, DXGI_FORMAT format);

	void BeginRenderPass(const RenderPassInfo& renderPassInfo);
	void EndRenderPass();

	void ClearUavUInt(GraphicsResource* pBuffer, UnorderedAccessView* pUav, uint32* values = nullptr);
	void ClearUavFloat(GraphicsResource* pBuffer, UnorderedAccessView* pUav, float* values = nullptr);

	void SetPipelineState(PipelineState* pPipelineState);
	void SetPipelineState(StateObject* pStateObject);

	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY type);
	void SetVertexBuffers(const VertexBufferView* pBuffers, uint32 bufferCount);
	void SetIndexBuffer(const IndexBufferView& indexBuffer);
	void SetViewport(const FloatRect& rect, float minDepth = 0.0f, float maxDepth = 1.0f);
	void SetScissorRect(const FloatRect& rect);

	void SetShadingRate(D3D12_SHADING_RATE shadingRate = D3D12_SHADING_RATE_1X1);
	void SetShadingRateImage(Texture* pTexture);

	void SetGraphicsRootSignature(RootSignature* pRootSignature);
	void SetComputeRootSignature(RootSignature* pRootSignature);
	void BindResource(uint32 rootIndex, uint32 offset, ResourceView* pView);
	void BindResources(uint32 rootIndex, uint32 offset, const D3D12_CPU_DESCRIPTOR_HANDLE* handles, uint32 count = 1);
	void SetDynamicVertexBuffer(uint32 slot, uint32 elementCount, uint32 elementSize, const void* pData);
	void SetDynamicIndexBuffer(uint32 elementCount, const void* pData, bool smallIndices = false);
	void SetRootSRV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void SetRootUAV(uint32 rootIndex, D3D12_GPU_VIRTUAL_ADDRESS address);
	void SetRootConstants(uint32 rootIndex, uint32 count, const void* pConstants);
	template<typename T>
	void SetRootConstants(uint32 rootIndex, const T& data)
	{
		static_assert(!std::is_pointer<T>::value, "Provided type is a pointer. This is probably unintentional.");
		SetRootConstants(rootIndex, sizeof(T) / sizeof(int32), &data);
	}
	void SetRootCBV(uint32 rootIndex, const void* pData, uint32 dataSize);
	template<typename T>
	void SetRootCBV(uint32 rootIndex, const T& data)
	{
		static_assert(!std::is_pointer<T>::value, "Provided type is a pointer. This is probably unintentional.");
		SetRootCBV(rootIndex, &data, sizeof(T));
	}

	DynamicAllocation AllocateTransientMemory(uint64 size, uint32 alignment = 256u);

	ID3D12GraphicsCommandList* GetCommandList() const { return m_pCommandList; }
	ID3D12GraphicsCommandList4* GetRaytracingCommandList() const { return  m_pRaytracingCommandList.Get(); }

	D3D12_COMMAND_LIST_TYPE GetType() const { return m_Type; }
	const PipelineState* GetCurrentPSO() const { return m_pCurrentPSO; }

	static bool IsTransitionAllowed(D3D12_COMMAND_LIST_TYPE commandlistType, D3D12_RESOURCE_STATES state);

	struct PendingBarrier
	{
		GraphicsResource* pResource;
		ResourceState State;
		uint32 Subresource;
	};

	const std::vector<PendingBarrier>& GetPendingBarriers() const { return m_PendingBarriers; }

	D3D12_RESOURCE_STATES GetResourceState(GraphicsResource* pResource, uint32 subResource) const
	{
		auto it = m_ResourceStates.find(pResource);
		check(it != m_ResourceStates.end());
		return it->second.Get(subResource);
	}

private:
	void PrepareDraw();

	std::vector<PendingBarrier> m_PendingBarriers;

	D3D12_RESOURCE_STATES GetResourceStateWithFallback(GraphicsResource* pResource, uint32 subResource) const
	{
		auto it = m_ResourceStates.find(pResource);
		if (it == m_ResourceStates.end())
		{
			return pResource->GetResourceState(subResource);
		}
		return it->second.Get(subResource);
	}

	OnlineDescriptorAllocator m_ShaderResourceDescriptorAllocator;

	ResourceBarrierBatcher m_BarrierBatcher;

	std::unique_ptr<DynamicResourceAllocator> m_pDynamicAllocator;
	ID3D12GraphicsCommandList* m_pCommandList;
	RefCountPtr<ID3D12GraphicsCommandList4> m_pRaytracingCommandList;
	RefCountPtr<ID3D12GraphicsCommandList6> m_pMeshShadingCommandList;
	ID3D12CommandAllocator* m_pAllocator;
	D3D12_COMMAND_LIST_TYPE m_Type;
	std::unordered_map<GraphicsResource*, ResourceState> m_ResourceStates;
	CommandListContext m_CurrentCommandContext = CommandListContext::Invalid;
	std::array<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> m_ResolveSubResourceParameters{};
	RenderPassInfo m_CurrentRenderPassInfo;
	bool m_InRenderPass = false;

	PipelineState* m_pCurrentPSO = nullptr;
	StateObject* m_pCurrentSO = nullptr;
};

class CommandSignature : public GraphicsObject
{
public:
	CommandSignature(GraphicsDevice* pParent);
	void Finalize(const char* pName);

	void SetRootSignature(ID3D12RootSignature* pRootSignature) { m_pRootSignature = pRootSignature; }
	void AddDispatch();
	void AddDispatchMesh();
	void AddDraw();
	void AddDrawIndexed();
	void AddConstants(uint32 numConstants, uint32 rootIndex, uint32 offset);
	void AddConstantBufferView(uint32 rootIndex);
	void AddShaderResourceView(uint32 rootIndex);
	void AddUnorderedAccessView(uint32 rootIndex);
	void AddVertexBuffer(uint32 slot);
	void AddIndexBuffer();

	ID3D12CommandSignature* GetCommandSignature() const { return m_pCommandSignature.Get(); }
	bool IsCompute() const { return m_IsCompute; }

private:
	RefCountPtr<ID3D12CommandSignature> m_pCommandSignature;
	ID3D12RootSignature* m_pRootSignature = nullptr;
	uint32 m_Stride = 0;
	std::vector<D3D12_INDIRECT_ARGUMENT_DESC> m_ArgumentDesc;
	bool m_IsCompute = false;
};
