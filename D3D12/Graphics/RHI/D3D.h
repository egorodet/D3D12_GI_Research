#pragma once

#include "pix3.h"
#include "RHI.h"
#include "Core/Paths.h"
#include "Core/Utils.h"

#define VERIFY_HR(hr) D3D::LogHRESULT(hr, nullptr, #hr, __FILE__, __LINE__)
#define VERIFY_HR_EX(hr, device) D3D::LogHRESULT(hr, device, #hr, __FILE__, __LINE__)

namespace D3D
{
	constexpr const char* CommandlistTypeToString(D3D12_COMMAND_LIST_TYPE type)
	{
#define STATE_CASE(name) case D3D12_COMMAND_LIST_TYPE_##name: return #name
		switch (type)
		{
			STATE_CASE(DIRECT);
			STATE_CASE(COMPUTE);
			STATE_CASE(COPY);
			STATE_CASE(BUNDLE);
			STATE_CASE(VIDEO_DECODE);
			STATE_CASE(VIDEO_ENCODE);
			STATE_CASE(VIDEO_PROCESS);
			default: return "";
		}
#undef STATE_CASE
	}

	inline void EnqueuePIXCapture(uint32 numFrames = 1)
	{
		HWND window = GetActiveWindow();
		if (SUCCEEDED(PIXSetTargetWindow(window)))
		{
			SYSTEMTIME time;
			GetSystemTime(&time);
			Paths::CreateDirectoryTree(Paths::SavedDir());
			std::string filePath = Sprintf("%ssGPU_Capture_%s.wpix", Paths::SavedDir().c_str(), Utils::GetTimeString().c_str());
			if (SUCCEEDED(PIXGpuCaptureNextFrames(MULTIBYTE_TO_UNICODE(filePath.c_str()), numFrames)))
			{
				E_LOG(Info, "Captured %d frames to '%s'", numFrames, filePath.c_str());
			}
		}
	}

	inline std::string GetErrorString(HRESULT errorCode, ID3D12Device* pDevice)
	{
		std::string str;
		char* errorMsg;
		if (FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errorMsg, 0, nullptr) != 0)
		{
			str += errorMsg;
			LocalFree(errorMsg);
		}
		if (errorCode == DXGI_ERROR_DEVICE_REMOVED && pDevice)
		{
			RefCountPtr<ID3D12InfoQueue> pInfo;
			pDevice->QueryInterface(pInfo.GetAddressOf());
			if (pInfo)
			{
				str += "Validation Layer: \n";
				for (uint64 i = 0; i < pInfo->GetNumStoredMessages(); ++i)
				{
					size_t messageLength = 0;
					pInfo->GetMessageA(0, nullptr, &messageLength);
					D3D12_MESSAGE* pMessage = (D3D12_MESSAGE*)malloc(messageLength);
					pInfo->GetMessageA(0, pMessage, &messageLength);
					str += pMessage->pDescription;
					str += "\n";
					free(pMessage);
				}
			}

			HRESULT removedReason = pDevice->GetDeviceRemovedReason();
			str += "\nDRED: " + GetErrorString(removedReason, nullptr);
		}
		return str;
	}

	inline bool LogHRESULT(HRESULT hr, ID3D12Device* pDevice, const char* pCode, const char* pFileName, uint32 lineNumber)
	{
		if (!SUCCEEDED(hr))
		{
			E_LOG(Error, "%s:%d: %s - %s", pFileName, lineNumber, GetErrorString(hr, pDevice).c_str(), pCode);
			__debugbreak();
			return false;
		}
		return true;
	}

	inline void SetObjectName(ID3D12Object* pObject, const char* pName)
	{
		if (pObject && pName)
		{
			VERIFY_HR_EX(pObject->SetPrivateData(WKPDID_D3DDebugObjectName, (uint32)strlen(pName), pName), nullptr);
		}
	}

	inline std::string GetObjectName(ID3D12Object* pObject)
	{
		std::string out;
		if (pObject)
		{
			uint32 size = 0;
			if (SUCCEEDED(pObject->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr)))
			{
				out.resize(size);
				VERIFY_HR(pObject->GetPrivateData(WKPDID_D3DDebugObjectName, &size, &out[0]));
			}
		}
		return out;
	}

	constexpr static const DXGI_FORMAT gDXGIFormatMap[] =
	{
		DXGI_FORMAT_UNKNOWN,
		DXGI_FORMAT_R8_UINT,
		DXGI_FORMAT_R8_SINT,
		DXGI_FORMAT_R8_UNORM,
		DXGI_FORMAT_R8_SNORM,
		DXGI_FORMAT_R8G8_UINT,
		DXGI_FORMAT_R8G8_SINT,
		DXGI_FORMAT_R8G8_UNORM,
		DXGI_FORMAT_R8G8_SNORM,
		DXGI_FORMAT_R16_UINT,
		DXGI_FORMAT_R16_SINT,
		DXGI_FORMAT_R16_UNORM,
		DXGI_FORMAT_R16_SNORM,
		DXGI_FORMAT_R16_FLOAT,
		DXGI_FORMAT_B4G4R4A4_UNORM,
		DXGI_FORMAT_B5G6R5_UNORM,
		DXGI_FORMAT_B5G5R5A1_UNORM,
		DXGI_FORMAT_R8G8B8A8_UINT,
		DXGI_FORMAT_R8G8B8A8_SINT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_R8G8B8A8_SNORM,
		DXGI_FORMAT_B8G8R8A8_UNORM,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
		DXGI_FORMAT_R10G10B10A2_UNORM,
		DXGI_FORMAT_R11G11B10_FLOAT,
		DXGI_FORMAT_R16G16_UINT,
		DXGI_FORMAT_R16G16_SINT,
		DXGI_FORMAT_R16G16_UNORM,
		DXGI_FORMAT_R16G16_SNORM,
		DXGI_FORMAT_R16G16_FLOAT,
		DXGI_FORMAT_R32_UINT,
		DXGI_FORMAT_R32_SINT,
		DXGI_FORMAT_R32_FLOAT,
		DXGI_FORMAT_R16G16B16A16_UINT,
		DXGI_FORMAT_R16G16B16A16_SINT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_UNORM,
		DXGI_FORMAT_R16G16B16A16_SNORM,
		DXGI_FORMAT_R32G32_UINT,
		DXGI_FORMAT_R32G32_SINT,
		DXGI_FORMAT_R32G32_FLOAT,
		DXGI_FORMAT_R32G32B32_UINT,
		DXGI_FORMAT_R32G32B32_SINT,
		DXGI_FORMAT_R32G32B32_FLOAT,
		DXGI_FORMAT_R32G32B32A32_UINT,
		DXGI_FORMAT_R32G32B32A32_SINT,
		DXGI_FORMAT_R32G32B32A32_FLOAT,

		DXGI_FORMAT_BC1_UNORM,
		DXGI_FORMAT_BC1_UNORM_SRGB,
		DXGI_FORMAT_BC2_UNORM,
		DXGI_FORMAT_BC2_UNORM_SRGB,
		DXGI_FORMAT_BC3_UNORM,
		DXGI_FORMAT_BC3_UNORM_SRGB,
		DXGI_FORMAT_BC4_UNORM,
		DXGI_FORMAT_BC4_SNORM,
		DXGI_FORMAT_BC5_UNORM,
		DXGI_FORMAT_BC5_SNORM,
		DXGI_FORMAT_BC6H_UF16,
		DXGI_FORMAT_BC6H_SF16,
		DXGI_FORMAT_BC7_UNORM,
		DXGI_FORMAT_BC7_UNORM_SRGB,

		DXGI_FORMAT_D16_UNORM,
		DXGI_FORMAT_D32_FLOAT,
		DXGI_FORMAT_D24_UNORM_S8_UINT,
		DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
	};

	static_assert(ARRAYSIZE(gDXGIFormatMap) == (uint32)ResourceFormat::Num);

	constexpr DXGI_FORMAT ConvertFormat(ResourceFormat format)
	{
		return gDXGIFormatMap[(uint32)format];
	}

	inline void ResolveAccess(ResourceAccess inAccess, D3D12_BARRIER_ACCESS& outAccess, D3D12_BARRIER_SYNC& outSync, D3D12_BARRIER_LAYOUT& outLayout)
	{
		outAccess = D3D12_BARRIER_ACCESS_COMMON;
		outSync = D3D12_BARRIER_SYNC_NONE;
		outLayout = D3D12_BARRIER_LAYOUT_COMMON;

		auto EnumHasAnyFlagsAndClear = [&](ResourceAccess& flags, ResourceAccess contains) {
			bool checked = EnumHasAnyFlags(flags, contains);
			flags &= ~contains;
			return checked;
		};

		// Don't know? Stall everything!
		if (inAccess == ResourceAccess::Unknown)
		{
			outAccess = D3D12_BARRIER_ACCESS_COMMON;
			outSync = D3D12_BARRIER_SYNC_ALL;
			outLayout = D3D12_BARRIER_LAYOUT_UNDEFINED;
			return;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::Present))
		{
			checkf(inAccess == ResourceAccess::Unknown, "Present state is not allowed to be combined.");
			outAccess = D3D12_BARRIER_ACCESS_COMMON;
			outSync = D3D12_BARRIER_SYNC_ALL;
			outLayout = D3D12_BARRIER_LAYOUT_PRESENT;
			return;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::RTV))
		{
			checkf(inAccess == ResourceAccess::Unknown, "RTV state is not allowed to be combined.");
			outAccess = D3D12_BARRIER_ACCESS_RENDER_TARGET;
			outSync = D3D12_BARRIER_SYNC_RENDER_TARGET;
			outLayout = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			return;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::ResolveDest))
		{
			checkf(inAccess == ResourceAccess::Unknown, "ResolveDest state is not allowed to be combined.");
			outAccess |= D3D12_BARRIER_ACCESS_RESOLVE_DEST;
			outSync |= D3D12_BARRIER_SYNC_RESOLVE;
			outLayout = D3D12_BARRIER_LAYOUT_RESOLVE_DEST;
			return;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::CopyDest))
		{
			checkf(inAccess == ResourceAccess::Unknown, "CopyDest state is not allowed to be combined.");
			outAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
			outSync |= D3D12_BARRIER_SYNC_COPY;
			outLayout = D3D12_BARRIER_LAYOUT_COPY_DEST;
			return;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::AccelerationStructureWrite))
		{
			checkf(inAccess == ResourceAccess::Unknown, "AccelerationStructureWrite state is not allowed to be combined.");
			outAccess |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
			outSync |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
			return;
		}

		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::DSVWrite))
		{
			outAccess = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
			outSync = D3D12_BARRIER_SYNC_DEPTH_STENCIL;
			outLayout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
			return;
		}

		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::VertexBuffer))
		{
			outAccess |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
			outSync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::IndexBuffer))
		{
			outAccess |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
			outSync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::ConstantBuffer))
		{
			outAccess |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
			outSync |= D3D12_BARRIER_SYNC_DRAW;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::SRVGraphics))
		{
			outAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
			outSync |= D3D12_BARRIER_SYNC_ALL_SHADING;
			outLayout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::SRVCompute))
		{
			outAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
			outSync |= D3D12_BARRIER_SYNC_NON_PIXEL_SHADING;
			outLayout = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::CopySrc))
		{
			outAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
			outSync |= D3D12_BARRIER_SYNC_COPY;
			outLayout = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::ResolveSrc))
		{
			outAccess |= D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
			outSync |= D3D12_BARRIER_SYNC_RESOLVE;
			outLayout = D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::DSVRead))
		{
			outAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
			outSync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
			outLayout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::IndirectArgs))
		{
			outAccess |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
			outSync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::AccelerationStructureRead))
		{
			outAccess |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
			outSync |= D3D12_BARRIER_SYNC_RAYTRACING;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::VRS))
		{
			outAccess |= D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;
			outSync |= D3D12_BARRIER_SYNC_ALL_SHADING;
			outLayout = D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE;
		}

		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::UAV))
		{
			outAccess |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
			outSync |= D3D12_BARRIER_SYNC_ALL_SHADING;
			outLayout = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		}
		if (EnumHasAnyFlagsAndClear(inAccess, ResourceAccess::DSVWrite))
		{
			outAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
			outSync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
			outLayout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		}
		checkf(inAccess == ResourceAccess::Unknown, "Following ResourceAccess flags are not accounted for: %s", RHI::ResourceStateToString(inAccess).c_str());
	}
}
