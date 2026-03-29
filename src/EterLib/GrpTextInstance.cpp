#include "StdAfx.h"
#include "GrpTextInstance.h"
#include "IME.h"
#include "TextTag.h"
#include "EterBase/Utils.h"
#include "EterLocale/Arabic.h"
#include "GrpDeviceDX11.h"
#include "GrpScreen.h"

#include <unordered_map>
#include <set>
#include <utf8.h>

// Forward declaration to avoid header conflicts
extern bool IsRTL();

const float c_fFontFeather = 0.5f;

CDynamicPool<CGraphicTextInstance> CGraphicTextInstance::ms_kPool;

static int gs_mx = 0;
static int gs_my = 0;

static std::wstring gs_hyperlinkText;

// M2-UI-TEXTTAIL-PARITY-26: Startup guard state (file-scope to allow reset)
static DWORD s_dwDX11TextStartupGuardBeginMS = timeGetTime(); // Initialize at first load

static bool __IsDX11TextStartupGuardActive()
{
	// Ignore transient bootstrap/resource misses in first frames after startup.
	// M2-UI-TEXTTAIL-PARITY-26: Extended to 5000ms to cover phase changes (alt-tab, resize, fullscreen).
	const DWORD dwNow = timeGetTime();
	return (dwNow >= s_dwDX11TextStartupGuardBeginMS) &&
		((dwNow - s_dwDX11TextStartupGuardBeginMS) < 5000u);
}

// M2-UI-TEXTTAIL-PARITY-26: Allow external reset of guard timer on phase changes (alt-tab, resize, fullscreen)
void ResetDX11TextStartupGuard()
{
	s_dwDX11TextStartupGuardBeginMS = timeGetTime();
}

static void __LogDX11TextFailOnce(bool& rbLogged, const char* c_szReason)
{
	if (rbLogged)
		return;
	if (__IsDX11TextStartupGuardActive())
		return;
	rbLogged = true;
	TraceError("DX11_TEXT_RENDER_FAIL reason=%s", c_szReason);
}

void CGraphicTextInstance::Hyperlink_UpdateMousePos(int x, int y)
{
	gs_mx = x;
	gs_my = y;
	gs_hyperlinkText = L"";
}

int CGraphicTextInstance::Hyperlink_GetText(char* buf, int len)
{
	if (gs_hyperlinkText.empty())
		return 0;

	int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, gs_hyperlinkText.c_str(), (int)gs_hyperlinkText.length(), buf, len, nullptr, nullptr);

	return (written > 0) ? written : 0;
}

int CGraphicTextInstance::__DrawCharacter(CGraphicFontTexture * pFontTexture, wchar_t text, DWORD dwColor, wchar_t prevChar)
{
	CGraphicFontTexture::TCharacterInfomation* pInsCharInfo = pFontTexture->GetCharacterInfomation(text);

	if (pInsCharInfo)
	{
		// Round kerning to nearest pixel to keep glyphs on the pixel grid.
		// Fractional offsets cause bilinear interpolation blur in D3D9.
		float kern = floorf(pFontTexture->GetKerning(prevChar, text) + 0.5f);

		m_dwColorInfoVector.push_back(dwColor);
		m_pCharInfoVector.push_back(pInsCharInfo);
		m_kernVector.push_back(kern);

		m_textWidth += (int)(pInsCharInfo->advance + kern);
		m_textHeight = std::max((WORD)pInsCharInfo->height, m_textHeight);
		return (int)(pInsCharInfo->advance + kern);
	}

	return 0;
}

void CGraphicTextInstance::__GetTextPos(DWORD index, float* x, float* y)
{
	index = std::min((size_t)index, m_pCharInfoVector.size());

	float sx = 0;
	float sy = 0;
	float fFontMaxHeight = 0;

	for(DWORD i=0; i<index; ++i)
	{
		if (sx+float(m_pCharInfoVector[i]->width) > m_fLimitWidth)
		{
			sx = 0;
			sy += fFontMaxHeight;
		}

		sx += float(m_pCharInfoVector[i]->advance);
		fFontMaxHeight = std::max(float(m_pCharInfoVector[i]->height), fFontMaxHeight);
	}

	*x = sx;
	*y = sy;
}

void CGraphicTextInstance::Update()
{
	if (m_isUpdate)
		return;

	// Get space height first for empty text cursor rendering
	int spaceHeight = 12; // default fallback
	if (!m_roText.IsNull() && !m_roText->IsEmpty())
	{
		CGraphicFontTexture* pFontTexture = m_roText->GetFontTexturePointer();
		if (pFontTexture)
		{
			CGraphicFontTexture::TCharacterInfomation* pSpaceInfo = pFontTexture->GetCharacterInfomation(L' ');
			spaceHeight = pSpaceInfo ? pSpaceInfo->height : 12;
		}
	}

	auto ResetState = [&, spaceHeight]()
		{
			m_pCharInfoVector.clear();
			m_kernVector.clear();
			m_dwColorInfoVector.clear();
			m_hyperlinkVector.clear();
			m_textWidth = 0;
			m_textHeight = spaceHeight; // Use space height instead of 0 for cursor rendering
			m_computedRTL = IsRTL(); // Use global RTL setting
			m_isUpdate = true;
		};

	if (m_roText.IsNull() || m_roText->IsEmpty())
	{
		ResetState();
		return;
	}

	CGraphicFontTexture* pFontTexture = m_roText->GetFontTexturePointer();
	if (!pFontTexture)
	{
		ResetState();
		return;
	}

	m_pCharInfoVector.clear();
	m_kernVector.clear();
	m_dwColorInfoVector.clear();
	m_hyperlinkVector.clear();

	m_textWidth = 0;
	m_textHeight = spaceHeight;

	const char* utf8 = m_stText.c_str();
	const int utf8Len = (int)m_stText.size();
	DWORD dwColor = m_dwTextColor;

	// UTF-8 -> UTF-16 conversion
	std::vector<wchar_t> wTextBuf((size_t)utf8Len + 1u, 0);
	int wTextLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, utf8Len, wTextBuf.data(), (int)wTextBuf.size());

	// If strict UTF-8 conversion fails, try lenient mode (replaces invalid sequences)
	if (wTextLen <= 0)
	{
		// Try lenient conversion (no MB_ERR_INVALID_CHARS flag)
		wTextLen = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8Len, wTextBuf.data(), (int)wTextBuf.size());

		if (wTextLen <= 0)
		{
			ResetState();
			return;
		}
	}

	// Set computed RTL based on global setting
	m_computedRTL = IsRTL();

	// Secret mode: draw '*' instead of actual characters
	if (m_isSecret)
	{
		wchar_t prevCh = 0;
		for (int i = 0; i < wTextLen; ++i)
		{
			__DrawCharacter(pFontTexture, L'*', dwColor, prevCh);
			prevCh = L'*';
		}

		pFontTexture->UpdateTexture();
		m_isUpdate = true;
		return;
	}

	// === RENDERING APPROACH ===
	// Use BuildVisualBidiText_Tagless() and BuildVisualChatMessage() from utf8.h
	// These functions handle Arabic shaping, BiDi reordering, and chat formatting properly

	// Special handling for chat messages
	if (m_isChatMessage && !m_chatName.empty() && !m_chatMessage.empty())
	{
		std::wstring wName = Utf8ToWide(m_chatName);
		std::wstring wMsg = Utf8ToWide(m_chatMessage);

		// Check if message has tags (hyperlinks)
		bool msgHasTags = (std::find(wMsg.begin(), wMsg.end(), L'|') != wMsg.end());

		if (!msgHasTags)
		{
			// No tags: Use BuildVisualChatMessage() for simple BiDi
			std::vector<wchar_t> visual = BuildVisualChatMessage(
				wName.data(), (int)wName.size(),
				wMsg.data(), (int)wMsg.size(),
				m_computedRTL);

			wchar_t prevCh = 0;
			for (size_t i = 0; i < visual.size(); ++i)
			{
				__DrawCharacter(pFontTexture, visual[i], dwColor, prevCh);
				prevCh = visual[i];
			}

			pFontTexture->UpdateTexture();
			m_isUpdate = true;
			return;
		}
		else
		{
			// Has tags (hyperlinks): Rebuild as "Message : Name" or "Name : Message"
			// then use tag-aware rendering below
			if (m_computedRTL)
			{
				// RTL: "Message : Name"
				m_stText = m_chatMessage + " : " + m_chatName;
			}
			else
			{
				// LTR: "Name : Message" (original format)
				m_stText = m_chatName + " : " + m_chatMessage;
			}

			// Re-convert to wide chars for tag-aware processing below
			const char* utf8 = m_stText.c_str();
			const int utf8Len = (int)m_stText.size();
			wTextBuf.clear();
			wTextBuf.resize((size_t)utf8Len + 1u, 0);
			wTextLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, utf8Len, wTextBuf.data(), (int)wTextBuf.size());

			if (wTextLen <= 0)
			{
				wTextLen = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8Len, wTextBuf.data(), (int)wTextBuf.size());
				if (wTextLen <= 0)
				{
					ResetState();
					return;
				}
			}
			// Fall through to tag-aware rendering below
		}
	}

	// Check if text contains tags or RTL
	const bool hasTags = (std::find(wTextBuf.begin(), wTextBuf.begin() + wTextLen, L'|') != (wTextBuf.begin() + wTextLen));
	bool hasRTL = false;
	for (int i = 0; i < wTextLen; ++i)
	{
		if (IsRTLCodepoint(wTextBuf[i]))
		{
			hasRTL = true;
			break;
		}
	}

	// Tag-aware BiDi rendering: Parse tags, apply BiDi per segment, track colors/hyperlinks
	// OPTIMIZED: Use helper lambda to eliminate code duplication (was repeated 5+ times)
	if (hasRTL || hasTags)
	{
		DWORD currentColor = dwColor;
		int hyperlinkStep = 0; // 0=normal, 1=collecting metadata, 2=visible hyperlink
		std::wstring hyperlinkMetadata;

		static std::vector<wchar_t> s_currentSegment;
		s_currentSegment.clear();

		SHyperlink currentHyperlink;
		currentHyperlink.sx = currentHyperlink.ex = 0;

		// In chat RTL, force RTL base direction so prefixes like "[hyperlink]" don't flip the paragraph to LTR.
		const bool forceRTLForBidi = (m_isChatMessage && m_computedRTL);

		// OPTIMIZED: Single helper function for flushing segments (eliminates 5x code duplication)
		auto FlushSegment = [&](DWORD segColor) -> int
		{
			if (s_currentSegment.empty())
				return 0;

			int totalWidth = 0;

			// Apply BiDi transformation using optimized BuildVisualBidiText_Tagless
			std::vector<wchar_t> visual = BuildVisualBidiText_Tagless(
				s_currentSegment.data(), (int)s_currentSegment.size(), forceRTLForBidi);

			wchar_t prevCh = m_pCharInfoVector.empty() ? 0 : 0; // no prev across segments
			for (size_t j = 0; j < visual.size(); ++j)
			{
				int w = __DrawCharacter(pFontTexture, visual[j], segColor, prevCh);
				totalWidth += w;
				prevCh = visual[j];
			}

			s_currentSegment.clear();
			return totalWidth;
		};

		// Prepend glyphs to the already-built draw list (used to place hyperlink before message in RTL chat).
		auto PrependGlyphs = [&](CGraphicFontTexture* pFontTexture,
		                         const std::vector<wchar_t>& chars,
		                         DWORD color,
		                         int& outWidth)
		{
			outWidth = 0;

			static std::vector<CGraphicFontTexture::TCharacterInfomation*> s_newCharInfos;
			static std::vector<DWORD> s_newColors;
			static std::vector<float> s_newKerns;
			s_newCharInfos.clear();
			s_newColors.clear();
			s_newKerns.clear();
			s_newCharInfos.reserve(chars.size());
			s_newColors.reserve(chars.size());
			s_newKerns.reserve(chars.size());

			for (size_t k = 0; k < chars.size(); ++k)
			{
				auto* pInfo = pFontTexture->GetCharacterInfomation(chars[k]);
				if (!pInfo)
					continue;

				s_newCharInfos.push_back(pInfo);
				s_newColors.push_back(color);
				s_newKerns.push_back(0.0f);

				outWidth += pInfo->advance;
				m_textHeight = std::max((WORD)pInfo->height, m_textHeight);
			}

			m_pCharInfoVector.insert(m_pCharInfoVector.begin(), s_newCharInfos.begin(), s_newCharInfos.end());
			m_dwColorInfoVector.insert(m_dwColorInfoVector.begin(), s_newColors.begin(), s_newColors.end());
			m_kernVector.insert(m_kernVector.begin(), s_newKerns.begin(), s_newKerns.end());

			for (auto& link : m_hyperlinkVector)
			{
				link.sx += outWidth;
				link.ex += outWidth;
			}

			m_textWidth += outWidth;
		};

		// Parse text with tags
		for (int i = 0; i < wTextLen;)
		{
			int tagLen = 0;
			std::wstring tagExtra;
			int tagType = GetTextTag(&wTextBuf[i], wTextLen - i, tagLen, tagExtra);

			if (tagType == TEXT_TAG_COLOR)
			{
				// Flush current segment before changing color
				currentHyperlink.ex += FlushSegment(currentColor);
				currentColor = htoi(tagExtra.c_str(), 8);
				i += tagLen;
			}
			else if (tagType == TEXT_TAG_RESTORE_COLOR)
			{
				// Flush segment before restoring color
				currentHyperlink.ex += FlushSegment(currentColor);
				currentColor = dwColor;
				i += tagLen;
			}
			else if (tagType == TEXT_TAG_HYPERLINK_START)
			{
				hyperlinkStep = 1;
				hyperlinkMetadata.clear();
				i += tagLen;
			}
			else if (tagType == TEXT_TAG_HYPERLINK_END)
			{
				if (hyperlinkStep == 1)
				{
					// End of metadata, start visible section
					// Flush any pending non-hyperlink segment first
					currentHyperlink.ex += FlushSegment(currentColor);

					hyperlinkStep = 2;
					currentHyperlink.text = hyperlinkMetadata;
					currentHyperlink.sx = currentHyperlink.ex; // Start hyperlink at current cursor position
				}
				else if (hyperlinkStep == 2)
				{
					// End of visible section - render hyperlink text with proper Arabic handling
					// In RTL chat: we want the hyperlink chunk to appear BEFORE the message, even if logically appended.
					if (!s_currentSegment.empty())
					{
						// OPTIMIZED: Use thread-local buffer for visible rendering
						static std::vector<wchar_t> s_visibleToRender;
						s_visibleToRender.clear();

						// Find bracket positions: [ ... ]
						int openBracket = -1, closeBracket = -1;
						for (size_t idx = 0; idx < s_currentSegment.size(); ++idx)
						{
							if (s_currentSegment[idx] == L'[' && openBracket == -1)
								openBracket = (int)idx;
							else if (s_currentSegment[idx] == L']' && closeBracket == -1)
								closeBracket = (int)idx;
						}

						if (openBracket >= 0 && closeBracket > openBracket)
						{
							// Keep '['
							s_visibleToRender.push_back(L'[');

							// Extract inside content and apply BiDi
							static std::vector<wchar_t> s_content;
							s_content.assign(
								s_currentSegment.begin() + openBracket + 1,
								s_currentSegment.begin() + closeBracket);

							// FIX: Use false to let BiDi auto-detect direction from content
							// This ensures English items like [Sword+9] stay LTR
							// while Arabic items like [Ø¯Ø±Ø¹ ÙÙˆÙ„Ø§Ø°ÙŠ+9] are properly RTL
							std::vector<wchar_t> visual = BuildVisualBidiText_Tagless(
								s_content.data(), (int)s_content.size(), false);

							s_visibleToRender.insert(s_visibleToRender.end(), visual.begin(), visual.end());

							// Keep ']'
							s_visibleToRender.push_back(L']');
						}
						else
						{
							// No brackets: apply BiDi to whole segment
							// FIX: Use false to let BiDi auto-detect direction from content
							std::vector<wchar_t> visual = BuildVisualBidiText_Tagless(
								s_currentSegment.data(), (int)s_currentSegment.size(), false);

							s_visibleToRender.insert(s_visibleToRender.end(), visual.begin(), visual.end());
						}

						// Ensure a space AFTER the hyperlink chunk (so it becomes "[hyperlink] Ø§Ø®ØªØ¨Ø§Ø±...")
						s_visibleToRender.push_back(L' ');

						// Key behavior:
						// In RTL chat, place hyperlink BEFORE the message by prepending glyphs.
						if (m_isChatMessage && m_computedRTL)
						{
							int addedWidth = 0;
							PrependGlyphs(pFontTexture, s_visibleToRender, currentColor, addedWidth);

							// Record the hyperlink range at the beginning (0..addedWidth)
							currentHyperlink.sx = 0;
							currentHyperlink.ex = addedWidth;
							m_hyperlinkVector.push_back(currentHyperlink);
						}
						else
						{
							// LTR or non-chat: keep original "append" behavior
							currentHyperlink.sx = currentHyperlink.ex;
							wchar_t prevCh = 0;
							for (size_t j = 0; j < s_visibleToRender.size(); ++j)
							{
								int w = __DrawCharacter(pFontTexture, s_visibleToRender[j], currentColor, prevCh);
								currentHyperlink.ex += w;
								prevCh = s_visibleToRender[j];
							}
							m_hyperlinkVector.push_back(currentHyperlink);
						}
					}

					hyperlinkStep = 0;
					s_currentSegment.clear();
				}
				i += tagLen;
			}
			else // TEXT_TAG_PLAIN or TEXT_TAG_TAG
			{
				if (hyperlinkStep == 1)
				{
					// Collecting hyperlink metadata (hidden)
					hyperlinkMetadata.push_back(wTextBuf[i]);
				}
				else
				{
					// Add to current segment
					// Will be BiDi-processed for normal text, or rendered directly for hyperlinks
					s_currentSegment.push_back(wTextBuf[i]);
				}
				i += tagLen;
			}
		}

		// Flush any remaining segment using optimized helper
		currentHyperlink.ex += FlushSegment(currentColor);

		pFontTexture->UpdateTexture();
		m_isUpdate = true;
		return;
	}

	// Simple LTR rendering for plain text (no tags, no RTL)
	// Just draw characters in logical order
	{
		wchar_t prevCh = 0;
		for (int i = 0; i < wTextLen; ++i)
		{
			__DrawCharacter(pFontTexture, wTextBuf[i], dwColor, prevCh);
			prevCh = wTextBuf[i];
		}
	}

	pFontTexture->UpdateTexture();
	m_isUpdate = true;
}

// M3-ETERLIB-TEXT-66: Text Rendering Architecture Summary
// ========================================================
// DX11 Path (DX11_STRICT_ONLY mode):
//   - RenderDX11() uses direct D3D11 API (lines 1183-1738)
//   - LCD two-pass subpixel rendering with explicit blend states
//   - No StateManager dependencies
//   - Baseline state: CGraphicDeviceDX11::SetUITextBaselineState()
//
// Legacy DX9 Path was removed from the runtime code path in this branch.
// Render() now uses RenderDX11() only; no StateManager fallback remains.
void CGraphicTextInstance::Render(RECT * pClipRect)
{
	// Try DX11 rendering first
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (pDX11Device && pDX11Device->IsValid())
	{
		// Startup/idle guard: no prepared glyph batch yet is not a DX11 failure.
		if (!m_isUpdate || m_pCharInfoVector.empty())
			return;

		if (RenderDX11(pClipRect))
		{
			// M3-CORE-51: Telemetry for strict mode compliance (30s heartbeat)
			static DWORD s_dwLastTelemetryTick = 0;
			const DWORD dwNow = timeGetTime();
			if (dwNow - s_dwLastTelemetryTick > 30000)
			{
				s_dwLastTelemetryTick = dwNow;
				TraceError("DX11_PIPELINE_STATE_PARITY pass=ui_text_strict path=dx11_native state_manager=unused");
			}
			return;
		}

		// M2-UI-TEXT-PATH-71: DX11-only text rendering - no legacy fallback available
		static DWORD s_dwLastDX11TextFailTick = 0;
		const DWORD dwNow = timeGetTime();
		if (0 == s_dwLastDX11TextFailTick || (dwNow - s_dwLastDX11TextFailTick) >= 5000)
		{
			s_dwLastDX11TextFailTick = dwNow;
			TraceError("DX11_TEXT_RENDER_FAIL reason=dx11_path_failed chars=%u update=%d",
				static_cast<unsigned int>(m_pCharInfoVector.size()),
				m_isUpdate ? 1 : 0);
		}
		return;
	}

	// M2-ETERLIB-TEXT-68: DX11-only - no DX9 fallback path, removed all legacy DX9 code
	// If DX11 path failed above, do not attempt DX9 rendering
	return;
}

bool CGraphicTextInstance::RenderDX11(RECT * pClipRect)
{
	// M2-ETERLIB-TEXT-68: Removed DX11_STRICT_ONLY guard - always use DX11 path
	if (!m_isUpdate)
		return true;

	CGraphicText* pkText = m_roText.GetPointer();
	if (!pkText)
	{
		static bool s_bLoggedTextResourceWait = false;
		__LogDX11TextFailOnce(s_bLoggedTextResourceWait, "font_resource_wait_text_pointer");
		return true;
	}

	CGraphicFontTexture* pFontTexture = pkText->GetFontTexturePointer();
	if (!pFontTexture)
	{
		static bool s_bLoggedFontTextureWait = false;
		__LogDX11TextFailOnce(s_bLoggedFontTextureWait, "font_resource_wait_texture_pointer");
		return true;
	}

	// Get DX11 device
	CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();
	if (!pDX11Device || !pDX11Device->IsValid())
	{
		// M2-UI-PARITY-FILTER-25: Track device invalidation when filter might be active
		static DWORD s_dwLastDeviceFailTick = 0;
		const DWORD dwNow = timeGetTime();
		if (dwNow - s_dwLastDeviceFailTick >= 10000)
		{
			s_dwLastDeviceFailTick = dwNow;
			TraceError("DX11_TEXT_FILTER_PARITY device_invalid_when_filter_active "
				"dx11_ptr=%p valid=%d",
				pDX11Device,
				pDX11Device ? pDX11Device->IsValid() : -1);
		}
		return false;
	}

	// Ensure bootstrap pipeline is ready
	if (!pDX11Device->EnsureBootstrapPipelineReady() || !pDX11Device->EnsureBootstrapUISamplerReady())
	{
		static bool s_bLoggedBootstrapFail = false;
		__LogDX11TextFailOnce(s_bLoggedBootstrapFail, "bootstrap_resources_incomplete");
		return false;
	}

	ID3D11DeviceContext* pContext = pDX11Device->GetContext();
	ID3D11Buffer* pVertexBuffer = pDX11Device->GetBootstrapUIVertexBuffer();
	ID3D11VertexShader* pVertexShader = pDX11Device->GetBootstrapUIVertexShader();
	ID3D11PixelShader* pPixelShader = pDX11Device->GetBootstrapUITexturePixelShader();
	ID3D11InputLayout* pInputLayout = pDX11Device->GetBootstrapUIInputLayout();
	ID3D11SamplerState* pSamplerState = pDX11Device->GetBootstrapUISamplerState();
	ID3D11BlendState* pAlphaBlendState = pDX11Device->GetBootstrapUIAlphaBlendState();
	ID3D11DepthStencilState* pDepthDisableState = pDX11Device->GetBootstrapUIDepthDisableState();

	if (!pContext || !pVertexBuffer || !pVertexShader || !pPixelShader || !pInputLayout || !pSamplerState || !pAlphaBlendState || !pDepthDisableState)
	{
		static bool s_bLoggedResourceFail = false;
		__LogDX11TextFailOnce(s_bLoggedResourceFail, "bootstrap_resources_null");
		return false;
	}

	// Use dedicated point sampler for glyph atlas to avoid blurred text while
	// keeping regular UI sprites on linear filtering.
	static ID3D11SamplerState* s_pDX11TextPointSampler = nullptr;
	if (!s_pDX11TextPointSampler)
	{
		if (ID3D11Device* pDevice = pDX11Device->GetDevice())
		{
			D3D11_SAMPLER_DESC kTextSamplerDesc = {};
			kTextSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			kTextSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			kTextSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			kTextSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			kTextSamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			kTextSamplerDesc.MinLOD = 0.0f;
			kTextSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			pDevice->CreateSamplerState(&kTextSamplerDesc, &s_pDX11TextPointSampler);
		}
	}

	// Get backbuffer dimensions for NDC conversion
	UINT uBackBufferWidth = 0, uBackBufferHeight = 0;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	if (uBackBufferWidth == 0 || uBackBufferHeight == 0)
	{
		static bool s_bLoggedBackbufferFail = false;
		__LogDX11TextFailOnce(s_bLoggedBackbufferFail, "backbuffer_size_invalid");
		return false;
	}

	// World/game passes can leave RT unbound; recover to main RT only when OM has no RTV.
	// If an overlay/filter RT is already active, preserve it to avoid breaking composed UI passes.
	ID3D11RenderTargetView* pPreBindRTV = nullptr;
	ID3D11DepthStencilView* pPreBindDSV = nullptr;
	pContext->OMGetRenderTargets(1, &pPreBindRTV, &pPreBindDSV);

	const bool bHadActiveRTV = (pPreBindRTV != nullptr);
	if (!bHadActiveRTV)
	{
		pDX11Device->BindMainRenderTargets();

		// M2-UI-TEXT-PATH-71: Also recover viewport after world→UI handoff
		D3D11_VIEWPORT kViewport = {};
		kViewport.TopLeftX = 0.0f;
		kViewport.TopLeftY = 0.0f;
		kViewport.Width = static_cast<float>(uBackBufferWidth);
		kViewport.Height = static_cast<float>(uBackBufferHeight);
		kViewport.MinDepth = 0.0f;
		kViewport.MaxDepth = 1.0f;
		pContext->RSSetViewports(1, &kViewport);
	}

	// M2-UI-PARITY-FILTER-25: Log only forced RT recovery path (when OM was null)
	ID3D11RenderTargetView* pPostBindRTV = nullptr;
	ID3D11DepthStencilView* pPostBindDSV = nullptr;
	pContext->OMGetRenderTargets(1, &pPostBindRTV, &pPostBindDSV);

	static DWORD s_dwLastRTRecoverLog = 0;
	if (!bHadActiveRTV && pPostBindRTV != nullptr)
	{
		const DWORD dwNow = timeGetTime();
		if (dwNow - s_dwLastRTRecoverLog >= 10000)
		{
			s_dwLastRTRecoverLog = dwNow;
			TraceError("DX11_TEXT_FILTER_PARITY rt_recovered_for_text rt_before=%p rt_after=%p",
				pPreBindRTV, pPostBindRTV);
		}
	}

	if (pPreBindRTV) pPreBindRTV->Release();
	if (pPreBindDSV) pPreBindDSV->Release();
	if (pPostBindRTV) pPostBindRTV->Release();
	if (pPostBindDSV) pPostBindDSV->Release();

	// DX11 Model Sync M3-TEXTTAIL22.A: Verify RT binding succeeded
	ID3D11RenderTargetView* pBoundRTV = nullptr;
	ID3D11DepthStencilView* pBoundDSV = nullptr;
	pContext->OMGetRenderTargets(1, &pBoundRTV, &pBoundDSV);
	static bool s_bLoggedRTMismatch = false;
	if (!pBoundRTV && !s_bLoggedRTMismatch)
	{
		s_bLoggedRTMismatch = true;
		TraceError("DX11_TEXTTAIL_RT_FAIL reason=main_rt_bind_failed");
	}
	if (pBoundRTV) pBoundRTV->Release();
	if (pBoundDSV) pBoundDSV->Release();

	// NDC conversion lambdas
	auto PixelToNDCX = [uBackBufferWidth](float fX) -> float {
		return (2.0f * fX / static_cast<float>(uBackBufferWidth)) - 1.0f;
	};
	auto PixelToNDCY = [uBackBufferHeight](float fY) -> float {
		return 1.0f - (2.0f * fY / static_cast<float>(uBackBufferHeight));
	};

	// SBootstrapVertex structure (must match DX11 input layout)
	struct SBootstrapVertex
	{
		float x, y, z;
		float r, g, b, a;
		float u, v;
	};

	// Calculate text position with alignment
	float fStanX = m_v3Position.x;
	float fStanY = m_v3Position.y + 1.0f;

	if (m_computedRTL)
	{
		switch (m_hAlign)
		{
			case HORIZONTAL_ALIGN_LEFT:
				fStanX -= m_textWidth;
				break;
			case HORIZONTAL_ALIGN_CENTER:
				fStanX -= float(m_textWidth / 2);
				break;
		}
	}
	else
	{
		switch (m_hAlign)
		{
			case HORIZONTAL_ALIGN_RIGHT:
				fStanX -= m_textWidth;
				break;
			case HORIZONTAL_ALIGN_CENTER:
				fStanX -= float(m_textWidth / 2);
				break;
		}
	}

	switch (m_vAlign)
	{
		case VERTICAL_ALIGN_BOTTOM:
			fStanY -= m_textHeight;
			break;
		case VERTICAL_ALIGN_CENTER:
			fStanY -= float(m_textHeight) / 2.0f;
			break;
	}

	// Build vertex batches per texture page (using font atlas API from Model 1)
	std::unordered_map<DWORD, std::vector<SBootstrapVertex>> outlineBatches;
	std::unordered_map<DWORD, std::vector<SBootstrapVertex>> mainBatches;
	// Screen-space UI/text in DX11 must keep a stable clip-space Z.
	// Using legacy per-instance Z can clip texttails intermittently.
	const float fUIZ = 0.0f;

	const float fFontHalfWeight = 1.0f;
	float fCurX, fCurY, fFontMaxHeight;
	CGraphicFontTexture::TCharacterInfomation* pCurCharInfo;

	// Build outline batches (if outline enabled)
	if (m_isOutline)
	{
		fCurX = fStanX;
		fCurY = fStanY;
		fFontMaxHeight = 0.0f;

		// Convert outline color DWORD to float RGBA
		float outlineR = ((m_dwOutLineColor >> 16) & 0xFF) / 255.0f;
		float outlineG = ((m_dwOutLineColor >> 8) & 0xFF) / 255.0f;
		float outlineB = ((m_dwOutLineColor) & 0xFF) / 255.0f;
		float outlineA = ((m_dwOutLineColor >> 24) & 0xFF) / 255.0f;

		for (int charIdx = 0; charIdx < (int)m_pCharInfoVector.size(); ++charIdx)
		{
			pCurCharInfo = m_pCharInfoVector[charIdx];

			float fKern = (charIdx < (int)m_kernVector.size()) ? m_kernVector[charIdx] : 0.0f;
			fCurX += fKern;

			float fFontWidth = float(pCurCharInfo->width);
			float fFontHeight = float(pCurCharInfo->height);
			float fFontAdvance = float(pCurCharInfo->advance);
			fFontMaxHeight = std::max(fFontMaxHeight, fFontHeight);

			// Multi-line wrapping
			if ((fCurX + fFontWidth) - m_v3Position.x > m_fLimitWidth)
			{
				if (m_isMultiLine)
				{
					fCurX = fStanX;
					fCurY += fFontMaxHeight;
				}
				else
				{
					break;
				}
			}

			// Clipping
			if (pClipRect && fCurY <= pClipRect->top)
			{
				fCurX += fFontAdvance;
				continue;
			}

			float fFontSx = fCurX + pCurCharInfo->bearingX - 0.5f;
			float fFontSy = fCurY - 0.5f;
			float fFontEx = fFontSx + fFontWidth;
			float fFontEy = fFontSy + fFontHeight;

			DWORD dwTexturePage = pCurCharInfo->index;
			std::vector<SBootstrapVertex>& vtxBatch = outlineBatches[dwTexturePage];

			// Outline is rendered in 4 directions (left, right, top, bottom) with 2 triangles each
			// Left outline
			{
				float x0 = PixelToNDCX(fFontSx - fFontHalfWeight);
				float x1 = PixelToNDCX(fFontEx - fFontHalfWeight);
				float y0 = PixelToNDCY(fFontSy);
				float y1 = PixelToNDCY(fFontEy);

				SBootstrapVertex v0 = { x0, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->top };
				SBootstrapVertex v1 = { x0, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->bottom };
				SBootstrapVertex v2 = { x1, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->top };
				SBootstrapVertex v3 = { x1, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->bottom };

				vtxBatch.push_back(v0); vtxBatch.push_back(v1); vtxBatch.push_back(v2);
				vtxBatch.push_back(v2); vtxBatch.push_back(v1); vtxBatch.push_back(v3);
			}

			// Right outline
			{
				float x0 = PixelToNDCX(fFontSx + fFontHalfWeight);
				float x1 = PixelToNDCX(fFontEx + fFontHalfWeight);
				float y0 = PixelToNDCY(fFontSy);
				float y1 = PixelToNDCY(fFontEy);

				SBootstrapVertex v0 = { x0, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->top };
				SBootstrapVertex v1 = { x0, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->bottom };
				SBootstrapVertex v2 = { x1, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->top };
				SBootstrapVertex v3 = { x1, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->bottom };

				vtxBatch.push_back(v0); vtxBatch.push_back(v1); vtxBatch.push_back(v2);
				vtxBatch.push_back(v2); vtxBatch.push_back(v1); vtxBatch.push_back(v3);
			}

			// Top outline
			{
				float x0 = PixelToNDCX(fFontSx);
				float x1 = PixelToNDCX(fFontEx);
				float y0 = PixelToNDCY(fFontSy - fFontHalfWeight);
				float y1 = PixelToNDCY(fFontEy - fFontHalfWeight);

				SBootstrapVertex v0 = { x0, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->top };
				SBootstrapVertex v1 = { x0, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->bottom };
				SBootstrapVertex v2 = { x1, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->top };
				SBootstrapVertex v3 = { x1, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->bottom };

				vtxBatch.push_back(v0); vtxBatch.push_back(v1); vtxBatch.push_back(v2);
				vtxBatch.push_back(v2); vtxBatch.push_back(v1); vtxBatch.push_back(v3);
			}

			// Bottom outline
			{
				float x0 = PixelToNDCX(fFontSx);
				float x1 = PixelToNDCX(fFontEx);
				float y0 = PixelToNDCY(fFontSy + fFontHalfWeight);
				float y1 = PixelToNDCY(fFontEy + fFontHalfWeight);

				SBootstrapVertex v0 = { x0, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->top };
				SBootstrapVertex v1 = { x0, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->left, pCurCharInfo->bottom };
				SBootstrapVertex v2 = { x1, y0, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->top };
				SBootstrapVertex v3 = { x1, y1, fUIZ, outlineR, outlineG, outlineB, outlineA, pCurCharInfo->right, pCurCharInfo->bottom };

				vtxBatch.push_back(v0); vtxBatch.push_back(v1); vtxBatch.push_back(v2);
				vtxBatch.push_back(v2); vtxBatch.push_back(v1); vtxBatch.push_back(v3);
			}

			fCurX += fFontAdvance;
		}
	}

	// Build main text batches
	fCurX = fStanX;
	fCurY = fStanY;
	fFontMaxHeight = 0.0f;

	for (int i = 0; i < (int)m_pCharInfoVector.size(); ++i)
	{
		pCurCharInfo = m_pCharInfoVector[i];

		float fKern = (i < (int)m_kernVector.size()) ? m_kernVector[i] : 0.0f;
		fCurX += fKern;

		float fFontWidth = float(pCurCharInfo->width);
		float fFontHeight = float(pCurCharInfo->height);
		float fFontAdvance = float(pCurCharInfo->advance);
		fFontMaxHeight = std::max(fFontMaxHeight, fFontHeight);

		// Multi-line wrapping
		if ((fCurX + fFontWidth) - m_v3Position.x > m_fLimitWidth)
		{
			if (m_isMultiLine)
			{
				fCurX = fStanX;
				fCurY += fFontMaxHeight;
			}
			else
			{
				break;
			}
		}

		// Clipping
		if (pClipRect && fCurY <= pClipRect->top)
		{
			fCurX += fFontAdvance;
			continue;
		}

		float fFontSx = fCurX + pCurCharInfo->bearingX - 0.5f;
		float fFontSy = fCurY - 0.5f;
		float fFontEx = fFontSx + fFontWidth;
		float fFontEy = fFontSy + fFontHeight;

		DWORD dwTexturePage = pCurCharInfo->index;
		std::vector<SBootstrapVertex>& vtxBatch = mainBatches[dwTexturePage];

		// Convert per-character color DWORD to float RGBA
		DWORD dwCharColor = m_dwColorInfoVector[i];
		float charR = ((dwCharColor >> 16) & 0xFF) / 255.0f;
		float charG = ((dwCharColor >> 8) & 0xFF) / 255.0f;
		float charB = ((dwCharColor) & 0xFF) / 255.0f;
		float charA = ((dwCharColor >> 24) & 0xFF) / 255.0f;

		// Convert to NDC
		float x0 = PixelToNDCX(fFontSx);
		float x1 = PixelToNDCX(fFontEx);
		float y0 = PixelToNDCY(fFontSy);
		float y1 = PixelToNDCY(fFontEy);

		SBootstrapVertex v0 = { x0, y0, fUIZ, charR, charG, charB, charA, pCurCharInfo->left, pCurCharInfo->top };
		SBootstrapVertex v1 = { x0, y1, fUIZ, charR, charG, charB, charA, pCurCharInfo->left, pCurCharInfo->bottom };
		SBootstrapVertex v2 = { x1, y0, fUIZ, charR, charG, charB, charA, pCurCharInfo->right, pCurCharInfo->top };
		SBootstrapVertex v3 = { x1, y1, fUIZ, charR, charG, charB, charA, pCurCharInfo->right, pCurCharInfo->bottom };

		vtxBatch.push_back(v0); vtxBatch.push_back(v1); vtxBatch.push_back(v2);
		vtxBatch.push_back(v2); vtxBatch.push_back(v1); vtxBatch.push_back(v3);

		fCurX += fFontAdvance;
	}

	// Set up DX11 pipeline state
	UINT uStride = sizeof(SBootstrapVertex);
	UINT uOffset = 0;
	pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &uStride, &uOffset);
	pContext->IASetInputLayout(pInputLayout);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->VSSetShader(pVertexShader, nullptr, 0);
	pContext->PSSetShader(pPixelShader, nullptr, 0);
	ID3D11SamplerState* pActiveTextSampler = s_pDX11TextPointSampler ? s_pDX11TextPointSampler : pSamplerState;
	pContext->PSSetSamplers(0, 1, &pActiveTextSampler);
	pContext->OMSetDepthStencilState(pDepthDisableState, 0);
	// M2-ETERLIB-STATE-48: Unconditional rasterizer state setting (was conditional)
	pContext->RSSetState(pDX11Device->GetBootstrapRasterizerState());

	// M2-ETERLIB-STATE-48: Set explicit text baseline state
	// M2-ETERLIB-STATE-49: DX11 shader-state is used instead of legacy SetTextureStageState (which is #else-guarded)
	pDX11Device->SetUITextBaselineState();

	// M2-UI-TEXTTAIL-PARITY-26: Track SRV failures for glyph emission diagnostics (persistent across frames)
	static int s_iSRVFailures = 0;
	static int s_iPersistentSRVFailures = 0;

	// LCD two-pass rendering helper
	auto DrawBatchLCD = [&](const std::unordered_map<DWORD, std::vector<SBootstrapVertex>>& batches, bool skipPass2) -> int
	{
		int iTotalGlyphQuads = 0;

		for (const auto& [dwTexturePage, vtxBatch] : batches)
		{
			if (vtxBatch.empty())
				continue;

			// Get texture SRV from font atlas using Model 1's API
			ID3D11ShaderResourceView* pTextureSRV = pFontTexture->GetTexturePageSRV(dwTexturePage);
			if (!pTextureSRV)
			{
				++s_iSRVFailures; // M3-TEXTTAIL22.A: Track SRV failures
				++s_iPersistentSRVFailures; // M2-UI-TEXTTAIL-PARITY-26: Persistent tracking
				static std::set<DWORD> s_loggedPages;
				if (s_loggedPages.find(dwTexturePage) == s_loggedPages.end())
				{
					s_loggedPages.insert(dwTexturePage);
					TraceError("DX11_TEXT_RENDER_FAIL reason=texture_srv_null page=%u", dwTexturePage);
				}
				continue;
			}

			// Map vertex buffer
			D3D11_MAPPED_SUBRESOURCE kMappedResource = {};
			HRESULT hr = pContext->Map(pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &kMappedResource);
			if (FAILED(hr))
			{
				static bool s_bLoggedMapFail = false;
				if (!s_bLoggedMapFail)
				{
					s_bLoggedMapFail = true;
					TraceError("DX11_TEXT_RENDER_FAIL reason=vertex_buffer_map_failed");
				}
				continue;
			}

			memcpy(kMappedResource.pData, vtxBatch.data(), vtxBatch.size() * sizeof(SBootstrapVertex));
			pContext->Unmap(pVertexBuffer, 0);

			// Bind texture
			pContext->PSSetShaderResources(0, 1, &pTextureSRV);

			// Pass 1: dest.rgb *= (1 - coverage.rgb) - ZERO, INVSRCCOLOR blend
			ID3D11BlendState* pPass1BlendState = pDX11Device->GetBootstrapUILCDPass1BlendState();
			if (pPass1BlendState)
			{
				float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				pContext->OMSetBlendState(pPass1BlendState, blendFactor, 0xFFFFFFFF);
			}
			else
			{
				// Fallback: use alpha blend (not ideal for LCD, but won't crash)
				float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				pContext->OMSetBlendState(pAlphaBlendState, blendFactor, 0xFFFFFFFF);
			}

			pContext->Draw((UINT)vtxBatch.size(), 0);

			if (!skipPass2)
			{
				// Pass 2: dest.rgb += textColor.rgb * coverage.rgb - ONE, ONE blend
				ID3D11BlendState* pPass2BlendState = pDX11Device->GetBootstrapUILCDPass2BlendState();
				if (pPass2BlendState)
				{
					float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
					pContext->OMSetBlendState(pPass2BlendState, blendFactor, 0xFFFFFFFF);
				}
				else
				{
					// Fallback: use alpha blend
					float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
					pContext->OMSetBlendState(pAlphaBlendState, blendFactor, 0xFFFFFFFF);
				}

				pContext->Draw((UINT)vtxBatch.size(), 0);
			}

			iTotalGlyphQuads += (int)(vtxBatch.size() / 6);
		}

		return iTotalGlyphQuads;
	};

	// Render outline batches first (skip Pass 2 if black outline)
	bool outlineIsBlack = ((m_dwOutLineColor & 0x00FFFFFF) == 0);
	int iOutlineQuads = DrawBatchLCD(outlineBatches, outlineIsBlack);

	// Render main text batches (always both passes)
	int iMainQuads = DrawBatchLCD(mainBatches, false);

	// M2-UI-TEXTTAIL-PARITY-26: Enhanced telemetry - glyph emission breakdown with persistent tracking
	static DWORD s_dwLastTelemetryTime = 0;
	DWORD dwNow = timeGetTime();
	if (dwNow - s_dwLastTelemetryTime > 5000)
	{
		s_dwLastTelemetryTime = dwNow;
		const int iTotalGlyphsSubmitted = static_cast<int>(m_pCharInfoVector.size());
		const int iGlyphsEmitted = iOutlineQuads + iMainQuads;
		TraceError("DX11_TEXT_RENDER_DX11 glyphs_submitted=%d glyphs_emitted=%d "
			"srv_failures=%d srv_failures_persistent=%d outline_quads=%d main_quads=%d",
			iTotalGlyphsSubmitted,
			iGlyphsEmitted,
			s_iSRVFailures,
			s_iPersistentSRVFailures,
			iOutlineQuads,
			iMainQuads);
		s_iSRVFailures = 0;
		// NOTE: s_iPersistentSRVFailures is never reset - accumulates for lifetime of process
	}

	// M2-ETERLIB-TEXT-68: Port cursor/selection rendering from DX9 to DX11
	if (m_isCursor)
	{
		// Draw Cursor
		float sx, sy, ex, ey;
		DirectX::SimpleMath::Color diffuse(1.0f, 1.0f, 1.0f, 1.0f); // M2-ETERLIB-TEXT-68

		int curpos = CIME::GetCurPos();
		int compend = curpos + CIME::GetCompLen();

		// Convert logical cursor position to visual position (handles tags)
		int visualCurpos = curpos;
		int visualCompend = compend;
		if (!m_logicalToVisualPos.empty())
		{
			if (curpos >= 0 && curpos < (int)m_logicalToVisualPos.size())
				visualCurpos = m_logicalToVisualPos[curpos];
			if (compend >= 0 && compend < (int)m_logicalToVisualPos.size())
				visualCompend = m_logicalToVisualPos[compend];
		}

		__GetTextPos(visualCurpos, &sx, &sy);

		// If Composition
		if(visualCurpos<visualCompend)
		{
			diffuse = DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 0.5f); // 0x7fffffff - M2-ETERLIB-TEXT-68
			__GetTextPos(visualCompend, &ex, &sy);
		}
		else
		{
			diffuse = DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f); // 0xffffffff - M2-ETERLIB-TEXT-68
			ex = sx + 2;
		}

		// FOR_ARABIC_ALIGN
		// Use the computed direction for this text instance, not the global UI direction
		if (m_computedRTL)
		{
			sx += m_v3Position.x - m_textWidth;
			ex += m_v3Position.x - m_textWidth;
			sy += m_v3Position.y;
		}
		else
		{
			sx += m_v3Position.x;
			sy += m_v3Position.y;
			ex += m_v3Position.x;
		}

		// Apply vertical alignment adjustment BEFORE calculating ey
		switch (m_vAlign)
		{
			case VERTICAL_ALIGN_BOTTOM:
				sy -= m_textHeight;
				break;

			case VERTICAL_ALIGN_CENTER:
				sy -= float(m_textHeight) / 2.0f;
				break;
		}

		// NOW calculate ey after sy has been adjusted
		ey = sy + m_textHeight;

		// M3-UI-TEXT-IME-CURSOR-70: DX11 cursor rendering implementation
		if (m_isCursor)
		{
			float cursorX = sx;
			float cursorY = sy;
			__GetTextPos(visualCurpos, &cursorX, &cursorY);

			cursorX += m_v3Position.x;
			cursorY += m_v3Position.y;

			// Draw cursor as a thin vertical bar (2 pixels wide, text height tall)
			const float cursorWidth = 2.0f;
			CScreen screen;
			screen.SetDiffuseColor(1.0f, 1.0f, 1.0f, 1.0f); // White cursor
			screen.RenderBar2dDX11(cursorX, cursorY, cursorX + cursorWidth, cursorY + m_textHeight, 0.0f);
		}

		// M3-UI-TEXT-IME-UNDERLINE-70: DX11 underline rendering implementation
		int ulbegin = CIME::GetULBegin();
		int ulend = CIME::GetULEnd();

		if (ulbegin < ulend)
		{
			__GetTextPos(curpos+ulbegin, &sx, &sy);
			__GetTextPos(curpos+ulend, &ex, &sy);

			sx += m_v3Position.x;
			sy += m_v3Position.y + m_textHeight;
			ex += m_v3Position.x;
			ey = sy + 2;

			// Draw underline in red (0xFFFF0000)
			CScreen screen;
			screen.SetDiffuseColor(1.0f, 0.0f, 0.0f, 1.0f);
			screen.RenderBar2dDX11(sx, sy, ex, ey, 0.0f);
		}
	}

	// M2-ETERLIB-TEXT-68: Port hyperlink hit testing from DX9 to DX11 (plain code, no DX11 calls needed)
	if (m_hyperlinkVector.size() != 0)
	{
		// FOR_ARABIC_ALIGN: RTL text is drawn with offset (m_v3Position.x - m_textWidth)
		// Use the computed direction for this text instance, not the global UI direction
		int textOffsetX = m_computedRTL ? (int)(m_v3Position.x - m_textWidth) : (int)m_v3Position.x;

		int lx = gs_mx - textOffsetX;
		int ly = gs_my - (int)m_v3Position.y;

		if (lx >= 0 && ly >= 0 && lx < m_textWidth && ly < m_textHeight)
		{
			std::vector<SHyperlink>::iterator it = m_hyperlinkVector.begin();

			while (it != m_hyperlinkVector.end())
			{
				SHyperlink & link = *it++;
				if (lx >= link.sx && lx < link.ex)
				{
					gs_hyperlinkText = link.text;
					break;
				}
			}
		}
	}

	return true;
}

void CGraphicTextInstance::CreateSystem(UINT uCapacity)
{
	ms_kPool.Create(uCapacity);
}

void CGraphicTextInstance::DestroySystem()
{
	ms_kPool.Destroy();
}

CGraphicTextInstance* CGraphicTextInstance::New()
{
	return ms_kPool.Alloc();
}

void CGraphicTextInstance::Delete(CGraphicTextInstance* pkInst)
{
	pkInst->Destroy();
	ms_kPool.Free(pkInst);
}

void CGraphicTextInstance::ShowCursor()
{
	m_isCursor = true;
}

void CGraphicTextInstance::HideCursor()
{
	m_isCursor = false;
}

void CGraphicTextInstance::SetSelection(int iStart, int iEnd)
{
	m_selStart = iStart;
	m_selEnd = iEnd;
}

void CGraphicTextInstance::ClearSelection()
{
	m_selStart = 0;
	m_selEnd = 0;
}

void CGraphicTextInstance::ShowOutLine()
{
	m_isOutline = true;
}

void CGraphicTextInstance::HideOutLine()
{
	m_isOutline = false;
}

void CGraphicTextInstance::SetColor(DWORD color)
{
	if (m_dwTextColor != color)
	{
		for (int i = 0; i < m_pCharInfoVector.size(); ++i)
			if (m_dwColorInfoVector[i] == m_dwTextColor)
				m_dwColorInfoVector[i] = color;

		m_dwTextColor = color;
	}
}

void CGraphicTextInstance::SetColor(float r, float g, float b, float a)
{
	SetColor(D3DXCOLOR(r, g, b, a));
}

void CGraphicTextInstance::SetOutLineColor(DWORD color)
{
	m_dwOutLineColor=color;
}

void CGraphicTextInstance::SetOutLineColor(float r, float g, float b, float a)
{
	m_dwOutLineColor=D3DXCOLOR(r, g, b, a);
}

void CGraphicTextInstance::SetSecret(bool Value)
{
	m_isSecret = Value;
}

void CGraphicTextInstance::SetOutline(bool Value)
{
	m_isOutline = Value;
}

void CGraphicTextInstance::SetFeather(bool Value)
{
	if (Value)
	{
		m_fFontFeather = c_fFontFeather;
	}
	else
	{
		m_fFontFeather = 0.0f;
	}
}

void CGraphicTextInstance::SetMultiLine(bool Value)
{
	m_isMultiLine = Value;
}

void CGraphicTextInstance::SetHorizonalAlign(int hAlign)
{
	m_hAlign = hAlign;
}

void CGraphicTextInstance::SetVerticalAlign(int vAlign)
{
	m_vAlign = vAlign;
}

void CGraphicTextInstance::SetMax(int iMax)
{
	m_iMax = iMax;
}

void CGraphicTextInstance::SetLimitWidth(float fWidth)
{
	m_fLimitWidth = fWidth;
}

void CGraphicTextInstance::SetValueString(const std::string& c_stValue)
{
	if (0 == m_stText.compare(c_stValue))
		return;

	m_stText = c_stValue;
	m_isUpdate = false;
}

void CGraphicTextInstance::SetValue(const char* c_szText, size_t len)
{
	if (0 == m_stText.compare(c_szText))
		return;

	m_stText = c_szText;
	m_isChatMessage = false; // Reset chat mode
	m_isUpdate = false;
}

void CGraphicTextInstance::SetChatValue(const char* c_szName, const char* c_szMessage)
{
	if (!c_szName || !c_szMessage)
		return;

	// Store separated components
	m_chatName = c_szName;
	m_chatMessage = c_szMessage;
	m_isChatMessage = true;

	// Build combined text for rendering (will be processed by Update())
	// Use BuildVisualChatMessage in Update() instead of BuildVisualBidiText_Tagless
	m_stText = std::string(c_szName) + " : " + std::string(c_szMessage);
	m_isUpdate = false;
}

void CGraphicTextInstance::SetPosition(float fx, float fy, float fz)
{
	m_v3Position.x = fx;
	m_v3Position.y = fy;
	m_v3Position.z = fz;
}

void CGraphicTextInstance::SetTextPointer(CGraphicText* pText)
{
	m_roText = pText;
}

void CGraphicTextInstance::SetTextDirection(ETextDirection dir)
{
	if (m_direction == dir)
		return;

	m_direction = dir;
	m_isUpdate = false;
}

const std::string & CGraphicTextInstance::GetValueStringReference()
{
	return m_stText;
}

WORD CGraphicTextInstance::GetTextLineCount()
{
	CGraphicFontTexture::TCharacterInfomation* pCurCharInfo;
	CGraphicFontTexture::TPCharacterInfomationVector::iterator itor;

	float fx = 0.0f;
	WORD wLineCount = 1;
	for (itor=m_pCharInfoVector.begin(); itor!=m_pCharInfoVector.end(); ++itor)
	{
		pCurCharInfo = *itor;

		float fFontWidth=float(pCurCharInfo->width);
		float fFontAdvance=float(pCurCharInfo->advance);
		//float fFontHeight=float(pCurCharInfo->height);

		// NOTE : í°íŠ¸ ì¶œë ¥ì— Width ì œí•œì„ ë‘¡ë‹ˆë‹¤. - [levites]
		if (fx+fFontWidth > m_fLimitWidth)
		{
			fx = 0.0f;
			++wLineCount;
		}

		fx += fFontAdvance;
	}

	return wLineCount;
}

void CGraphicTextInstance::GetTextSize(int* pRetWidth, int* pRetHeight)
{
	*pRetWidth = m_textWidth;
	*pRetHeight = m_textHeight;
}

int CGraphicTextInstance::PixelPositionToCharacterPosition(int iPixelPosition)
{
	// Clamp to valid range [0, textWidth]
	int adjustedPixelPos = iPixelPosition;
	if (adjustedPixelPos < 0)
		adjustedPixelPos = 0;
	if (adjustedPixelPos > m_textWidth)
		adjustedPixelPos = m_textWidth;

	// RTL: interpret click from right edge of rendered text
	if (m_computedRTL)
		adjustedPixelPos = m_textWidth - adjustedPixelPos;

	int icurPosition = 0;
	int visualPos = -1;

	for (int i = 0; i < (int)m_pCharInfoVector.size(); ++i)
	{
		CGraphicFontTexture::TCharacterInfomation* pCurCharInfo = m_pCharInfoVector[i];

		// Use advance instead of width (width is not reliable for cursor hit-testing)
		int adv = pCurCharInfo->advance;
		if (adv <= 0)
			adv = pCurCharInfo->width;

		int charStart = icurPosition;
		icurPosition += adv;

		int charMid = charStart + adv / 2;

		if (adjustedPixelPos < charMid)
		{
			visualPos = i;
			break;
		}
	}

	if (visualPos < 0)
		visualPos = (int)m_pCharInfoVector.size();

	if (!m_visualToLogicalPos.empty() && visualPos >= 0 && visualPos < (int)m_visualToLogicalPos.size())
		return m_visualToLogicalPos[visualPos];

	return visualPos;
}

int CGraphicTextInstance::GetHorizontalAlign()
{
	return m_hAlign;
}

void CGraphicTextInstance::__Initialize()
{
	m_roText = NULL;

	m_hAlign = HORIZONTAL_ALIGN_LEFT;
	m_vAlign = VERTICAL_ALIGN_TOP;

	m_iMax = 0;
	m_fLimitWidth = 1600.0f;

	m_isCursor = false;
	m_isSecret = false;
	m_isMultiLine = false;

	m_selStart = 0;
	m_selEnd = 0;

	m_isOutline = false;
	m_fFontFeather = c_fFontFeather;

	m_isUpdate = false;
	// Use Auto direction for input fields - they should auto-detect based on content
	// Only chat messages should be explicitly set to RTL
	m_direction = ETextDirection::Auto;
	m_computedRTL = false;
	m_isChatMessage = false;
	m_chatName = "";
	m_chatMessage = "";

	m_textWidth = 0;
	m_textHeight = 0;

	m_v3Position.x = m_v3Position.y = m_v3Position.z = 0.0f;

	m_dwOutLineColor=0xff000000;
}

void CGraphicTextInstance::Destroy()
{
	m_stText="";
	m_pCharInfoVector.clear();
	m_kernVector.clear();
	m_dwColorInfoVector.clear();
	m_hyperlinkVector.clear();
	m_logicalToVisualPos.clear();
	m_visualToLogicalPos.clear();

	__Initialize();
}

CGraphicTextInstance::CGraphicTextInstance()
{
	__Initialize();
}

CGraphicTextInstance::~CGraphicTextInstance()
{
	Destroy();
}
