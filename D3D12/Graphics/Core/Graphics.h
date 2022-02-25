#pragma once

#include "Shader.h"
#include "DescriptorHandle.h"
#include "GraphicsResource.h"

class CommandQueue;
class CommandContext;
class OfflineDescriptorAllocator;
class DynamicAllocationManager;
class GraphicsResource;
class RootSignature;
class Texture;
class PipelineState;
class Buffer;
class ShaderManager;
class PipelineStateInitializer;
class StateObject;
class StateObjectInitializer;
class GlobalOnlineDescriptorHeap;
class ResourceView;
class SwapChain;
class OnlineDescriptorAllocator;
class Fence;
class GraphicsDevice;
class CommandSignature;
struct TextureDesc;
struct BufferDesc;

using WindowHandle = HWND;
using WindowHandlePtr = HWND;

enum class GraphicsInstanceFlags
{
	None			= 0,
	DebugDevice		= 1 << 0,
	DRED			= 1 << 1,
	GpuValidation	= 1 << 2,
	Pix				= 1 << 3,
};
DECLARE_BITMASK_TYPE(GraphicsInstanceFlags);

enum class DefaultTexture
{
	White2D,
	Black2D,
	Magenta2D,
	Gray2D,
	Normal2D,
	RoughnessMetalness,
	BlackCube,
	Black3D,
	ColorNoise256,
	BlueNoise512,
	MAX,
};

namespace GraphicsCommon
{
	Texture* GetDefaultTexture(DefaultTexture type);

	extern CommandSignature* pIndirectDrawSignature;
	extern CommandSignature* pIndirectDispatchSignature;
	extern CommandSignature* pIndirectDispatchMeshSignature;
}

class GraphicsInstance
{
public:
	GraphicsInstance(GraphicsInstanceFlags createFlags);
	RefCountPtr<SwapChain> CreateSwapchain(GraphicsDevice* pDevice, WindowHandle pNativeWindow, uint32 width, uint32 height, uint32 numFrames, bool vsync);
	RefCountPtr<IDXGIAdapter4> EnumerateAdapter(bool useWarp);
	RefCountPtr<GraphicsDevice> CreateDevice(RefCountPtr<IDXGIAdapter4> pAdapter);

	static GraphicsInstance CreateInstance(GraphicsInstanceFlags createFlags = GraphicsInstanceFlags::None);

	bool AllowTearing() const { return m_AllowTearing; }

private:
	RefCountPtr<IDXGIFactory6> m_pFactory;
	bool m_AllowTearing = false;
};

class SwapChain : public GraphicsObject
{
public:
	SwapChain(GraphicsDevice* pDevice, IDXGIFactory6* pFactory, WindowHandle pNativeWindow, uint32 width, uint32 height, uint32 numFrames, bool vsync);
	~SwapChain();
	void OnResize(uint32 width, uint32 height);
	void Present();

	void SetVsync(bool enabled) { m_Vsync = enabled; }
	IDXGISwapChain4* GetSwapChain() const { return m_pSwapchain.Get(); }
	Texture* GetBackBuffer() const { return m_Backbuffers[m_CurrentImage]; }
	Texture* GetBackBuffer(uint32 index) const { return m_Backbuffers[index]; }
	uint32 GetBackbufferIndex() const { return m_CurrentImage; }
	DXGI_FORMAT GetFormat() const { return m_Format; }

private:
	std::vector<RefCountPtr<Texture>> m_Backbuffers;
	RefCountPtr<IDXGISwapChain4> m_pSwapchain;
	DXGI_FORMAT m_Format;
	uint32 m_CurrentImage;
	bool m_Vsync;
};

class GraphicsCapabilities
{
public:
	void Initialize(GraphicsDevice* pDevice);

	bool SupportsRaytracing() const { return RayTracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED; }
	bool SupportsMeshShading() const { return MeshShaderSupport != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED; }
	bool SupportsVRS() const { return VRSTier != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED; }
	bool SupportsSamplerFeedback() const { return SamplerFeedbackSupport != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED; }
	void GetShaderModel(uint8& maj, uint8& min) const { maj = (uint8)(ShaderModel >> 0x4); min = (uint8)(ShaderModel & 0xF); }
	bool CheckUAVSupport(DXGI_FORMAT format) const;

	D3D12_RENDER_PASS_TIER RenderPassTier = D3D12_RENDER_PASS_TIER_0;
	D3D12_RAYTRACING_TIER RayTracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	uint16 ShaderModel = (D3D_SHADER_MODEL)0;
	D3D12_MESH_SHADER_TIER MeshShaderSupport = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	D3D12_SAMPLER_FEEDBACK_TIER SamplerFeedbackSupport = D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
	D3D12_VARIABLE_SHADING_RATE_TIER VRSTier = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
	int VRSTileSize = -1;

private:
	GraphicsDevice* m_pDevice = nullptr;
	CD3DX12FeatureSupport m_FeatureSupport;
};

class DeferredDeleteQueue : public GraphicsObject
{
private:
	struct FencedObject
	{
		Fence* pFence;
		uint64 FenceValue;
		ID3D12Object* pResource;
	};

public:
	DeferredDeleteQueue(GraphicsDevice* pParent);
	~DeferredDeleteQueue();

	void EnqueueResource(ID3D12Object* pResource, Fence* pFence);

	void Clean();
private:
	std::mutex m_QueueCS;
	std::queue<FencedObject> m_DeletionQueue;
};

class GraphicsDevice : public GraphicsObject
{
public:
	GraphicsDevice(IDXGIAdapter4* pAdapter);
	~GraphicsDevice();

	bool IsFenceComplete(uint64 fenceValue);
	void WaitForFence(uint64 fenceValue);
	void TickFrame();
	void IdleGPU();

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	GlobalOnlineDescriptorHeap* GetGlobalViewHeap() const { return m_pGlobalViewHeap; }
	GlobalOnlineDescriptorHeap* GetGlobalSamplerHeap() const { return m_pGlobalSamplerHeap; }

	template<typename DESC_TYPE>
	struct DescriptorSelector {};
	template<> struct DescriptorSelector<D3D12_SHADER_RESOURCE_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; } };
	template<> struct DescriptorSelector<D3D12_UNORDERED_ACCESS_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; } };
	template<> struct DescriptorSelector<D3D12_CONSTANT_BUFFER_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; } };
	template<> struct DescriptorSelector<D3D12_RENDER_TARGET_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_RTV; } };
	template<> struct DescriptorSelector<D3D12_DEPTH_STENCIL_VIEW_DESC> { static constexpr D3D12_DESCRIPTOR_HEAP_TYPE Type() { return D3D12_DESCRIPTOR_HEAP_TYPE_DSV; } };

	template<typename DESC_TYPE>
	D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor()
	{
		return m_DescriptorHeaps[DescriptorSelector<DESC_TYPE>::Type()]->AllocateDescriptor();
	}

	template<typename DESC_TYPE>
	void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
	{
		m_DescriptorHeaps[DescriptorSelector<DESC_TYPE>::Type()]->FreeDescriptor(descriptor);
	}

	DescriptorHandle StoreViewDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE view);
	void FreeViewDescriptor(DescriptorHandle& heapIndex);

	RefCountPtr<Texture> CreateTexture(const TextureDesc& desc, const char* pName);
	RefCountPtr<Buffer> CreateBuffer(const BufferDesc& desc, const char* pName);

	ID3D12Resource* CreateResource(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_CLEAR_VALUE* pClearValue = nullptr);
	void ReleaseResource(ID3D12Resource* pResource);
	RefCountPtr<PipelineState> CreatePipeline(const PipelineStateInitializer& psoDesc);
	RefCountPtr<PipelineState> CreatePipeline(Shader* pShader, RootSignature* pRootSignature);
	RefCountPtr<StateObject> CreateStateObject(const StateObjectInitializer& stateDesc);

	Shader* GetShader(const char* pShaderPath, ShaderType shaderType, const char* entryPoint = "", const std::vector<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const char* pShaderPath, const std::vector<ShaderDefine>& defines = {});

	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }
	ShaderManager* GetShaderManager() const { return m_pShaderManager.get(); }

	const GraphicsCapabilities& GetCapabilities() const { return Capabilities; }

	Fence* GetFrameFence() const { return m_pFrameFence; }

private:
	bool m_IsTearingDown = false;
	GraphicsCapabilities Capabilities;

	RefCountPtr<ID3D12Device> m_pDevice;
	RefCountPtr<ID3D12Device5> m_pRaytracingDevice;

	RefCountPtr<Fence> m_pFrameFence;

	std::array<RefCountPtr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<RefCountPtr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandLists;
	std::vector<RefCountPtr<ID3D12CommandList>> m_CommandLists;

	DeferredDeleteQueue m_DeleteQueue;

	HANDLE m_DeviceRemovedEvent = 0;
	RefCountPtr<Fence> m_pDeviceRemovalFence;

	std::unique_ptr<ShaderManager> m_pShaderManager;

	RefCountPtr<GlobalOnlineDescriptorHeap> m_pGlobalViewHeap;
	RefCountPtr<GlobalOnlineDescriptorHeap> m_pGlobalSamplerHeap;

	std::array<RefCountPtr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	RefCountPtr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::mutex m_ContextAllocationMutex;
};
