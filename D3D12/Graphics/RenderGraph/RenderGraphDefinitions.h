#pragma once

struct RGHandleT
{
	explicit RGHandleT(uint32 id = InvalidIndex)
		: Index(id)
	{}

	bool operator==(const RGHandleT& other) const { return Index == other.Index; }
	bool operator!=(const RGHandleT& other) const { return Index != other.Index; }

	constexpr static const uint32 InvalidIndex = 0xFFFFFFFF;
	inline bool IsValid() const { return Index != InvalidIndex; }

	int Index;
};

template<typename T>
struct RGHandle : RGHandleT
{
	explicit RGHandle(uint32 id = InvalidIndex)
		: RGHandleT(id)
	{}
	using Type = T;
};
