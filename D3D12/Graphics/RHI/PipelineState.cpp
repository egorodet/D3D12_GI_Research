#include "stdafx.h"
#include "PipelineState.h"
#include "Shader.h"
#include "Graphics.h"
#include "RootSignature.h"

PipelineStateInitializer::PipelineStateInitializer()
{
	m_pSubobjectData.resize(sizeof(CD3DX12_PIPELINE_STATE_STREAM2));
	m_pSubobjectLocations.fill(-1);

	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>() = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>() = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>() = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>() = DefaultSampleDesc();
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS>() = D3D12_PIPELINE_STATE_FLAG_NONE;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK>() = DefaultSampleMask();
}

void PipelineStateInitializer::SetName(const char* pName)
{
	m_Name = pName;
}

void PipelineStateInitializer::SetDepthOnlyTarget(ResourceFormat dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY& formatArray = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
	formatArray.NumRenderTargets = 0;
	DXGI_SAMPLE_DESC& sampleDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
	sampleDesc.Count = msaa;
	sampleDesc.Quality = 0;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().MultisampleEnable = msaa > 1;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>() = D3D::ConvertFormat(dsvFormat);
}

void PipelineStateInitializer::SetRenderTargetFormats(const Span<ResourceFormat>& rtvFormats, ResourceFormat dsvFormat, uint32 msaa)
{
	D3D12_RT_FORMAT_ARRAY& formatArray = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>();
	// Validation layer bug - Throws error about RT Format even if NumRenderTargets == 0.
	memset(formatArray.RTFormats, 0, sizeof(DXGI_FORMAT) * ARRAYSIZE(formatArray.RTFormats));
	formatArray.NumRenderTargets = 0;
	for (ResourceFormat format : rtvFormats)
	{
		formatArray.RTFormats[formatArray.NumRenderTargets++] = D3D::ConvertFormat(format);
	}
	DXGI_SAMPLE_DESC& sampleDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC>();
	sampleDesc.Count = msaa;
	sampleDesc.Quality = 0;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().MultisampleEnable = msaa > 1;
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>() = D3D::ConvertFormat(dsvFormat);
}

void PipelineStateInitializer::SetBlendMode(const BlendMode& blendMode, bool /*alphaToCoverage*/)
{
	D3D12_BLEND_DESC& blendDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND>();
	D3D12_RENDER_TARGET_BLEND_DESC& desc = blendDesc.RenderTarget[0];
	desc.RenderTargetWriteMask = 0xf;
	desc.BlendEnable = blendMode == BlendMode::Replace ? false : true;

	switch (blendMode)
	{
	case BlendMode::Replace:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_ZERO;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Alpha:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Additive:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Multiply:
		desc.SrcBlend = D3D12_BLEND_DEST_COLOR;
		desc.DestBlend = D3D12_BLEND_ZERO;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_DEST_COLOR;
		desc.DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::AddAlpha:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::PreMultiplyAlpha:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::InverseDestinationAlpha:
		desc.SrcBlend = D3D12_BLEND_INV_DEST_ALPHA;
		desc.DestBlend = D3D12_BLEND_DEST_ALPHA;
		desc.BlendOp = D3D12_BLEND_OP_ADD;
		desc.SrcBlendAlpha = D3D12_BLEND_INV_DEST_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
		desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case BlendMode::Subtract:
		desc.SrcBlend = D3D12_BLEND_ONE;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		desc.SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case BlendMode::SubtractAlpha:
		desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlend = D3D12_BLEND_ONE;
		desc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		desc.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		desc.DestBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendOpAlpha = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case BlendMode::Undefined:
	default:
		break;
	}
}

void PipelineStateInitializer::SetDepthEnabled(bool enabled)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthEnable = enabled;
}

void PipelineStateInitializer::SetDepthWrite(bool enabled)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthWriteMask = enabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
}

void PipelineStateInitializer::SetDepthTest(D3D12_COMPARISON_FUNC func)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>().DepthFunc = func;
}

void PipelineStateInitializer::SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int /*stencilRef*/, unsigned char compareMask, unsigned char writeMask)
{
	D3D12_DEPTH_STENCIL_DESC1& dssDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1>();
	dssDesc.StencilEnable = stencilEnabled;
	dssDesc.FrontFace.StencilFunc = mode;
	dssDesc.FrontFace.StencilPassOp = pass;
	dssDesc.FrontFace.StencilFailOp = fail;
	dssDesc.FrontFace.StencilDepthFailOp = zFail;
	dssDesc.StencilReadMask = compareMask;
	dssDesc.StencilWriteMask = writeMask;
	dssDesc.BackFace = dssDesc.FrontFace;
}

void PipelineStateInitializer::SetFillMode(D3D12_FILL_MODE fillMode)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().FillMode = fillMode;
}

void PipelineStateInitializer::SetCullMode(D3D12_CULL_MODE cullMode)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().CullMode = cullMode;
}

void PipelineStateInitializer::SetLineAntialias(bool lineAntiAlias)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>().AntialiasedLineEnable = lineAntiAlias;
}

void PipelineStateInitializer::SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias)
{
	D3D12_RASTERIZER_DESC& rsDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>();
	rsDesc.SlopeScaledDepthBias = slopeScaledDepthBias;
	rsDesc.DepthBias = depthBias;
	rsDesc.DepthBiasClamp = depthBiasClamp;
}

void PipelineStateInitializer::SetInputLayout(const Span<VertexElementDesc>& layout)
{
	D3D12_INPUT_LAYOUT_DESC& ilDesc = GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();

	m_IlDesc.clear();
	for (const VertexElementDesc& element : layout)
	{
		D3D12_INPUT_ELEMENT_DESC& desc = m_IlDesc.emplace_back();
		desc.AlignedByteOffset = element.ByteOffset;
		desc.Format = D3D::ConvertFormat(element.Format);
		desc.InputSlot = 0;
		desc.InputSlotClass = element.InstanceStepRate > 0 ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		desc.InstanceDataStepRate = element.InstanceStepRate;
		desc.SemanticIndex = 0;
		desc.SemanticName = element.pSemantic;
	}

	ilDesc.NumElements = (uint32)m_IlDesc.size();
	ilDesc.pInputElementDescs = m_IlDesc.data();
}

void PipelineStateInitializer::SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY>() = topology;
}

void PipelineStateInitializer::SetRootSignature(RootSignature* pRootSignature)
{
	GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE>() = pRootSignature->GetRootSignature();
}

void PipelineStateInitializer::SetVertexShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Graphics;
	m_ShaderDescs[(int)ShaderType::Vertex] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetPixelShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_ShaderDescs[(int)ShaderType::Pixel] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetGeometryShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Graphics;
	m_ShaderDescs[(int)ShaderType::Geometry] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetComputeShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Compute;
	m_ShaderDescs[(int)ShaderType::Compute] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetMeshShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Mesh;
	m_ShaderDescs[(int)ShaderType::Mesh] = { pShaderPath, entryPoint, defines.Copy() };
}

void PipelineStateInitializer::SetAmplificationShader(const char* pShaderPath, const char* entryPoint, const Span<ShaderDefine>& defines)
{
	m_Type = PipelineStateType::Mesh;
	m_ShaderDescs[(int)ShaderType::Amplification] = { pShaderPath, entryPoint, defines.Copy() };
}

D3D12_PIPELINE_STATE_STREAM_DESC PipelineStateInitializer::GetDesc(GraphicsDevice* pDevice)
{
	auto GetByteCode = [this](ShaderType type) -> D3D12_SHADER_BYTECODE& {
		switch (type)
		{
		case ShaderType::Vertex: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS>();
		case ShaderType::Pixel: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS>();
		case ShaderType::Geometry: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS>();
		case ShaderType::Mesh: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS>();
		case ShaderType::Amplification: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS>();
		case ShaderType::Compute: return GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS>();
		case ShaderType::MAX:
		default:
			noEntry();
			static D3D12_SHADER_BYTECODE dummy;
			return dummy;
		}
	};

	for (uint32 i = 0; i < (int)ShaderType::MAX; ++i)
	{
		Shader* pShader = nullptr;
		const ShaderDesc& desc = m_ShaderDescs[i];
		if (desc.Path.length() > 0)
		{
			pShader = pDevice->GetShaderManager()->GetShader(desc.Path.c_str(), (ShaderType)i, desc.EntryPoint.c_str(), desc.Defines);
			check(pShader);
			if (pShader)
			{
				GetByteCode((ShaderType)i) = pShader->GetByteCode();
				if (m_Name.empty())
				{
					m_Name = Sprintf("%s (Unnamed)", pShader->EntryPoint.c_str());
				}
			}

			m_Shaders[i] = pShader;
		}
	}

	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
	streamDesc.pPipelineStateSubobjectStream = m_pSubobjectData.data();
	streamDesc.SizeInBytes = m_Size;

	return streamDesc;
}

PipelineState::PipelineState(GraphicsDevice* pParent)
	: GraphicsObject(pParent)
{
	m_ReloadHandle = pParent->GetShaderManager()->OnShaderRecompiledEvent().AddRaw(this, &PipelineState::OnShaderReloaded);
}

PipelineState::~PipelineState()
{
	GetParent()->GetShaderManager()->OnShaderRecompiledEvent().Remove(m_ReloadHandle);
}

void PipelineState::Create(const PipelineStateInitializer& initializer)
{
	GetParent()->DeferReleaseObject(m_pPipelineState.Detach());

	check(initializer.m_Type != PipelineStateType::MAX);
	RefCountPtr<ID3D12Device2> pDevice2;
	VERIFY_HR_EX(GetParent()->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice2.GetAddressOf())), GetParent()->GetDevice());

	m_Desc = initializer;
	if (m_Desc.m_IlDesc.size() > 0)
	{
		D3D12_INPUT_LAYOUT_DESC& ilDesc = m_Desc.GetSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT>();
		ilDesc.pInputElementDescs = m_Desc.m_IlDesc.data();
	}

	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = m_Desc.GetDesc(GetParent());
	VERIFY_HR_EX(pDevice2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(m_pPipelineState.ReleaseAndGetAddressOf())), GetParent()->GetDevice());
	D3D::SetObjectName(m_pPipelineState.Get(), m_Desc.m_Name.c_str());
}

void PipelineState::ConditionallyReload()
{
	if (m_NeedsReload)
	{
		Create(m_Desc);
		m_NeedsReload = false;
		E_LOG(Info, "Reloaded Pipeline: %s", m_Desc.m_Name.c_str());
	}
}

void PipelineState::OnShaderReloaded(Shader* pOldShader, Shader* pNewShader)
{
	for (Shader*& pShader : m_Desc.m_Shaders)
	{
		if (pShader && pShader == pOldShader)
		{
			pShader = pNewShader;
			m_NeedsReload = true;
		}
	}
}
