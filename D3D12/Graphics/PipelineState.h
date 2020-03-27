#pragma once

enum class BlendMode
{
	Replace = 0,
	And,
	Multiply,
	Alpha,
	AddAlpha,
	PreMultiplyAlpha,
	InverseDestinationAlpha,
	Subtract,
	SubtractAlpha,
	Undefined,
};

class PipelineState
{
public:
	PipelineState();
	PipelineState(const PipelineState& other);
	ID3D12PipelineState* GetPipelineState() const { return m_pPipelineState.Get(); }
	void Finalize(const char* pName, ID3D12Device* pDevice);

	void SetRenderTargetFormat(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, uint32 msaa, uint32 msaaQuality);
	void SetRenderTargetFormats(DXGI_FORMAT* rtvFormats, uint32 count, DXGI_FORMAT dsvFormat, uint32 msaa, uint32 msaaQuality);

	//BlendState
	void SetBlendMode(const BlendMode& blendMode, bool alphaToCoverage);

	//DepthStencilState
	void SetDepthEnabled(bool enabled);
	void SetDepthWrite(bool enabled);
	void SetDepthTest(const D3D12_COMPARISON_FUNC func);
	void SetStencilTest(bool stencilEnabled, D3D12_COMPARISON_FUNC mode, D3D12_STENCIL_OP pass, D3D12_STENCIL_OP fail, D3D12_STENCIL_OP zFail, unsigned int stencilRef, unsigned char compareMask, unsigned char writeMask);

	//RasterizerState
	void SetFillMode(D3D12_FILL_MODE fillMode);
	void SetCullMode(D3D12_CULL_MODE cullMode);
	void SetLineAntialias(bool lineAntiAlias);
	void SetDepthBias(int depthBias, float depthBiasClamp, float slopeScaledDepthBias);

	void SetInputLayout(D3D12_INPUT_ELEMENT_DESC* pElements, uint32 count);
	void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology);

	void SetRootSignature(ID3D12RootSignature* pRootSignature);

	//Shaders
	void SetVertexShader(const void* pByteCode, uint32 byteCodeLength);
	void SetPixelShader(const void* pByteCode, uint32 byteCodeLength);
	void SetGeometryShader(const void* pByteCode, uint32 byteCodeLength);
	void SetComputeShader(const void* pByteCode, uint32 byteCodeLength);

protected:
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	CD3DX12_PIPELINE_STATE_STREAM1 m_Desc = {};
};