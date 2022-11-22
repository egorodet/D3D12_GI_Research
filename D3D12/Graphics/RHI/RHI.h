#pragma once

class GraphicsDevice;
class PipelineState;
class StateObject;
class Texture;
class Buffer;
class RootSignature;

enum class ResourceFormat
{
	Unknown,

	R8_UINT,
	R8_SINT,
	R8_UNORM,
	R8_SNORM,
	RG8_UINT,
	RG8_SINT,
	RG8_UNORM,
	RG8_SNORM,
	R16_UINT,
	R16_SINT,
	R16_UNORM,
	R16_SNORM,
	R16_FLOAT,
	BGRA4_UNORM,
	B5G6R5_UNORM,
	B5G5R5A1_UNORM,
	RGBA8_UINT,
	RGBA8_SINT,
	RGBA8_UNORM,
	RGBA8_SNORM,
	BGRA8_UNORM,
	RGB10A2_UNORM,
	R11G11B10_FLOAT,
	RG16_UINT,
	RG16_SINT,
	RG16_UNORM,
	RG16_SNORM,
	RG16_FLOAT,
	R32_UINT,
	R32_SINT,
	R32_FLOAT,
	RGBA16_UINT,
	RGBA16_SINT,
	RGBA16_FLOAT,
	RGBA16_UNORM,
	RGBA16_SNORM,
	RG32_UINT,
	RG32_SINT,
	RG32_FLOAT,
	RGB32_UINT,
	RGB32_SINT,
	RGB32_FLOAT,
	RGBA32_UINT,
	RGBA32_SINT,
	RGBA32_FLOAT,

	BC1_UNORM,
	BC2_UNORM,
	BC3_UNORM,
	BC4_UNORM,
	BC4_SNORM,
	BC5_UNORM,
	BC5_SNORM,
	BC6H_UFLOAT,
	BC6H_SFLOAT,
	BC7_UNORM,

	D16_UNORM,
	D32_FLOAT,
	D24S8,
	D32S8,

	Num,
};

enum class FormatType
{
	Integer,
	Normalized,
	Float,
	DepthStencil
};

struct FormatInfo
{
	ResourceFormat Format;
	const char* pName;
	uint8 BytesPerBlock;
	uint8 BlockSize;
	FormatType Type;
	uint32 NumComponents;
	bool IsDepth : 1;
	bool IsStencil : 1;
	bool IsSigned : 1;
	bool IsSRGB : 1;
	bool IsBC : 1;
};

enum class ResourceAccess
{
	Unknown = 0,

	// Read-only states
	VertexBuffer				= 1u << 0u,
	ConstantBuffer				= 1u << 1u,
	IndexBuffer					= 1u << 2u,
	DSVRead						= 1u << 3u,
	SRVGraphics					= 1u << 4u,
	SRVCompute					= 1u << 5u,
	CopySrc						= 1u << 6u,
	IndirectArgs				= 1u << 7u,
	ResolveSrc					= 1u << 8u,
	AccelerationStructureRead	= 1u << 9u,
	VRS							= 1u << 10u,
	Present						= 1u << 11u,

	// Read-write states
	RTV							= 1u << 12u,
	UAV							= 1u << 13u,
	DSVWrite					= 1u << 14u,
	CopyDest					= 1u << 15u,
	ResolveDest					= 1u << 16u,
	AccelerationStructureWrite	= 1u << 17u,

	// Masks
	SRVMask = SRVGraphics | SRVCompute,
	ReadMask = VertexBuffer | ConstantBuffer | IndexBuffer | DSVRead | SRVGraphics | SRVCompute | CopySrc | IndirectArgs | ResolveSrc | AccelerationStructureRead | VRS,
	WriteMask = RTV | UAV | DSVWrite | CopyDest | ResolveDest | AccelerationStructureWrite,
};
DECLARE_BITMASK_TYPE(ResourceAccess);

namespace RHI
{
	inline std::string ResourceStateToString(ResourceAccess access)
	{
		if (access == ResourceAccess::Unknown)
		{
			return "Unknown";
		}

		char outString[1024];
		outString[0] = '\0';
		char* pCurrent = outString;
		int i = 0;
		auto AddText = [&](const char* pText)
		{
			if (i++ > 0)
				*pCurrent++ = '/';
			strcpy_s(pCurrent, 1024 - (pCurrent - outString), pText);
			size_t len = strlen(pText);
			pCurrent += len;
		};

#define STATE_CASE(name) if((access & ResourceAccess::name) == ResourceAccess::name) { AddText(#name); access &= ~ResourceAccess::name; }
		STATE_CASE(VertexBuffer);
		STATE_CASE(ConstantBuffer);
		STATE_CASE(IndexBuffer);
		STATE_CASE(DSVRead);
		STATE_CASE(SRVGraphics);
		STATE_CASE(SRVCompute);
		STATE_CASE(CopySrc);
		STATE_CASE(IndirectArgs);
		STATE_CASE(ResolveSrc);
		STATE_CASE(AccelerationStructureRead);
		STATE_CASE(VRS);
		STATE_CASE(RTV);
		STATE_CASE(UAV);
		STATE_CASE(DSVWrite);
		STATE_CASE(CopyDest);
		STATE_CASE(ResolveDest);
		STATE_CASE(AccelerationStructureWrite);
#undef STATE_CASE

		checkf(access == ResourceAccess::Unknown, "Following ResourceAccess flags are not accounted for: %s", RHI::ResourceStateToString(access).c_str());
		return outString;
	}

	const FormatInfo& GetFormatInfo(ResourceFormat format);
	const uint32 GetFormatByteSize(ResourceFormat format, uint32 width, uint32 height = 1, uint32 depth = 1);
	ResourceFormat SRVFormatFromDepth(ResourceFormat format);
	ResourceFormat DSVFormat(ResourceFormat format);
}

template<bool ThreadSafe>
struct FreeList
{
public:
	FreeList(uint32 chunkSize, bool canResize = true)
		: m_NumAllocations(0), m_ChunkSize(chunkSize), m_CanResize(canResize)
	{
		m_FreeList.resize(chunkSize);
		std::iota(m_FreeList.begin(), m_FreeList.end(), 0);
	}

	uint32 Allocate()
	{
		std::scoped_lock lock(m_Mutex);
		if (m_NumAllocations + 1 > m_FreeList.size())
		{
			check(m_CanResize);
			uint32 size = (uint32)m_FreeList.size();
			m_FreeList.resize(size + m_ChunkSize);
			std::iota(m_FreeList.begin() + size, m_FreeList.end(), size);
		}
		return m_FreeList[m_NumAllocations++];
	}

	void Free(uint32 index)
	{
		std::scoped_lock lock(m_Mutex);
		check(m_NumAllocations > 0);
		--m_NumAllocations;
		m_FreeList[m_NumAllocations] = index;
	}

	uint32 GetNumAllocations() const { return m_NumAllocations; }
	bool CanAllocate() const { return m_NumAllocations < m_FreeList.size(); }

private:
	struct DummyMutex
	{
		void lock() {}
		void unlock() {}
	};
	using TMutex = std::conditional_t<ThreadSafe, std::mutex, DummyMutex>;

	std::vector<uint32> m_FreeList;
	uint32 m_NumAllocations;
	uint32 m_ChunkSize;
	TMutex m_Mutex;
	bool m_CanResize;
};
