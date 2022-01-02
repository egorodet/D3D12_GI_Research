#pragma once

class GraphicsDevice;
class ResourceView;

class GraphicsObject
{
public:
	GraphicsObject(GraphicsDevice* pParent = nullptr)
		: m_pParent(pParent)
	{}

	inline GraphicsDevice* GetParent() const { return m_pParent; }

private:
	GraphicsDevice* m_pParent;
};

constexpr D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_UNKNOWN = (D3D12_RESOURCE_STATES)-1;

class ResourceState
{
public:
	ResourceState(D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNKNOWN)
		: m_CommonState(initialState), m_AllSameState(true)
	{}
	void Set(D3D12_RESOURCE_STATES state, uint32 subResource)
	{
		if (subResource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
		{
			check(subResource < m_ResourceStates.size());
			if (m_AllSameState)
			{
				for (D3D12_RESOURCE_STATES& s : m_ResourceStates)
				{
					s = m_CommonState;
				}
				m_AllSameState = false;
			}
			m_ResourceStates[subResource] = state;

			if (!m_AllSameState)
			{
				bool sameState = true;
				for (D3D12_RESOURCE_STATES& s : m_ResourceStates)
				{
					if (s != state)
					{
						sameState = false;
						break;
					}
				}
				if (sameState)
				{
					m_AllSameState = true;
				}
			}
		}
		else
		{
			m_AllSameState = true;
			m_CommonState = state;
		}
	}
	D3D12_RESOURCE_STATES Get(uint32 subResource) const
	{
		assert(m_AllSameState || subResource < (uint32)m_ResourceStates.size());
		return m_AllSameState ? m_CommonState : m_ResourceStates[subResource];
	}
private:
	constexpr static uint32 MAX_SUBRESOURCES = 12;
	std::array<D3D12_RESOURCE_STATES, MAX_SUBRESOURCES> m_ResourceStates{};
	D3D12_RESOURCE_STATES m_CommonState;
	bool m_AllSameState;
};

class GraphicsResource : public GraphicsObject
{
	friend class CommandContext;

public:
	GraphicsResource(GraphicsDevice* pParent);
	GraphicsResource(GraphicsDevice* pParent, ID3D12Resource* pResource, D3D12_RESOURCE_STATES state);
	virtual ~GraphicsResource();

	void* Map(uint32 subResource = 0, uint64 readFrom = 0, uint64 readTo = 0);
	void Unmap(uint32 subResource = 0, uint64 writtenFrom = 0, uint64 writtenTo = 0);
	void* GetMappedData() const { return m_pMappedData; }
	void SetImmediateDelete(bool immediate) { m_ImmediateDelete = immediate; }

	void Release();
	void SetName(const char* pName);
	const std::string& GetName() { return m_Name; }

	inline ID3D12Resource* GetResource() const { return m_pResource; }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGpuHandle() const { return m_pResource->GetGPUVirtualAddress(); }

	void SetResourceState(D3D12_RESOURCE_STATES state, uint32 subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { m_ResourceState.Set(state, subResource); }
	inline D3D12_RESOURCE_STATES GetResourceState(uint32 subResource = 0) const { return m_ResourceState.Get(subResource); }

protected:
	std::string m_Name;
	bool m_ImmediateDelete = false;
	ID3D12Resource* m_pResource = nullptr;
	void* m_pMappedData = nullptr;
	std::vector<std::unique_ptr<ResourceView>> m_Descriptors;
	ResourceState m_ResourceState;
};
