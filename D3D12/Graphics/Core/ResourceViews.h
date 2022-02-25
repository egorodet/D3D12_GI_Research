#pragma once
#include "GraphicsResource.h"
#include "DescriptorHandle.h"

class Buffer;
class Texture;
class GraphicsResource;

struct BufferUAVDesc
{
	BufferUAVDesc(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool raw = false, bool counter = false)
		: Format(format), Raw(raw), Counter(counter)
	{}

	static BufferUAVDesc CreateRaw()
	{
		return BufferUAVDesc(DXGI_FORMAT_UNKNOWN, true, false);
	}

	DXGI_FORMAT Format;
	bool Raw;
	bool Counter;
};

struct BufferSRVDesc
{
	BufferSRVDesc(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool raw = false, uint32 elementOffset = 0, uint32 numElements = 0)
		: Format(format), Raw(raw), ElementOffset(elementOffset), NumElements(numElements)
	{}

	DXGI_FORMAT Format;
	bool Raw;
	uint32 ElementOffset;
	uint32 NumElements;
};

struct TextureSRVDesc
{
	TextureSRVDesc(uint8 mipLevel = 0)
		: MipLevel(mipLevel)
	{}

	uint8 MipLevel;

	bool operator==(const TextureSRVDesc& other) const
	{
		return MipLevel == other.MipLevel;
	}
};

struct TextureUAVDesc
{
	explicit TextureUAVDesc(uint8 mipLevel)
		: MipLevel(mipLevel)
	{}

	uint8 MipLevel;

	bool operator==(const TextureUAVDesc& other) const
	{
		return MipLevel == other.MipLevel;
	}
};

class ResourceView
{
public:
	virtual ~ResourceView() = default;
	GraphicsResource* GetParent() const { return m_pParent; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor() const { return m_Descriptor; }
	uint32 GetHeapIndex() const { return m_GpuDescriptor.HeapIndex; }
	uint64 GetGPUView() const { return m_GpuDescriptor.GpuHandle.ptr; }
protected:
	GraphicsResource* m_pParent = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_Descriptor = {};
	DescriptorHandle m_GpuDescriptor;
};

class ShaderResourceView : public ResourceView
{
public:
	~ShaderResourceView();
	void Create(Buffer* pBuffer, const BufferSRVDesc& desc);
	void Create(Texture* pTexture, const TextureSRVDesc& desc);
	void Release();
};

class UnorderedAccessView : public ResourceView
{
public:
	~UnorderedAccessView();
	void Create(Buffer* pBuffer, const BufferUAVDesc& desc);
	void Create(Texture* pTexture, const TextureUAVDesc& desc);
	void Release();

	Buffer* GetCounter() const { return m_pCounter; }
	UnorderedAccessView* GetCounterUAV() const;
	ShaderResourceView* GetCounterSRV() const;

private:
	RefCountPtr<Buffer> m_pCounter;
};
