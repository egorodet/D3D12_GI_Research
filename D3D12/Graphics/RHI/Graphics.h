#pragma once

#include "Shader.h"
#include "DescriptorHandle.h"
#include "GraphicsResource.h"
#include "ResourceViews.h"
#include "D3DUtils.h"
#include "Fence.h"

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
class Image;
class CommandSignatureInitializer;
struct TextureDesc;
struct BufferDesc;

using WindowHandle = HWND;

enum class DisplayMode
{
	SDR,
	HDR_PQ,
	HDR_scRGB,
};

class SwapChain : public GraphicsObject
{
public:
	static constexpr uint32 NUM_FRAMES = 3;

	SwapChain(GraphicsDevice* pDevice, DisplayMode displayMode, WindowHandle pNativeWindow);
	~SwapChain();
	void OnResizeOrMove(uint32 width, uint32 height);
	void Present(bool vsync);

	void SetDisplayMode(DisplayMode displayMode) { m_DesiredDisplayMode = displayMode; }
	void SetVsync(bool enabled) { m_Vsync = enabled; }
	bool DisplaySupportsHDR() const;

	IntVector2 GetViewport() const;
	IDXGISwapChain4* GetSwapChain() const { return m_pSwapchain.Get(); }
	Texture* GetBackBuffer() const { return m_Backbuffers[m_CurrentImage]; }
	Texture* GetBackBuffer(uint32 index) const { return m_Backbuffers[index]; }
	uint32 GetBackbufferIndex() const { return m_CurrentImage; }
	DXGI_FORMAT GetFormat() const { return m_Format; }

private:
	DisplayMode m_DesiredDisplayMode;
	std::array<SyncPoint, NUM_FRAMES> m_PresentSyncPoints;
	RefCountPtr<Fence> m_pPresentFence;
	std::array<RefCountPtr<Texture>, NUM_FRAMES> m_Backbuffers;
	RefCountPtr<IDXGISwapChain4> m_pSwapchain;
	DXGI_FORMAT m_Format;
	uint32 m_CurrentImage;
	uint32 m_Width = 0;
	uint32 m_Height = 0;
	bool m_Vsync;
	bool m_AllowTearing = false;
};

struct GraphicsDeviceOptions
{
	bool UseDebugDevice = false;
	bool UseDRED = false;
	bool UseGPUValidation = false;
	bool LoadPIX = false;
	bool UseWarp = false;
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

class GraphicsDevice : public GraphicsObject
{
public:
	GraphicsDevice(GraphicsDeviceOptions options);
	~GraphicsDevice();

	void TickFrame();
	void IdleGPU();

	CommandQueue* GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const;
	CommandContext* AllocateCommandContext(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
	void FreeCommandList(CommandContext* pCommandList);

	D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type);
	void FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_CPU_DESCRIPTOR_HANDLE descriptor);
	DescriptorHandle StoreViewDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE view);
	void FreeViewDescriptor(DescriptorHandle& heapIndex);

	RefCountPtr<Texture> CreateTexture(const TextureDesc& desc, const char* pName);
	RefCountPtr<Texture> CreateTextureForSwapchain(ID3D12Resource* pSwapchainResource);
	RefCountPtr<Buffer> CreateBuffer(const BufferDesc& desc, const char* pName);
	void DeferReleaseObject(ID3D12Object* pObject);

	RefCountPtr<PipelineState> CreatePipeline(const PipelineStateInitializer& psoDesc);
	RefCountPtr<PipelineState> CreateComputePipeline(RefCountPtr<RootSignature>& pRootSignature, const char* pShaderPath, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	RefCountPtr<StateObject> CreateStateObject(const StateObjectInitializer& stateDesc);
	RefCountPtr<ShaderResourceView> CreateSRV(Buffer* pBuffer, const BufferSRVDesc& desc);
	RefCountPtr<UnorderedAccessView> CreateUAV(Buffer* pBuffer, const BufferUAVDesc& desc);
	RefCountPtr<ShaderResourceView> CreateSRV(Texture* pTexture, const TextureSRVDesc& desc);
	RefCountPtr<UnorderedAccessView> CreateUAV(Texture* pTexture, const TextureUAVDesc& desc);
	RefCountPtr<CommandSignature> CreateCommandSignature(const CommandSignatureInitializer& signatureDesc, const char* pName, RootSignature* pRootSignature = nullptr);

	Shader* GetShader(const char* pShaderPath, ShaderType shaderType, const char* entryPoint = "", const Span<ShaderDefine>& defines = {});
	ShaderLibrary* GetLibrary(const char* pShaderPath, const Span<ShaderDefine>& defines = {});

	GlobalOnlineDescriptorHeap* GetGlobalViewHeap() const { return m_pGlobalViewHeap; }
	GlobalOnlineDescriptorHeap* GetGlobalSamplerHeap() const { return m_pGlobalSamplerHeap; }
	ID3D12Device* GetDevice() const { return m_pDevice.Get(); }
	ID3D12Device5* GetRaytracingDevice() const { return m_pRaytracingDevice.Get(); }
	ShaderManager* GetShaderManager() const { return m_pShaderManager.get(); }
	const GraphicsCapabilities& GetCapabilities() const { return m_Capabilities; }
	Fence* GetFrameFence() const { return m_pFrameFence; }
	IDXGIFactory6* GetFactory() const { return m_pFactory; }

private:
	struct LiveObjectReporter
	{
		~LiveObjectReporter();
	} Reporter;

	GraphicsCapabilities m_Capabilities;

	RefCountPtr<IDXGIFactory6> m_pFactory;
	RefCountPtr<ID3D12Device> m_pDevice;
	RefCountPtr<ID3D12Device4> m_pDevice4;
	RefCountPtr<ID3D12Device5> m_pRaytracingDevice;

	class DRED
	{
	public:
		DRED(GraphicsDevice* pDevice);
		~DRED();

		RefCountPtr<Fence> m_pFence;
		HANDLE m_WaitHandle;
	};
	std::unique_ptr<DRED> m_pDRED;

	RefCountPtr<Fence> m_pFrameFence;
	std::array<RefCountPtr<CommandQueue>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandQueues;
	std::array<std::vector<RefCountPtr<CommandContext>>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_CommandListPool;
	std::array<std::queue<CommandContext*>, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE> m_FreeCommandLists;

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

	DeferredDeleteQueue m_DeleteQueue;

	std::unique_ptr<ShaderManager> m_pShaderManager;
	RefCountPtr<GlobalOnlineDescriptorHeap> m_pGlobalViewHeap;
	RefCountPtr<GlobalOnlineDescriptorHeap> m_pGlobalSamplerHeap;

	std::array<RefCountPtr<OfflineDescriptorAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> m_DescriptorHeaps;
	RefCountPtr<DynamicAllocationManager> m_pDynamicAllocationManager;

	std::mutex m_ContextAllocationMutex;
};
