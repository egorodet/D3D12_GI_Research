#pragma once

#include "RHI.h"

class GraphicsDevice;
class UnorderedAccessView;
class ShaderResourceView;
class CommandContext;

class GraphicsObject
{
public:
	GraphicsObject(GraphicsDevice* pParent)
		: m_pParent(pParent)
	{}
	virtual ~GraphicsObject() = default;

	uint32 AddRef()
	{
		return ++m_RefCount;
	}

	uint32 Release()
	{
		uint32 result = --m_RefCount;
		if (result == 0)
			delete this;
		return result;
	}

	uint32 GetNumRefs() const { return m_RefCount; }
	GraphicsDevice* GetParent() const { return m_pParent; }

private:
	std::atomic<uint32> m_RefCount = 0;
	GraphicsDevice* m_pParent;
};

class ResourceState
{
public:
	ResourceState(ResourceAccess initialState = ResourceAccess::Unknown)
		: m_CommonState(initialState), m_AllSameState(true)
	{}

	void Set(ResourceAccess state, uint32 subResource)
	{
		if (subResource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			check(subResource < m_ResourceStates.size());
			if (m_AllSameState)
			{
				for (ResourceAccess& s : m_ResourceStates)
				{
					s = m_CommonState;
				}
				m_AllSameState = false;
			}
			m_ResourceStates[subResource] = state;
		}
		else
		{
			m_AllSameState = true;
			m_CommonState = state;
		}
	}
	ResourceAccess Get(uint32 subResource) const
	{
		if (m_AllSameState)
		{
			return m_CommonState;
		}
		else
		{
			assert(subResource < (uint32)m_ResourceStates.size());
			return m_ResourceStates[subResource];
		}
	}

	static bool HasWriteResourceState(ResourceAccess state)
	{
		return EnumHasAnyFlags(state, ResourceAccess::WriteMask);
	};

	static bool CanCombineResourceState(ResourceAccess stateA, ResourceAccess stateB)
	{
		return !HasWriteResourceState(stateA) && !HasWriteResourceState(stateB);
	}

private:
	std::array<ResourceAccess, D3D12_REQ_MIP_LEVELS> m_ResourceStates{};
	ResourceAccess m_CommonState;
	bool m_AllSameState;
};

class GraphicsResource : public GraphicsObject
{
	friend class GraphicsDevice;

public:
	GraphicsResource(GraphicsDevice* pParent, ID3D12Resource* pResource);
	~GraphicsResource();

	void* GetMappedData() const { check(m_pMappedData); return m_pMappedData; }
	void SetImmediateDelete(bool immediate) { m_ImmediateDelete = immediate; }

	void SetName(const char* pName);
	const std::string& GetName() const { return m_Name; }

	UnorderedAccessView* GetUAV() const { return m_pUav; }
	ShaderResourceView* GetSRV() const { return m_pSrv; }

	int32 GetSRVIndex() const;
	int32 GetUAVIndex() const;

	inline ID3D12Resource* GetResource() const { return m_pResource; }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	void SetResourceState(ResourceAccess state, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { m_ResourceState.Set(state, subResource); }
	inline ResourceAccess GetResourceState(uint32 subResource = 0) const { return m_ResourceState.Get(subResource); }

protected:
	std::string m_Name;
	bool m_ImmediateDelete = false;
	ID3D12Resource* m_pResource = nullptr;
	void* m_pMappedData = nullptr;
	ResourceState m_ResourceState;
	RefCountPtr<ShaderResourceView> m_pSrv;
	RefCountPtr<UnorderedAccessView> m_pUav;
};
