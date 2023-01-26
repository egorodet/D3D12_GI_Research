#include "stdafx.h"
#include "VisualizeTexture.h"
#include "../RenderGraph/RenderGraph.h"
#include "../RHI/Graphics.h"
#include "../RHI/RHI.h"
#include "../RHI/PipelineState.h"
#include "../RHI/RootSignature.h"
#include "../ImGuiRenderer.h"
#include "../SceneView.h"
#include "Content/Image.h"
#include "imgui_internal.h"
#include "IconsFontAwesome4.h"

VisualizeTexture::VisualizeTexture(GraphicsDevice* pDevice)
{
	m_pVisualizeRS = new RootSignature(pDevice);
	m_pVisualizeRS->AddConstantBufferView(0);
	m_pVisualizeRS->Finalize("Common");
	m_pVisualizePSO = pDevice->CreateComputePipeline(m_pVisualizeRS, "ImageVisualize.hlsl", "CSMain");

	CommandContext& context = *pDevice->AllocateCommandContext();
	constexpr uint32 checkerPixels[] =
	{
		0xFFFFFFFF, 0xFF000000,
		0xFF000000, 0xFFFFFFFF
	};
	m_pCheckerPattern = GraphicsCommon::CreateTextureFromImage(context, Image(2, 2, 1, ResourceFormat::RGBA8_UNORM, 1, checkerPixels), false, "Checker Pattern");
	context.Execute(false);
}

void VisualizeTexture::Capture(RGGraph& graph, RGTexture* pTexture)
{
	if (!pTexture)
	{
		pVisualizeTexture = nullptr;
		return;
	}
	if (SourceName != pTexture->GetName())
	{
		Scale = 1.0f;
	}

	const TextureDesc& desc = pTexture->GetDesc();
	RGTexture* pTarget = graph.Create("Visualize Target", TextureDesc::Create2D(desc.Width, desc.Height, ResourceFormat::RGBA8_UNORM));
	SourceName = pTexture->GetName();
	SourceDesc = pTexture->GetDesc();

	graph.AddPass("Process Image Visualizer", RGPassFlag::Compute | RGPassFlag::NeverCull)
		.Read(pTexture)
		.Write(pTarget)
		.Bind([=](CommandContext& cmdContext)
			{
				cmdContext.SetComputeRootSignature(m_pVisualizeRS);
				cmdContext.SetPipelineState(m_pVisualizePSO);

				struct ConstantsData
				{
					Vector2 InvDimensions;
					Vector2 ValueRange;
					uint32 TextureSource;
					uint32 TextureTarget;
					TextureDimension TextureType;
					uint32 ChannelMask;
					float MipLevel;
					float Slice;
				} constants;

				const TextureDesc& desc = pTexture->GetDesc();
				const FormatInfo& formatInfo = RHI::GetFormatInfo(desc.Format);

				constants.TextureSource = pTexture->Get()->GetSRV()->GetHeapIndex();
				constants.TextureTarget = pTarget->Get()->GetUAV()->GetHeapIndex();
				constants.InvDimensions.x = 1.0f / desc.Width;
				constants.InvDimensions.y = 1.0f / desc.Height;
				constants.TextureType = pTexture->GetDesc().Dimensions;
				constants.ValueRange = Vector2(RangeMin, RangeMax);
				constants.ChannelMask =
					(VisibleChannels[0] ? 1 : 0) << 0 |
					(VisibleChannels[1] ? 1 : 0) << 1 |
					(VisibleChannels[2] ? 1 : 0) << 2 |
					(VisibleChannels[3] ? 1 : 0) << 3;
				constants.ChannelMask &= ((1u << formatInfo.NumComponents) - 1u);
				constants.MipLevel = (float)MipLevel;
				constants.Slice = Slice / desc.DepthOrArraySize;
				if (pTexture->GetDesc().Dimensions == TextureDimension::TextureCube)
					constants.Slice = (float)CubeFaceIndex;

				cmdContext.SetRootCBV(0, constants);
				cmdContext.Dispatch(ComputeUtils::GetNumThreadGroups(desc.Width, 8, desc.Height, 8));
			});
	
	graph.Export(pTarget, &pVisualizeTexture);
}

void VisualizeTexture::RenderUI(const ImVec2& viewportOrigin, const ImVec2& viewportSize)
{
	if (pVisualizeTexture)
	{
		struct Group
		{
			Group()
			{
				ImGui::BeginGroup();
				ImGui::Dummy(ImVec2(1, 3));
				ImGui::Dummy(ImVec2(0, 2));
				ImGui::SameLine();
			}

			~Group()
			{
				ImGui::SameLine();
				ImGui::Dummy(ImVec2(0, 0));
				ImGui::Dummy(ImVec2(1, 3));
				ImGui::EndGroup();
				ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0.3f, 0.3f, 0.3f, 1.0f), 2.5f);
			}
		};

		if (ImGui::Begin("Visualize Texture", 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::PushID("VisualizeTexture");
			TextureDesc& desc = SourceDesc;
			const FormatInfo& formatInfo = RHI::GetFormatInfo(desc.Format);
			Vector2u mipSize(desc.Width >> MipLevel, desc.Height >> MipLevel);

			{
				Group group;

				// Channel visibility switches
				{
					auto ChannelButton = [](const char* pName, bool* pValue, bool enabled, const ImVec2& size)
					{
						ImGui::BeginDisabled(!enabled);
						ImGui::ToggleButton(pName, pValue, size);
						ImGui::EndDisabled();
					};

					ImVec2 buttonSize = ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing());

					ChannelButton("R", &VisibleChannels[0], formatInfo.NumComponents >= 1, buttonSize);
					ImGui::SameLine();
					ChannelButton("G", &VisibleChannels[1], formatInfo.NumComponents >= 2, buttonSize);
					ImGui::SameLine();
					ChannelButton("B", &VisibleChannels[2], formatInfo.NumComponents >= 3, buttonSize);
					ImGui::SameLine();
					ChannelButton("A", &VisibleChannels[3], formatInfo.NumComponents >= 4, buttonSize);
				}

				ImGui::SameLine();

				// X-Ray mode
				{
					ImVec2 buttonSize = ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing());
					ImGui::ToggleButton(ICON_FA_SEARCH_PLUS, &XRay, buttonSize);
				}
			}

			ImGui::SameLine();

			// Mip Selection
			{
				Group group;

				ImGui::BeginDisabled(desc.Mips <= 1);
				std::vector<std::string> mipTexts(desc.Mips);
				for (uint32 i = 0; i < desc.Mips; ++i)
				{
					mipTexts[i] = Sprintf("%d - %dx%d", i, Math::Max(1u, desc.Width >> i), Math::Max(1u, desc.Height >> i));
				}
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Mip");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(170);
				ImGui::Combo("##Mip", &MipLevel, [](void* pData, int idx, const char** pOutText)
					{
						std::string* pStrings = (std::string*)pData;
						*pOutText = pStrings[idx].c_str();
						return true;
					}, mipTexts.data(), (int)mipTexts.size());
				ImGui::EndDisabled();
			}

			ImGui::SameLine();

			// Slice/Face Selection
			{
				Group group;

				ImGui::BeginDisabled(desc.Dimensions != TextureDimension::TextureCube && desc.Dimensions != TextureDimension::TextureCubeArray);
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Slice/Face");
				ImGui::SameLine();
				constexpr const char* faceNames[] = { "Right", "Left", "Top", "Bottom", "Back", "Front" };
				ImGui::SetNextItemWidth(100);
				ImGui::Combo("##SliceFace", &CubeFaceIndex, faceNames, ARRAYSIZE(faceNames));
				ImGui::EndDisabled();
			}

			{
				Group group;

				ImGui::AlignTextToFramePadding();
				ImGui::Text("Zoom");
				ImGui::SameLine();
				if (ImGui::Button("1:1"))
				{
					Scale = 1.0f;
				}
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_ARROWS_ALT " Fit"))
				{
					ImVec2 ratio = ImGui::GetWindowSize() / ImVec2((float)mipSize.x, (float)mipSize.y);
					Scale = ImMin(ratio.x, ratio.y);
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(60);
				float scalePercent = Scale * 100.0f;
				if (ImGui::DragFloat("##Scale", &scalePercent, 4.0f, 1.0f, 50000.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic))
				{
					Scale = scalePercent / 100.0f;
				}
			}

			ImGui::SameLine();

			// Value Range Slider
			{
				Group group;

				float minValue = 0.0f;
				float maxValue = 1.0f;
				float stepSize = 0.01f;
				float* pMinRange = &RangeMin;
				float* pMaxRange = &RangeMax;

				constexpr float triangleSize = 5;

				ImGuiWindow* window = ImGui::GetCurrentWindow();
				ImGuiContext& g = *GImGui;
				const ImGuiStyle& style = g.Style;

				ImGui::AlignTextToFramePadding();
				ImGui::Text("Range");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				ImGui::DragFloat("##RangeMin", pMinRange, stepSize, minValue, *pMaxRange);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(200);
				ImGuiID id = ImGui::GetID("##RangeSlider");
				const float w = ImGui::CalcItemWidth();
				const ImVec2 label_size = ImGui::CalcTextSize("", nullptr, true);
				const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
				const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));
				ImGui::ItemSize(total_bb);
				ImGui::ItemAdd(frame_bb, id);

				ImGui::RenderNavHighlight(frame_bb, id);
				ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImGuiCol_FrameBgActive, true, g.Style.FrameRounding);

				ImRect item_bb = ImRect(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
				float minRangePosX = Math::RemapRange(*pMinRange, minValue, maxValue, item_bb.Min.x, item_bb.Max.x);
				float maxRangePosX = Math::RemapRange(*pMaxRange, minValue, maxValue, item_bb.Min.x, item_bb.Max.x);

				{
					ImGuiID minRangeHandleID = ImGui::GetID("##SliderMin");
					ImRect minRangeHandleBB(ImVec2(minRangePosX - triangleSize, item_bb.Min.y), ImVec2(minRangePosX + triangleSize, item_bb.Min.y + triangleSize * 2));
					ImGui::ItemAdd(minRangeHandleBB, minRangeHandleID);
					const bool hovered = ImGui::ItemHoverable(minRangeHandleBB, minRangeHandleID);
					const bool clicked = hovered && ImGui::IsMouseClicked(0, minRangeHandleID);
					if (clicked || g.NavActivateId == minRangeHandleID || g.NavActivateInputId == minRangeHandleID)
					{
						if (clicked)
							ImGui::SetKeyOwner(ImGuiKey_MouseLeft, minRangeHandleID);
						ImGui::SetActiveID(minRangeHandleID, window);
						ImGui::SetFocusID(minRangeHandleID, window);
						ImGui::FocusWindow(window);
					}
					ImRect grabBB;
					if (ImGui::SliderBehavior(item_bb, minRangeHandleID, ImGuiDataType_Float, pMinRange, &minValue, &maxValue, "", ImGuiSliderFlags_None, &grabBB))
					{
						ImGui::DataTypeClamp(ImGuiDataType_Float, pMinRange, &minValue, pMaxRange);
					}
				}
				{
					ImGuiID maxRangeHandleID = ImGui::GetID("##SliderMax");
					ImRect maxRangeHandleBB(ImVec2(maxRangePosX - triangleSize, item_bb.Max.y - triangleSize * 2), ImVec2(maxRangePosX + triangleSize, item_bb.Max.y));
					ImGui::ItemAdd(maxRangeHandleBB, maxRangeHandleID);
					const bool hovered = ImGui::ItemHoverable(maxRangeHandleBB, maxRangeHandleID);
					const bool clicked = hovered && ImGui::IsMouseClicked(0, maxRangeHandleID);
					if (clicked || g.NavActivateId == maxRangeHandleID || g.NavActivateInputId == maxRangeHandleID)
					{
						if (clicked)
							ImGui::SetKeyOwner(ImGuiKey_MouseLeft, maxRangeHandleID);
						ImGui::SetActiveID(maxRangeHandleID, window);
						ImGui::SetFocusID(maxRangeHandleID, window);
						ImGui::FocusWindow(window);
					}
					ImRect grabBB;
					if (ImGui::SliderBehavior(item_bb, maxRangeHandleID, ImGuiDataType_Float, pMaxRange, &minValue, &maxValue, "", ImGuiSliderFlags_None, &grabBB))
					{
						ImGui::DataTypeClamp(ImGuiDataType_Float, pMaxRange, pMinRange, &maxValue);
					}
				}

				ImDrawList* pDrawList = ImGui::GetWindowDrawList();

				pDrawList->AddRectFilled(item_bb.Min, item_bb.Max, ImColor(0.3f, 0.8f, 1.0f, 1.0f));
				pDrawList->AddRect(item_bb.Min, item_bb.Max, ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddRectFilled(item_bb.Min, ImVec2(minRangePosX, item_bb.Max.y), ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddRect(item_bb.Min, ImVec2(minRangePosX, item_bb.Max.y), ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddRectFilled(ImVec2(maxRangePosX, item_bb.Min.y), item_bb.Max, ImColor(1.0f, 1.0f, 1.0f, 1.0f));
				pDrawList->AddRect(ImVec2(maxRangePosX, item_bb.Min.y), item_bb.Max, ImColor(0.0f, 0.0f, 0.0f, 1.0f));

				const ImVec2 blackTri[] = {
					ImVec2(-1.0f, 0.0f) * triangleSize + ImVec2(minRangePosX, item_bb.Min.y),
					ImVec2(1.0f, 0.0f) * triangleSize + ImVec2(minRangePosX, item_bb.Min.y),
					ImVec2(0.0f, 2.0f) * triangleSize + ImVec2(minRangePosX, item_bb.Min.y),
				};

				pDrawList->AddTriangleFilled(blackTri[0], blackTri[1], blackTri[2], ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddTriangle(blackTri[0], blackTri[1], blackTri[2], ImColor(1.0f, 1.0f, 1.0f, 1.0f));

				const ImVec2 whiteTri[] = {
					ImVec2(1.0f, 0.0f) * triangleSize + ImVec2(maxRangePosX, item_bb.Max.y),
					ImVec2(-1.0f, 0.0f) * triangleSize + ImVec2(maxRangePosX, item_bb.Max.y),
					ImVec2(0.0f, -2.0f) * triangleSize + ImVec2(maxRangePosX, item_bb.Max.y),
				};
				pDrawList->AddTriangleFilled(whiteTri[0], whiteTri[1], whiteTri[2], ImColor(1.0f, 1.0f, 1.0f, 1.0f));
				pDrawList->AddTriangle(whiteTri[0], whiteTri[1], whiteTri[2], ImColor(0.0f, 0.0f, 0.0f, 1.0f));

				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				ImGui::DragFloat("##RangeMax", pMaxRange, stepSize, *pMinRange, maxValue);
			}


			// Image
			{
				ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollWithMouse;
				if (XRay)
				{
					windowFlags |= ImGuiWindowFlags_NoScrollbar;
				}
				else
				{
					windowFlags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
					windowFlags |= ImGuiWindowFlags_AlwaysHorizontalScrollbar;
				}

				ImVec2 avail = ImGui::GetContentRegionAvail();
				ImGui::BeginChild("##ImageView", ImVec2(avail.x, avail.y - ImGui::GetTextLineHeight()), false, windowFlags);

				ImVec2 UV;

				static bool held = false;
				if (XRay)
				{
					ImVec2 maxSize = ImGui::GetContentRegionAvail() - ImVec2(0, ImGui::GetTextLineHeight());
					ImGui::SetCursorScreenPos(ImMax(viewportOrigin, ImGui::GetCursorScreenPos()));
					ImVec2 size = ImClamp(maxSize, ImVec2(0, 0), viewportOrigin + viewportSize - ImGui::GetCursorScreenPos());
					ImVec2 uv0 = (ImGui::GetCursorScreenPos() - viewportOrigin) / viewportSize;
					ImVec2 uv1 = uv0 + size / viewportSize;
					ImGui::Image(pVisualizeTexture, size, uv0, uv1);
					held = false;
					UV = (ImGui::GetMousePos() - viewportOrigin) / viewportSize;
				}
				else
				{
					ImVec2 imageSize = ImVec2((float)mipSize.x, (float)mipSize.y) * Scale;
					ImVec2 checkersSize = ImMax(ImGui::GetContentRegionAvail(), imageSize);
					ImGui::Image(m_pCheckerPattern, checkersSize, ImVec2(0, 0), checkersSize / 50.0f, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
					ImGui::SetCursorPos(ImVec2(0, 0));
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					ImGui::ImageButton("##ImageView", pVisualizeTexture, imageSize);
					UV = (ImGui::GetMousePos() - ImGui::GetItemRectMin()) / ImGui::GetItemRectSize();
					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
					{
						held = true;
					}
				}

				if (held)
				{
					if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
					{
						held = false;
					}
					else
					{
						ImGuiContext& g = *ImGui::GetCurrentContext();
						ImGuiWindow* window = g.CurrentWindow;
						if (held)
						{
							ImGui::SetScrollX(window, window->Scroll.x - ImGui::GetIO().MouseDelta.x);
							ImGui::SetScrollY(window, window->Scroll.y - ImGui::GetIO().MouseDelta.y);
						}
					}
				}

				ImGui::EndChild();

				if (ImGui::IsItemHovered())
				{
					float wheel = ImGui::GetIO().MouseWheel;
					if (wheel != 0)
					{
						float logScale = logf(Scale);
						logScale += wheel / 5.0f;
						Scale = Math::Clamp(expf(logScale), 0.0f, 1000.0f);
					}
				}

				// Texture Description
				{
					UV = ImClamp(UV, ImVec2(0, 0), ImVec2(1, 1));
					Vector2u texel((uint32)Math::Floor(UV.x * mipSize.x), (uint32)Math::Floor(UV.y * mipSize.y));
					ImGui::Text("%s - %dx%d %d mips - %s - %8d, %8d (%.4f, %.4f)", SourceName.c_str(), desc.Width, desc.Height, desc.Mips, formatInfo.pName, texel.x, texel.y, UV.x, 1.0f - UV.y);
				}
			}
			ImGui::PopID();
		}
		ImGui::End();
	}
}
