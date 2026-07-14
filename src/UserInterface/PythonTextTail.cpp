//
// 캐릭터를 따라다니는 텍스트 관련 소스 (이름, 길드이름, 길드마크 등)
//
#include "stdafx.h"
#include "InstanceBase.h"
#include "resource.h"
#include "PythonTextTail.h"
#include "PythonCharacterManager.h"
#include "PythonPlayer.h"
#include "PythonGuild.h"
#include "Locale.h"
#include "MarkManager.h"
#include "PackLib/PackManager.h"
#include "EterLib/GrpDeviceDX11.h"
#include "config.h"

#include <utf8.h>
// EPlaceDir and TextTailBiDi() template are defined in utf8.h

const D3DXCOLOR c_TextTail_Player_Color = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
const D3DXCOLOR c_TextTail_Monster_Color = D3DXCOLOR(1.0f, 0.0f, 0.0f, 1.0f);
const D3DXCOLOR c_TextTail_Item_Color = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
const D3DXCOLOR c_TextTail_Chat_Color = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
const D3DXCOLOR c_TextTail_Info_Color = D3DXCOLOR(1.0f, 0.785f, 0.785f, 1.0f);
const D3DXCOLOR c_TextTail_Guild_Name_Color = 0xFFEFD3FF;
const float c_TextTail_Name_Position = -10.0f;
const float c_fxMarkPosition = 1.5f;
const float c_fyGuildNamePosition = 15.0f;
const float c_fyMarkPosition = 15.0f + 11.0f;
BOOL bPKTitleEnable = TRUE;

// DX11 Model Sync M3-TEXTTAIL-29: Early culling telemetry
namespace {
	inline void LogEarlyCullStats()
	{
		static DWORD s_dwLastHeartbeatTick = 0u;
		static DWORD s_dwEarlyDistanceCulls = 0u;
		static DWORD s_dwOwnerInvalidCulls = 0u;
		static DWORD s_dwProjectionAttempts = 0u;

		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwLastHeartbeatTick || (dwNow - s_dwLastHeartbeatTick) >= 30000u)
		{
			s_dwLastHeartbeatTick = dwNow;
			if (s_dwProjectionAttempts > 0u || s_dwEarlyDistanceCulls > 0u || s_dwOwnerInvalidCulls > 0u)
			{
				const DWORD dwTotalCulls = s_dwEarlyDistanceCulls + s_dwOwnerInvalidCulls;
				const DWORD dwProjections = s_dwProjectionAttempts;
				const float fCullRate = (dwProjections > 0u) ? (100.0f * static_cast<float>(dwTotalCulls) / static_cast<float>(dwProjections + dwTotalCulls)) : 0.0f;
				TraceError("DX11_TEXTTAIL_CULL_EARLY_STATS projections=%u early_dist_culls=%u owner_invalid_culls=%u cull_rate=%.1f%% interval_ms=30000",
					dwProjections, s_dwEarlyDistanceCulls, s_dwOwnerInvalidCulls, fCullRate);
				s_dwProjectionAttempts = 0u;
				s_dwEarlyDistanceCulls = 0u;
				s_dwOwnerInvalidCulls = 0u;
			}
		}
	}

	inline void IncrementEarlyDistanceCull()
	{
		static DWORD s_dwEarlyDistanceCulls = 0u;
		++s_dwEarlyDistanceCulls;
		LogEarlyCullStats();
	}

	inline void IncrementOwnerInvalidCull()
	{
		static DWORD s_dwOwnerInvalidCulls = 0u;
		++s_dwOwnerInvalidCulls;
		LogEarlyCullStats();
	}

	inline void IncrementProjectionAttempt()
	{
		static DWORD s_dwProjectionAttempts = 0u;
		++s_dwProjectionAttempts;
		LogEarlyCullStats();
	}
}

// M2-UIPY-40: Global visibility state tracking (shared between Render and ShowCharacterTextTail)
static int g_iStateBlocked = 0;

// TEXTTAIL_LIVINGTIME_CONTROL
long gs_TextTail_LivingTime = 5000;

long TextTail_GetLivingTime()
{
	assert(gs_TextTail_LivingTime>1000);
	return gs_TextTail_LivingTime;
}

void TextTail_SetLivingTime(long livingTime)
{
	gs_TextTail_LivingTime = livingTime;
}
// END_OF_TEXTTAIL_LIVINGTIME_CONTROL

CGraphicText * ms_pFont = NULL;

void CPythonTextTail::GetInfo(std::string* pstInfo)
{
	char szInfo[256];
	sprintf(szInfo, "TextTail: ChatTail %zd, ChrTail (Map %zd, List %zd), ItemTail (Map %zd, List %zd), Pool %zd", 
		m_ChatTailMap.size(), 
		m_CharacterTextTailMap.size(), m_CharacterTextTailList.size(), 
		m_ItemTextTailMap.size(), m_ItemTextTailList.size(), 
		m_TextTailPool.GetCapacity());

	pstInfo->append(szInfo);
}

void CPythonTextTail::SetOptimizationSettings(bool bEnable, int iProfile)
{
	m_bOptimizationEnabled = bEnable ? true : false;
	m_iOptimizationProfile = iProfile;
	if (m_iOptimizationProfile < 0)
		m_iOptimizationProfile = 0;
	else if (m_iOptimizationProfile > 2)
		m_iOptimizationProfile = 2;

	if (!m_bOptimizationEnabled || m_iOptimizationProfile <= 0)
	{
		m_dwArrangeIntervalMS = 0;
		m_iMaxRenderCount = 1000000;
	}
	else if (m_iOptimizationProfile == 1)
	{
		m_dwArrangeIntervalMS = 100;
		m_iMaxRenderCount = 80;
	}
	else
	{
		m_dwArrangeIntervalMS = 130;
		m_iMaxRenderCount = 50;
	}

	m_dwLastArrangeTime = 0;
}

void CPythonTextTail::SetGridOptimizationEnabled(bool bEnable)
{
	m_bGridOptimizationEnabled = bEnable ? true : false;
}

bool CPythonTextTail::IsGridOptimizationEnabled() const
{
	return m_bGridOptimizationEnabled;
}

void CPythonTextTail::SetOptimizationRange(float fMaxDistance)
{
	if (fMaxDistance < 1500.0f)
		fMaxDistance = 1500.0f;
	else if (fMaxDistance > 9000.0f)
		fMaxDistance = 9000.0f;

	m_fOptimizationMaxDistance = fMaxDistance;
}

float CPythonTextTail::GetOptimizationRange() const
{
	return m_fOptimizationMaxDistance;
}

DWORD CPythonTextTail::GetVisibleTextTailCount() const
{
	return static_cast<DWORD>(m_CharacterTextTailList.size() + m_ItemTextTailList.size() + m_ChatTailMap.size());
}

DWORD CPythonTextTail::GetLastFrameMS() const
{
	return m_dwLastArrangeMS + m_dwLastRenderMS;
}

DWORD CPythonTextTail::GetLastCollisionCheckCount() const
{
	return m_dwLastCollisionChecks;
}

void CPythonTextTail::UpdateAllTextTail()
{
	CInstanceBase * pInstance = CPythonCharacterManager::Instance().GetMainInstancePtr();
	if (pInstance)
	{
		TPixelPosition pixelPos;
		pInstance->NEW_GetPixelPosition(&pixelPos);

		TTextTailMap::iterator itorMap;

		for (itorMap = m_CharacterTextTailMap.begin(); itorMap != m_CharacterTextTailMap.end(); ++itorMap)
		{
			UpdateDistance(pixelPos, itorMap->second);
		}

		for (itorMap = m_ItemTextTailMap.begin(); itorMap != m_ItemTextTailMap.end(); ++itorMap)
		{
			UpdateDistance(pixelPos, itorMap->second);
		}

		for (TChatTailMap::iterator itorChat=m_ChatTailMap.begin(); itorChat!=m_ChatTailMap.end(); ++itorChat)
		{
			UpdateDistance(pixelPos, itorChat->second);

			// NOTE : Chat TextTail이 있으면 캐릭터 이름도 출력한다.
			if (itorChat->second->bNameFlag)
			{
				DWORD dwVID = itorChat->first;
				ShowCharacterTextTail(dwVID);
			}
		}
	}
}

void CPythonTextTail::UpdateShowingTextTail()
{
	TTextTailList::iterator itor;

	// DX11 Model Sync M3-TEXTTAIL-29: Early distance culling for item texttails
	for (itor = m_ItemTextTailList.begin(); itor != m_ItemTextTailList.end(); ++itor)
	{
		TTextTail* pTextTail = *itor;
		if (m_bOptimizationEnabled && pTextTail->fDistanceFromPlayer > m_fOptimizationMaxDistance)
		{
			IncrementEarlyDistanceCull();
			continue; // Skip expensive projection for far objects
		}
		UpdateTextTail(pTextTail);
	}

	// DX11 Model Sync M3-TEXTTAIL-29: Early distance culling for chat texttails
	for (TChatTailMap::iterator itorChat=m_ChatTailMap.begin(); itorChat!=m_ChatTailMap.end(); ++itorChat)
	{
		TTextTail* pTextTail = itorChat->second;
		if (m_bOptimizationEnabled && pTextTail->fDistanceFromPlayer > m_fOptimizationMaxDistance)
		{
			IncrementEarlyDistanceCull();
			continue; // Skip expensive projection for far objects
		}
		UpdateTextTail(pTextTail);
	}

	// DX11 Model Sync M3-TEXTTAIL-29: Early distance culling for character texttails
	for (itor = m_CharacterTextTailList.begin(); itor != m_CharacterTextTailList.end(); ++itor)
	{
		TTextTail * pTextTail = *itor;
		if (m_bOptimizationEnabled && pTextTail->fDistanceFromPlayer > m_fOptimizationMaxDistance)
		{
			IncrementEarlyDistanceCull();
			continue; // Skip expensive projection for far objects
		}
		UpdateTextTail(pTextTail);

		// NOTE : Chat TextTail이 있을 경우 위치를 바꾼다.
		TChatTailMap::iterator itor = m_ChatTailMap.find(pTextTail->dwVirtualID);
		if (m_ChatTailMap.end() != itor)
		{
			TTextTail * pChatTail = itor->second;
			if (pChatTail->bNameFlag)
			{
				pTextTail->y = pChatTail->y - 17.0f;
			}
		}
	}
}

void CPythonTextTail::UpdateTextTail(TTextTail * pTextTail)
{
	if (!pTextTail->pOwner)
		return;

	/////

	CPythonGraphic & rpyGraphic = CPythonGraphic::Instance();
	rpyGraphic.Identity();

	const DirectX::SimpleMath::Vector3 & c_rv3Position = pTextTail->pOwner->GetPosition();

	// DX11 Model Sync M3-TEXTTAIL-29: Validate owner position before expensive projection
	if (!std::isfinite(c_rv3Position.x) || !std::isfinite(c_rv3Position.y) || !std::isfinite(c_rv3Position.z))
	{
		IncrementOwnerInvalidCull();
		pTextTail->x = -10000.0f;
		pTextTail->y = -10000.0f;
		pTextTail->z = 0.0f;
		return;
	}

	// DX11 Model Sync M3-TEXTTAIL-29: Track projection attempts for culling effectiveness stats
	IncrementProjectionAttempt();

	const bool bDX11Active =
		(CGraphicDeviceDX11::GetActiveDevice() && CGraphicDeviceDX11::GetActiveDevice()->IsValid());
	bool bUsedDX11WorldProjection = false;
	if (bDX11Active)
	{
		bUsedDX11WorldProjection = CGraphicBase::ProjectPositionDX11World(
			c_rv3Position.x,
			c_rv3Position.y,
			c_rv3Position.z + pTextTail->fHeight,
			&pTextTail->x,
			&pTextTail->y,
			&pTextTail->z);
	}

	if (!bUsedDX11WorldProjection)
	{
		rpyGraphic.ProjectPosition(c_rv3Position.x,
								   c_rv3Position.y,
								   c_rv3Position.z + pTextTail->fHeight,
								   &pTextTail->x,
								   &pTextTail->y,
								   &pTextTail->z);
	}

	{
		static DWORD s_dwDX11ProjectionSourceTick = 0u;
		static DWORD s_dwDX11WorldProjectionCount = 0u;
		static DWORD s_dwLegacyProjectionCount = 0u;
		if (bUsedDX11WorldProjection)
			++s_dwDX11WorldProjectionCount;
		else
			++s_dwLegacyProjectionCount;

		const DWORD dwNow = ELTimer_GetMSec();
		if (bDX11Active && (0u == s_dwDX11ProjectionSourceTick || (dwNow - s_dwDX11ProjectionSourceTick) >= 5000u))
		{
			s_dwDX11ProjectionSourceTick = dwNow;
			TraceError("DX11_TEXTTAIL_PROJECTION_SOURCE world_snapshot=%u legacy_current_state=%u",
				s_dwDX11WorldProjectionCount, s_dwLegacyProjectionCount);
			s_dwDX11WorldProjectionCount = 0u;
			s_dwLegacyProjectionCount = 0u;
		}
	}

	UINT uBackBufferWidth = 0u;
	UINT uBackBufferHeight = 0u;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	const float fGuardMarginX = static_cast<float>(uBackBufferWidth) * 2.0f;
	const float fGuardMarginY = static_cast<float>(uBackBufferHeight) * 2.0f;
	if (!std::isfinite(pTextTail->x) || !std::isfinite(pTextTail->y) || !std::isfinite(pTextTail->z) ||
		pTextTail->z < -0.05f || pTextTail->z > 1.05f ||
		pTextTail->x < -fGuardMarginX || pTextTail->x > (static_cast<float>(uBackBufferWidth) + fGuardMarginX) ||
		pTextTail->y < -fGuardMarginY || pTextTail->y > (static_cast<float>(uBackBufferHeight) + fGuardMarginY))
	{
		static DWORD s_dwProjectionRejectStatsTick = 0u;
		static DWORD s_dwProjectionRejectCount = 0u;
		static float s_fLastRejectX = 0.0f;
		static float s_fLastRejectY = 0.0f;
		static float s_fLastRejectZ = 0.0f;
		++s_dwProjectionRejectCount;
		s_fLastRejectX = pTextTail->x;
		s_fLastRejectY = pTextTail->y;
		s_fLastRejectZ = pTextTail->z;

		const DWORD dwNow = ELTimer_GetMSec();
		if (0u == s_dwProjectionRejectStatsTick || (dwNow - s_dwProjectionRejectStatsTick) >= 10000u)
		{
			s_dwProjectionRejectStatsTick = dwNow;
			TraceError("DX11_TEXTTAIL_PROJECTION_REJECT_STATS count=%u last_x=%.2f last_y=%.2f last_z=%.3f bb_w=%u bb_h=%u",
				s_dwProjectionRejectCount, s_fLastRejectX, s_fLastRejectY, s_fLastRejectZ, uBackBufferWidth, uBackBufferHeight);
			s_dwProjectionRejectCount = 0u;
		}
		// Keep tail out of the visible frame when projection is invalid.
		pTextTail->x = -10000.0f;
		pTextTail->y = -10000.0f;
		pTextTail->z = 0.0f;
		return;
	}

	pTextTail->x = floorf(pTextTail->x);
	pTextTail->y = floorf(pTextTail->y);

	// NOTE : 13m 밖에 있을때만 깊이를 넣습니다 - [levites]
	if (pTextTail->fDistanceFromPlayer < 1300.0f)
	{
		pTextTail->z = 0.0f;
	}
	else
	{
		pTextTail->z = pTextTail->z * CPythonGraphic::Instance().GetOrthoDepth() * -1.0f;
		pTextTail->z += 10.0f;
	}

	// DX11 UI/text path draws in clip space; large legacy z values can push
	// text tails outside visible clip range. Keep tails on UI plane in DX11.
	if (CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice())
	{
		if (pDX11Device->IsValid())
			pTextTail->z = 0.0f;
	}

	// DX11 Model Sync M3-TEXTTAIL22.A: Track z-plane projection for DX11 parity
	static DWORD s_dwZClampTelemetryTick = 0;
	static int s_iZClamped = 0;
	static int s_iZNotClamped = 0;
	const DWORD dwNow = ELTimer_GetMSec();

	if (pTextTail->z == 0.0f)
		++s_iZClamped;
	else
		++s_iZNotClamped;

	if (0 == s_dwZClampTelemetryTick || dwNow - s_dwZClampTelemetryTick >= 5000u)
	{
		s_dwZClampTelemetryTick = dwNow;
		CGraphicDeviceDX11* pDX11Device = CGraphicDeviceDX11::GetActiveDevice();

		// M2-UI-PARITY-FILTER-25: Check if ScreenFilter overlay is active during z-clamping
		bool hasRTBound = false;
		if (pDX11Device && pDX11Device->IsValid())
		{
			ID3D11DeviceContext* pContext = pDX11Device->GetContext();
			if (pContext)
			{
				ID3D11RenderTargetView* pCurrentRTV = nullptr;
				ID3D11DepthStencilView* pCurrentDSV = nullptr;
				pContext->OMGetRenderTargets(1, &pCurrentRTV, &pCurrentDSV);
				hasRTBound = (pCurrentRTV != nullptr);

				if (pCurrentRTV) pCurrentRTV->Release();
				if (pCurrentDSV) pCurrentDSV->Release();
			}
		}

		TraceError("DX11_TEXTTAIL_ZPROJ z_clamped=%d z_legacy=%d dx11_active=%d filter_rt_active=%d",
			s_iZClamped, s_iZNotClamped,
			(pDX11Device && pDX11Device->IsValid()) ? 1 : 0,
			hasRTBound ? 1 : 0);
		s_iZClamped = 0;
		s_iZNotClamped = 0;
	}
}

void CPythonTextTail::ArrangeTextTail()
{
	TTextTailList::iterator itor;
	TTextTailList::iterator itorCompare;
	const DWORD dwArrangeStart = ELTimer_GetMSec();
	m_dwLastCollisionChecks = 0;

	DWORD dwTime = CTimer::Instance().GetCurrentMillisecond();
	if (m_bOptimizationEnabled && m_dwArrangeIntervalMS > 0 && dwTime - m_dwLastArrangeTime < m_dwArrangeIntervalMS)
	{
		for (itor = m_ItemTextTailList.begin(); itor != m_ItemTextTailList.end(); ++itor)
		{
			TTextTail* pTextTail = *itor;
			if (pTextTail->pOwnerTextInstance)
			{
				pTextTail->pOwnerTextInstance->SetPosition(pTextTail->x, pTextTail->y, pTextTail->z);
				pTextTail->pOwnerTextInstance->Update();
				pTextTail->pTextInstance->SetColor(pTextTail->Color.r, pTextTail->Color.g, pTextTail->Color.b);
				pTextTail->pTextInstance->SetPosition(pTextTail->x, pTextTail->y + 15.0f, pTextTail->z);
				pTextTail->pTextInstance->Update();
			}
			else
			{
				pTextTail->pTextInstance->SetColor(pTextTail->Color.r, pTextTail->Color.g, pTextTail->Color.b);
				pTextTail->pTextInstance->SetPosition(pTextTail->x, pTextTail->y, pTextTail->z);
				pTextTail->pTextInstance->Update();
			}
		}

		for (itor = m_CharacterTextTailList.begin(); itor != m_CharacterTextTailList.end(); ++itor)
		{
			TTextTail* pTextTail = *itor;

			CGraphicMarkInstance* pMarkInstance = pTextTail->pMarkInstance;
			CGraphicTextInstance* pGuildNameInstance = pTextTail->pGuildNameTextInstance;
			if (pMarkInstance && pGuildNameInstance)
			{
				int iWidth, iHeight;
				int iImageHalfSize = pMarkInstance->GetWidth() / 2 + c_fxMarkPosition;
				pGuildNameInstance->GetTextSize(&iWidth, &iHeight);

				pMarkInstance->SetPosition(pTextTail->x - iWidth / 2 - iImageHalfSize, pTextTail->y - c_fyMarkPosition);
				pGuildNameInstance->SetPosition(pTextTail->x + iImageHalfSize, pTextTail->y - c_fyGuildNamePosition, pTextTail->z);
				pGuildNameInstance->Update();
			}

			int iNameWidth = 0;
			int iNameHeight = 0;
			pTextTail->pTextInstance->GetTextSize(&iNameWidth, &iNameHeight);

			const float fxAdd = 4.0f;
			float nameShift = 0.0f;
			if (pTextTail->pTitleTextInstance)
				nameShift = 1.0f;
			else if (pTextTail->pLevelTextInstance)
				nameShift = 5.0f;

			const float nameX = floorf((pTextTail->x + nameShift) + 0.5f);
			const float nameY = floorf(pTextTail->y + 0.5f);
			const float nameLeftEdge = pTextTail->x - (iNameWidth / 2.0f);
			float cursor = nameLeftEdge - fxAdd;

			cursor = TextTailBiDi(pTextTail->pTitleTextInstance, cursor, nameY, pTextTail->z, fxAdd, EPlaceDir::Left);
			cursor = TextTailBiDi(pTextTail->pLevelTextInstance, cursor, nameY, pTextTail->z, fxAdd, EPlaceDir::Left);

			pTextTail->pTextInstance->SetColor(pTextTail->Color.r, pTextTail->Color.g, pTextTail->Color.b);
			pTextTail->pTextInstance->SetPosition(nameX, nameY, pTextTail->z);
			pTextTail->pTextInstance->Update();
		}

		for (TChatTailMap::iterator itorChat = m_ChatTailMap.begin(); itorChat != m_ChatTailMap.end();)
		{
			TTextTail* pTextTail = itorChat->second;
			if (pTextTail->LivingTime < dwTime)
			{
				DeleteTextTail(pTextTail);
				itorChat = m_ChatTailMap.erase(itorChat);
				continue;
			}

			pTextTail->pTextInstance->SetColor(pTextTail->Color);
			pTextTail->pTextInstance->SetPosition(pTextTail->x, pTextTail->y, pTextTail->z);
			pTextTail->pTextInstance->Update();

			++itorChat;
		}
		m_dwLastArrangeMS = ELTimer_GetMSec() - dwArrangeStart;
		return;
	}
	m_dwLastArrangeTime = dwTime;

	if (m_bGridOptimizationEnabled)
	{
		static const float c_fCellWidth = 160.0f;
		static const float c_fCellHeight = 28.0f;
		std::map<unsigned __int64, std::vector<TTextTail*> > kCellMap;

		for (itor = m_ItemTextTailList.begin(); itor != m_ItemTextTailList.end(); ++itor)
		{
			TTextTail* pInsertTextTail = *itor;
			const int yTemp = 5;
			int iLimitCount = 0;

			while (iLimitCount < 20)
			{
				const float x1 = pInsertTextTail->x + pInsertTextTail->xStart;
				const float y1 = pInsertTextTail->y + pInsertTextTail->yStart;
				const float x2 = pInsertTextTail->x + pInsertTextTail->xEnd;
				const float y2 = pInsertTextTail->y + pInsertTextTail->yEnd;

				const int iMinCellX = static_cast<int>(floorf(x1 / c_fCellWidth));
				const int iMaxCellX = static_cast<int>(floorf(x2 / c_fCellWidth));
				const int iMinCellY = static_cast<int>(floorf(y1 / c_fCellHeight));
				const int iMaxCellY = static_cast<int>(floorf(y2 / c_fCellHeight));

				bool bAdjusted = false;
				for (int iCellY = iMinCellY - 1; iCellY <= iMaxCellY + 1 && !bAdjusted; ++iCellY)
				{
					for (int iCellX = iMinCellX - 1; iCellX <= iMaxCellX + 1 && !bAdjusted; ++iCellX)
					{
						const unsigned __int64 ullKey =
							(static_cast<unsigned __int64>(static_cast<DWORD>(iCellX)) << 32) |
							static_cast<DWORD>(iCellY);

						std::map<unsigned __int64, std::vector<TTextTail*> >::iterator itCell = kCellMap.find(ullKey);
						if (itCell == kCellMap.end())
							continue;

						std::vector<TTextTail*>& rCellList = itCell->second;
						for (std::vector<TTextTail*>::iterator itCellTail = rCellList.begin(); itCellTail != rCellList.end(); ++itCellTail)
						{
							TTextTail* pCompareTextTail = *itCellTail;
							if (pCompareTextTail == pInsertTextTail)
								continue;

							++m_dwLastCollisionChecks;
							if (isIn(pInsertTextTail, pCompareTextTail))
							{
								pInsertTextTail->y = pCompareTextTail->y + pCompareTextTail->yEnd + yTemp;
								bAdjusted = true;
								break;
							}
						}
					}
				}

				if (!bAdjusted)
					break;

				++iLimitCount;
			}

			const float x1 = pInsertTextTail->x + pInsertTextTail->xStart;
			const float y1 = pInsertTextTail->y + pInsertTextTail->yStart;
			const float x2 = pInsertTextTail->x + pInsertTextTail->xEnd;
			const float y2 = pInsertTextTail->y + pInsertTextTail->yEnd;
			const int iMinCellX = static_cast<int>(floorf(x1 / c_fCellWidth));
			const int iMaxCellX = static_cast<int>(floorf(x2 / c_fCellWidth));
			const int iMinCellY = static_cast<int>(floorf(y1 / c_fCellHeight));
			const int iMaxCellY = static_cast<int>(floorf(y2 / c_fCellHeight));

			for (int iCellY = iMinCellY; iCellY <= iMaxCellY; ++iCellY)
			{
				for (int iCellX = iMinCellX; iCellX <= iMaxCellX; ++iCellX)
				{
					const unsigned __int64 ullKey =
						(static_cast<unsigned __int64>(static_cast<DWORD>(iCellX)) << 32) |
						static_cast<DWORD>(iCellY);
					kCellMap[ullKey].push_back(pInsertTextTail);
				}
			}

			if (pInsertTextTail->pOwnerTextInstance)
			{
				pInsertTextTail->pOwnerTextInstance->SetPosition(pInsertTextTail->x, pInsertTextTail->y, pInsertTextTail->z);
				pInsertTextTail->pOwnerTextInstance->Update();

				pInsertTextTail->pTextInstance->SetColor(pInsertTextTail->Color.r, pInsertTextTail->Color.g, pInsertTextTail->Color.b);
				pInsertTextTail->pTextInstance->SetPosition(pInsertTextTail->x, pInsertTextTail->y + 15.0f, pInsertTextTail->z);
				pInsertTextTail->pTextInstance->Update();
			}
			else
			{
				pInsertTextTail->pTextInstance->SetColor(pInsertTextTail->Color.r, pInsertTextTail->Color.g, pInsertTextTail->Color.b);
				pInsertTextTail->pTextInstance->SetPosition(pInsertTextTail->x, pInsertTextTail->y, pInsertTextTail->z);
				pInsertTextTail->pTextInstance->Update();
			}
		}
	}
	else
	{
		for (itor = m_ItemTextTailList.begin(); itor != m_ItemTextTailList.end(); ++itor)
		{
			TTextTail* pInsertTextTail = *itor;

			const int yTemp = 5;
			int iLimitCount = 0;

			for (itorCompare = m_ItemTextTailList.begin(); itorCompare != m_ItemTextTailList.end();)
			{
				TTextTail* pCompareTextTail = *itorCompare;

				if (*itorCompare == *itor)
				{
					++itorCompare;
					continue;
				}

				if (iLimitCount >= 20)
					break;

				++m_dwLastCollisionChecks;
				if (isIn(pInsertTextTail, pCompareTextTail))
				{
					pInsertTextTail->y = pCompareTextTail->y + pCompareTextTail->yEnd + yTemp;
					itorCompare = m_ItemTextTailList.begin();
					++iLimitCount;
					continue;
				}

				++itorCompare;
			}

			if (pInsertTextTail->pOwnerTextInstance)
			{
				pInsertTextTail->pOwnerTextInstance->SetPosition(pInsertTextTail->x, pInsertTextTail->y, pInsertTextTail->z);
				pInsertTextTail->pOwnerTextInstance->Update();

				pInsertTextTail->pTextInstance->SetColor(pInsertTextTail->Color.r, pInsertTextTail->Color.g, pInsertTextTail->Color.b);
				pInsertTextTail->pTextInstance->SetPosition(pInsertTextTail->x, pInsertTextTail->y + 15.0f, pInsertTextTail->z);
				pInsertTextTail->pTextInstance->Update();
			}
			else
			{
				pInsertTextTail->pTextInstance->SetColor(pInsertTextTail->Color.r, pInsertTextTail->Color.g, pInsertTextTail->Color.b);
				pInsertTextTail->pTextInstance->SetPosition(pInsertTextTail->x, pInsertTextTail->y, pInsertTextTail->z);
				pInsertTextTail->pTextInstance->Update();
			}
		}
	}

	for (itor = m_CharacterTextTailList.begin(); itor != m_CharacterTextTailList.end(); ++itor)
	{
		TTextTail* pTextTail = *itor;

		CGraphicMarkInstance* pMarkInstance = pTextTail->pMarkInstance;
		CGraphicTextInstance* pGuildNameInstance = pTextTail->pGuildNameTextInstance;
		if (pMarkInstance && pGuildNameInstance)
		{
			int iWidth, iHeight;
			int iImageHalfSize = pMarkInstance->GetWidth() / 2 + c_fxMarkPosition;
			pGuildNameInstance->GetTextSize(&iWidth, &iHeight);

			pMarkInstance->SetPosition(pTextTail->x - iWidth / 2 - iImageHalfSize, pTextTail->y - c_fyMarkPosition);
			pGuildNameInstance->SetPosition(pTextTail->x + iImageHalfSize, pTextTail->y - c_fyGuildNamePosition, pTextTail->z);
			pGuildNameInstance->Update();
		}

		// Title & Name refactor
		int iNameWidth = 0, iNameHeight = 0;
		pTextTail->pTextInstance->GetTextSize(&iNameWidth, &iNameHeight);

		const float fxAdd = 4.0f;

		float nameShift = 0.0f;
		if (pTextTail->pTitleTextInstance)
			nameShift = 1.0f;
		else if (pTextTail->pLevelTextInstance)
			nameShift = 5.0f;

		float nameX = floorf((pTextTail->x + nameShift) + 0.5f);
		float nameY = floorf(pTextTail->y + 0.5f);

		const float nameLeftEdge = pTextTail->x - (iNameWidth / 2.0f);
		float cursor = nameLeftEdge - fxAdd;

		// [Level] [Title] Name - LTR.
		// It can be set for RTL - Name [Title] [Level] by adding the required check.
		cursor = TextTailBiDi(pTextTail->pTitleTextInstance, cursor, nameY, pTextTail->z, fxAdd, EPlaceDir::Left);
		cursor = TextTailBiDi(pTextTail->pLevelTextInstance, cursor, nameY, pTextTail->z, fxAdd, EPlaceDir::Left);

		pTextTail->pTextInstance->SetColor(pTextTail->Color.r, pTextTail->Color.g, pTextTail->Color.b);
		pTextTail->pTextInstance->SetPosition(nameX, nameY, pTextTail->z);
		pTextTail->pTextInstance->Update();
	}

	for (TChatTailMap::iterator itorChat=m_ChatTailMap.begin(); itorChat!=m_ChatTailMap.end();)
	{
		TTextTail * pTextTail = itorChat->second;

		if (pTextTail->LivingTime < dwTime)
		{
			DeleteTextTail(pTextTail);
			itorChat = m_ChatTailMap.erase(itorChat);
			continue;
		}
		else
			++itorChat;

		pTextTail->pTextInstance->SetColor(pTextTail->Color);
		pTextTail->pTextInstance->SetPosition(pTextTail->x, pTextTail->y, pTextTail->z);
		pTextTail->pTextInstance->Update();
	}

	m_dwLastArrangeMS = ELTimer_GetMSec() - dwArrangeStart;
}

void CPythonTextTail::Render()
{
	const DWORD dwRenderStart = ELTimer_GetMSec();
	const int iRenderLimit = m_bOptimizationEnabled ? m_iMaxRenderCount : 1000000;
	int iRendered = 0;
	const DWORD dwTargetVID = CPythonPlayer::Instance().GetTargetVID();

	// DX11 Model Sync M3-TEXTTAIL22.A: Track visibility funnel (submitted → culled → throttled → rendered)
	static int s_iDistanceCulled = 0;
	static int s_iLimitThrottled = 0;

	// M2-UIPY-40: Enhanced visibility diagnostics
	static int s_iNotEmitted = 0;  // Failed visibility checks in ShowCharacterTextTail

	// DX11 Model Sync M3-TEXTTAIL-PARITY-25: Track screen-space placement buckets
	static int s_iOnscreen = 0;
	static int s_iEdge = 0;
	static int s_iOffscreen = 0;
	static int s_iAlphaZero = 0;
	static int s_iOffscreenClip = 0;  // M2-UIPY-40: Tails clipped by scissor rect
	static DWORD s_dwScreenBucketTelemetryTick = 0;

	std::vector<TTextTail*> kCharacterRenderList;
	kCharacterRenderList.reserve(m_CharacterTextTailList.size());
	for (TTextTailList::iterator itor = m_CharacterTextTailList.begin(); itor != m_CharacterTextTailList.end(); ++itor)
		kCharacterRenderList.push_back(*itor);

	// M2-UIPY-40: Snapshot tails not emitted into render list this frame.
	// Keep this as a per-frame value (not cumulative), otherwise telemetry quickly
	// explodes with frame count and becomes hard to correlate with runtime behavior.
	const int iNotEmittedCurrent = std::max(
		0,
		static_cast<int>(m_CharacterTextTailMap.size()) - static_cast<int>(m_CharacterTextTailList.size()));
	s_iNotEmitted = iNotEmittedCurrent;

	if (m_bOptimizationEnabled)
	{
		struct FTextTailPriority
		{
			DWORD dwTargetVID;
			bool operator() (TTextTail* pkLeft, TTextTail* pkRight) const
			{
				int iLeftPriority = 2;
				int iRightPriority = 2;

				if (pkLeft->dwVirtualID == dwTargetVID)
					iLeftPriority = 0;
				else
				{
					CInstanceBase* pLeftInstance = CPythonCharacterManager::Instance().GetInstancePtr(pkLeft->dwVirtualID);
					if (pLeftInstance && pLeftInstance->IsPartyMember())
						iLeftPriority = 1;
				}

				if (pkRight->dwVirtualID == dwTargetVID)
					iRightPriority = 0;
				else
				{
					CInstanceBase* pRightInstance = CPythonCharacterManager::Instance().GetInstancePtr(pkRight->dwVirtualID);
					if (pRightInstance && pRightInstance->IsPartyMember())
						iRightPriority = 1;
				}

				if (iLeftPriority != iRightPriority)
					return iLeftPriority < iRightPriority;

				return pkLeft->fDistanceFromPlayer < pkRight->fDistanceFromPlayer;
			}
		};

		FTextTailPriority kSorter;
		kSorter.dwTargetVID = dwTargetVID;
		std::stable_sort(kCharacterRenderList.begin(), kCharacterRenderList.end(), kSorter);
	}

	// M3-TEXTTAIL-PARITY-25: Get screen dimensions for bucket categorization
	UINT uBackBufferWidth = 0u;
	UINT uBackBufferHeight = 0u;
	CGraphicBase::GetBackBufferSize(&uBackBufferWidth, &uBackBufferHeight);
	const float fEdgeMargin = 100.0f;

	for (std::vector<TTextTail*>::iterator it = kCharacterRenderList.begin(); it != kCharacterRenderList.end(); ++it)
	{
		if (iRendered >= iRenderLimit)
		{
			++s_iLimitThrottled; // M3-TEXTTAIL22.A: Track limit throttling
			break;
		}

		TTextTail* pTextTail = *it;
		if (m_bOptimizationEnabled && pTextTail->fDistanceFromPlayer > m_fOptimizationMaxDistance)
		{
			++s_iDistanceCulled; // M3-TEXTTAIL22.A: Track distance culling
			continue;
		}

		// M3-TEXTTAIL-PARITY-25: Categorize screen-space bucket
		const float x = pTextTail->x;
		const float y = pTextTail->y;
		const float alpha = pTextTail->Color.a;

		if (alpha <= 0.01f)
		{
			++s_iAlphaZero;
		}
		else if (x >= 0.0f && x <= static_cast<float>(uBackBufferWidth) &&
		         y >= 0.0f && y <= static_cast<float>(uBackBufferHeight))
		{
			if (x < fEdgeMargin || x > (static_cast<float>(uBackBufferWidth) - fEdgeMargin) ||
			    y < fEdgeMargin || y > (static_cast<float>(uBackBufferHeight) - fEdgeMargin))
			{
				++s_iEdge;
			}
			else
			{
				++s_iOnscreen;
			}
		}
		else
		{
			++s_iOffscreen;
		}

		pTextTail->pTextInstance->Render();
		if (pTextTail->pMarkInstance && pTextTail->pGuildNameTextInstance)
		{
			pTextTail->pMarkInstance->Render();
			pTextTail->pGuildNameTextInstance->Render();
		}
		if (pTextTail->pTitleTextInstance)
			pTextTail->pTitleTextInstance->Render();
		if (pTextTail->pLevelTextInstance)
			pTextTail->pLevelTextInstance->Render();

		++iRendered;
	}

	std::vector<TTextTail*> kItemRenderList;
	kItemRenderList.reserve(m_ItemTextTailList.size());
	for (TTextTailList::iterator itor = m_ItemTextTailList.begin(); itor != m_ItemTextTailList.end(); ++itor)
		kItemRenderList.push_back(*itor);

	if (m_bOptimizationEnabled)
	{
		struct FItemTextTailDistance
		{
			bool operator() (TTextTail* pkLeft, TTextTail* pkRight) const
			{
				return pkLeft->fDistanceFromPlayer < pkRight->fDistanceFromPlayer;
			}
		};

		std::stable_sort(kItemRenderList.begin(), kItemRenderList.end(), FItemTextTailDistance());
	}

	for (std::vector<TTextTail*>::iterator it = kItemRenderList.begin(); it != kItemRenderList.end(); ++it)
	{
		if (iRendered >= iRenderLimit)
		{
			++s_iLimitThrottled; // M3-TEXTTAIL22.A: Track limit throttling
			break;
		}

		TTextTail* pTextTail = *it;
		if (m_bOptimizationEnabled && pTextTail->fDistanceFromPlayer > m_fOptimizationMaxDistance)
		{
			++s_iDistanceCulled; // M3-TEXTTAIL22.A: Track distance culling
			continue;
		}

		// M3-TEXTTAIL-PARITY-25: Categorize screen-space bucket
		const float x = pTextTail->x;
		const float y = pTextTail->y;
		const float alpha = pTextTail->Color.a;

		if (alpha <= 0.01f)
		{
			++s_iAlphaZero;
		}
		else if (x >= 0.0f && x <= static_cast<float>(uBackBufferWidth) &&
		         y >= 0.0f && y <= static_cast<float>(uBackBufferHeight))
		{
			if (x < fEdgeMargin || x > (static_cast<float>(uBackBufferWidth) - fEdgeMargin) ||
			    y < fEdgeMargin || y > (static_cast<float>(uBackBufferHeight) - fEdgeMargin))
			{
				++s_iEdge;
			}
			else
			{
				++s_iOnscreen;
			}
		}
		else
		{
			++s_iOffscreen;
		}

		RenderTextTailBox(pTextTail);
		pTextTail->pTextInstance->Render();
		if (pTextTail->pOwnerTextInstance)
			pTextTail->pOwnerTextInstance->Render();

		++iRendered;
	}

	for (TChatTailMap::iterator itorChat = m_ChatTailMap.begin(); itorChat != m_ChatTailMap.end(); ++itorChat)
	{
		if (iRendered >= iRenderLimit)
		{
			++s_iLimitThrottled; // M3-TEXTTAIL22.A: Track limit throttling
			break;
		}

		TTextTail* pTextTail = itorChat->second;
		if (m_bOptimizationEnabled && pTextTail->fDistanceFromPlayer > m_fOptimizationMaxDistance)
		{
			++s_iDistanceCulled; // M3-TEXTTAIL22.A: Track distance culling
			continue;
		}
		const bool bDX11Active =
			(CGraphicDeviceDX11::GetActiveDevice() && CGraphicDeviceDX11::GetActiveDevice()->IsValid());
		if (bDX11Active || pTextTail->pOwner->isShow())
		{
			// M3-TEXTTAIL-PARITY-25: Categorize screen-space bucket
			const float x = pTextTail->x;
			const float y = pTextTail->y;
			const float alpha = pTextTail->Color.a;

			if (alpha <= 0.01f)
			{
				++s_iAlphaZero;
			}
			else if (x >= 0.0f && x <= static_cast<float>(uBackBufferWidth) &&
			         y >= 0.0f && y <= static_cast<float>(uBackBufferHeight))
			{
				if (x < fEdgeMargin || x > (static_cast<float>(uBackBufferWidth) - fEdgeMargin) ||
				    y < fEdgeMargin || y > (static_cast<float>(uBackBufferHeight) - fEdgeMargin))
				{
					++s_iEdge;
				}
				else
				{
					++s_iOnscreen;
				}
			}
			else
			{
				++s_iOffscreen;
			}

			RenderTextTailName(pTextTail);
			++iRendered;
		}
	}

	m_dwLastRenderMS = ELTimer_GetMSec() - dwRenderStart;

	// DX11 Model Sync M3-TEXTTAIL22.A: Enhanced telemetry - visibility funnel breakdown
	static DWORD s_dwTextTailTelemetryTick = 0;
	const DWORD dwNow = ELTimer_GetMSec();
	if (0 == s_dwTextTailTelemetryTick || dwNow - s_dwTextTailTelemetryTick >= 2500u)
	{
		s_dwTextTailTelemetryTick = dwNow;
		const int iTotalSubmitted = static_cast<int>(kCharacterRenderList.size() + kItemRenderList.size() + m_ChatTailMap.size());
		TraceError(
			"DX11_TEXTTAIL_RENDER submitted=%d distance_culled=%d limit_throttled=%d rendered=%d "
			"opt=%d range=%.1f limit=%d",
			iTotalSubmitted,
			s_iDistanceCulled,
			s_iLimitThrottled,
			iRendered,
			m_bOptimizationEnabled ? 1 : 0,
			m_fOptimizationMaxDistance,
			iRenderLimit);

		// M3-TEXTTAIL-PARITY-25: Screen-space placement bucket telemetry
		TraceError(
			"DX11_TEXTTAIL_SCREEN_BUCKET onscreen=%d edge=%d offscreen=%d alpha_zero=%d",
			s_iOnscreen,
			s_iEdge,
			s_iOffscreen,
			s_iAlphaZero);

		// M2-UIPY-40: Enhanced visibility diagnostics
		TraceError(
			"DX11_TEXTTAIL_VISIBILITY_DETAIL not_emitted=%d state_blocked=%d offscreen_clip=%d",
			s_iNotEmitted,
			g_iStateBlocked,  // Use global counter
			s_iOffscreenClip);

		// M2-UI-TEXTTAIL-DX11-72: Parity log - expected vs rendered vs rejected
		//
		// NOTE:
		// "Onscreen/edge/offscreen/alpha_zero" are telemetry buckets, not rejection
		// criteria. A texttail that survives the actual rejection funnel still renders
		// even if it lands outside the viewport or has zero alpha input data.
		//
		// To keep parity aligned with the real runtime path, expected must represent
		// every submitted tail that was not actually rejected in Render() loops.
		// Rejections from pre-list visibility filters (not_emitted/state_blocked) are
		// tracked separately and must not be subtracted from submitted tails.
		const int iRejectedRaw = s_iDistanceCulled + s_iLimitThrottled + s_iOffscreenClip;
		const int iRejected = (iRejectedRaw > iTotalSubmitted) ? iTotalSubmitted : iRejectedRaw;
		const int iExpected = iTotalSubmitted - iRejected;
		if (iRejectedRaw > iTotalSubmitted)
		{
			static DWORD s_dwTextTailParityClampTick = 0u;
			if (0u == s_dwTextTailParityClampTick || (dwNow - s_dwTextTailParityClampTick) >= 5000u)
			{
				s_dwTextTailParityClampTick = dwNow;
				TraceError(
					"DX11_TEXTTAIL_PARITY_CLAMP submitted=%d rejected_raw=%d rejected_clamped=%d",
					iTotalSubmitted,
					iRejectedRaw,
					iRejected);
			}
		}
		TraceError(
			"DX11_TEXTTAIL_PARITY expected=%d rendered=%d rejected=%d accept_rate=%.1f%%",
			iExpected,
			iRendered,
			iRejected,
			(iExpected > 0) ? (100.0f * iRendered / iExpected) : 0.0f);

		// M3-TEXTTAIL-PARITY-25: Visibility anomaly detection
		const int iTotalVisible = s_iOnscreen + s_iEdge;
		if (iRendered > 0 && iTotalVisible == 0)
		{
			TraceError(
				"DX11_TEXTTAIL_VISIBILITY_ANOMALY rendered=%d visible_buckets=0 offscreen=%d alpha_zero=%d",
				iRendered,
				s_iOffscreen,
				s_iAlphaZero);
		}

		// M2-UIWORLD-33: One-shot cull reason logging when submitted>0 but rendered=0
		if (iTotalSubmitted > 0 && iRendered == 0)
		{
			static bool s_bLoggedZeroRender = false;
			if (!s_bLoggedZeroRender)
			{
				s_bLoggedZeroRender = true;
				// Determine primary cull reason
				const char* c_szReason = "unknown";
				if (s_iDistanceCulled > 0)
					c_szReason = "distance_cull";
				else if (s_iAlphaZero > iTotalSubmitted / 2)
					c_szReason = "alpha_zero";
				else if (s_iOffscreen > iTotalSubmitted / 2)
					c_szReason = "offscreen";
				else if (s_iLimitThrottled > 0)
					c_szReason = "limit_throttle";
				else if (s_iOnscreen == 0 && s_iEdge == 0 && s_iOffscreen == 0 && s_iAlphaZero == 0)
					c_szReason = "all_bucket_unknown";

				TraceError(
					"DX11_TEXTTAIL_ZERO_RENDER submitted=%d distance_culled=%d limit_throttled=%d "
					"onscreen=%d edge=%d offscreen=%d alpha_zero=%d reason=%s",
					iTotalSubmitted,
					s_iDistanceCulled,
					s_iLimitThrottled,
					s_iOnscreen,
					s_iEdge,
					s_iOffscreen,
					s_iAlphaZero,
					c_szReason);
			}
		}

		s_iDistanceCulled = 0;
		s_iLimitThrottled = 0;
		s_iOnscreen = 0;
		s_iEdge = 0;
		s_iOffscreen = 0;
		s_iAlphaZero = 0;
		s_iNotEmitted = 0;
		g_iStateBlocked = 0;  // Reset global counter
		s_iOffscreenClip = 0;
	}
}

void CPythonTextTail::RenderTextTailBox(TTextTail * pTextTail)
{
	// 검은색 테두리
	CPythonGraphic::Instance().SetDiffuseColor(0.0f, 0.0f, 0.0f, 1.0f);
	CPythonGraphic::Instance().RenderBox2d(pTextTail->x + pTextTail->xStart,
										   pTextTail->y + pTextTail->yStart,
										   pTextTail->x + pTextTail->xEnd,
										   pTextTail->y + pTextTail->yEnd,
										   pTextTail->z);

	// 검은색 투명박스
	CPythonGraphic::Instance().SetDiffuseColor(0.0f, 0.0f, 0.0f, 0.3f);
	CPythonGraphic::Instance().RenderBar2d(pTextTail->x + pTextTail->xStart,
										   pTextTail->y + pTextTail->yStart,
										   pTextTail->x + pTextTail->xEnd,
										   pTextTail->y + pTextTail->yEnd,
										   pTextTail->z);
}

void CPythonTextTail::RenderTextTailName(TTextTail * pTextTail)
{
	pTextTail->pTextInstance->Render();
}

void CPythonTextTail::HideAllTextTail()
{
	// NOTE : Show All을 해준뒤 Hide All을 해주지 않으면 문제 발생 가능성 있음
	//        디자인 자체가 그렇게 깔끔하게 되지 않았음 - [levites]
	m_CharacterTextTailList.clear();
	m_ItemTextTailList.clear();
}

void CPythonTextTail::UpdateDistance(const TPixelPosition & c_rCenterPosition, TTextTail * pTextTail)
{
	const DirectX::SimpleMath::Vector3 & c_rv3Position = pTextTail->pOwner->GetPosition();
	DirectX::SimpleMath::Vector2 v2Distance(c_rv3Position.x - c_rCenterPosition.x, -c_rv3Position.y - c_rCenterPosition.y);
	pTextTail->fDistanceFromPlayer = D3DXVec2Length(&v2Distance);
}

void CPythonTextTail::ShowAllTextTail()
{
	const float fShowDistance = (m_bOptimizationEnabled ? m_fOptimizationMaxDistance : DX11RuntimeConfig::kTextTailDefaultMaxDistance);

	TTextTailMap::iterator itor;
	for (itor = m_CharacterTextTailMap.begin(); itor != m_CharacterTextTailMap.end(); ++itor)
	{
		TTextTail * pTextTail = itor->second;
		if (pTextTail->fDistanceFromPlayer < fShowDistance)
			ShowCharacterTextTail(itor->first);
	}
	for (itor = m_ItemTextTailMap.begin(); itor != m_ItemTextTailMap.end(); ++itor)
	{
		TTextTail * pTextTail = itor->second;
		if (pTextTail->fDistanceFromPlayer < fShowDistance)
			ShowItemTextTail(itor->first);
	}
}

void CPythonTextTail::ShowCharacterTextTail(DWORD VirtualID)
{
	TTextTailMap::iterator itor = m_CharacterTextTailMap.find(VirtualID);

	if (m_CharacterTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;

	if (m_CharacterTextTailList.end() != std::find(m_CharacterTextTailList.begin(), m_CharacterTextTailList.end(), pTextTail))
	{
		//Tracef("이미 리스트에 있음 : %d\n", VirtualID);
		return;
	}

	// DX11 Model Sync M3-TEXTTAIL22.A + M3-TEXTTAIL-24: Track visibility filter reasons
	static DWORD s_dwVisibilityTelemetryTick = 0;
	static int s_iPassedAll = 0;
	static int s_iFailedOwnerNotShow = 0;
	static int s_iFailedGuildWall = 0;
	static int s_iFailedCannotPick = 0;
	static int s_iFailedNoInstance = 0;
	const DWORD dwNow = ELTimer_GetMSec();

	// NOTE : ShowAll 시에는 모든 Instance 의 Pointer 를 찾아서 체크하므로 부하가 걸릴 가능성도 있다.
	//        CInstanceBase 가 TextTail 을 직접 가지고 있는 것이 가장 좋은 형태일 듯..

	// DX11 Model Sync M3-TEXTTAIL-24: Removed owner->isShow() check
	// Previous implementation checked view frustum culling state (m_isVisible set by CullingManager),
	// which rejected entities slightly off-screen or aggressively culled by DX11.
	// This caused excessive fail_not_show (7,500-14,000 per 10s) for valid texttails.
	// Texttails have independent distance culling (m_fOptimizationMaxDistance) and
	// projection validation (UpdateTextTail lines 217-235), so owner render state is not needed.
	//
	// if (!pTextTail->pOwner->isShow())
	// {
	//     ++s_iFailedOwnerNotShow;
	//     return;
	// }

	CInstanceBase * pInstance = CPythonCharacterManager::Instance().GetInstancePtr(pTextTail->dwVirtualID);
	if (!pInstance)
	{
		++s_iFailedNoInstance;
		++g_iStateBlocked;  // M2-UIPY-40: Track all state blocks globally

		// M2-UI-PARITY-FILTER-25: Check if ScreenFilter overlay is active when tail blocked
		static DWORD s_dwLastOverlayBlockLog = 0;
		const DWORD dwNow = ELTimer_GetMSec();
		if (dwNow - s_dwLastOverlayBlockLog >= 15000)
		{
			s_dwLastOverlayBlockLog = dwNow;

			// Query DX11 device for overlay state
			CGraphicDeviceDX11* pDX11 = CGraphicDeviceDX11::GetActiveDevice();
			ID3D11RenderTargetView* pCurrentRTV = nullptr;
			ID3D11DepthStencilView* pCurrentDSV = nullptr;
			bool hasRTBound = false;

			if (pDX11 && pDX11->IsValid())
			{
				ID3D11DeviceContext* pContext = pDX11->GetContext();
				if (pContext)
				{
					pContext->OMGetRenderTargets(1, &pCurrentRTV, &pCurrentDSV);
					hasRTBound = (pCurrentRTV != nullptr);

					if (pCurrentRTV) pCurrentRTV->Release();
					if (pCurrentDSV) pCurrentDSV->Release();
				}
			}

			TraceError("DX11_TEXTTAIL_FILTER_PARITY tail_blocked_no_instance has_rt_bound=%d virtual_id=%u",
				hasRTBound ? 1 : 0, pTextTail->dwVirtualID);
		}
		return;
	}

	if (pInstance->IsGuildWall())
	{
		++s_iFailedGuildWall;
		++g_iStateBlocked;  // M2-UIPY-40: Track all state blocks globally
		return;
	}

	// M2-UI-TEXTTAIL-PARITY-26: Texttail visibility check passed
	// Note: Atlas readiness is handled internally by CGraphicTextInstance::Render()
	// via the startup guard (__IsDX11TextStartupGuardActive) which was extended to 5000ms
	// and resets on phase changes (CreateDeviceObjects callback).
	++s_iPassedAll;
	m_CharacterTextTailList.push_back(pTextTail);

	if (0 == s_dwVisibilityTelemetryTick || dwNow - s_dwVisibilityTelemetryTick >= 10000u)
	{
		s_dwVisibilityTelemetryTick = dwNow;
		TraceError("DX11_TEXTTAIL_VISIBILITY passed=%d fail_not_show=%d fail_no_instance=%d fail_guild_wall=%d fail_cannot_pick=%d",
			s_iPassedAll, s_iFailedOwnerNotShow, s_iFailedNoInstance, s_iFailedGuildWall, s_iFailedCannotPick);
		s_iPassedAll = 0;
		s_iFailedOwnerNotShow = 0;
		s_iFailedNoInstance = 0;
		s_iFailedGuildWall = 0;
		s_iFailedCannotPick = 0;
	}
}

void CPythonTextTail::ShowItemTextTail(DWORD VirtualID)
{
	TTextTailMap::iterator itor = m_ItemTextTailMap.find(VirtualID);

	if (m_ItemTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;

	if (m_ItemTextTailList.end() != std::find(m_ItemTextTailList.begin(), m_ItemTextTailList.end(), pTextTail))
	{
		//Tracef("이미 리스트에 있음 : %d\n", VirtualID);
		return;
	}

	m_ItemTextTailList.push_back(pTextTail);
}

bool CPythonTextTail::isIn(CPythonTextTail::TTextTail * pSource, CPythonTextTail::TTextTail * pTarget)
{
	float x1Source = pSource->x + pSource->xStart;
	float y1Source = pSource->y + pSource->yStart;
	float x2Source = pSource->x + pSource->xEnd;
	float y2Source = pSource->y + pSource->yEnd;
	float x1Target = pTarget->x + pTarget->xStart;
	float y1Target = pTarget->y + pTarget->yStart;
	float x2Target = pTarget->x + pTarget->xEnd;
	float y2Target = pTarget->y + pTarget->yEnd;

	if (x1Source <= x2Target && x2Source >= x1Target &&
	    y1Source <= y2Target && y2Source >= y1Target)
	{
		return true;
	}

	return false;
}

void CPythonTextTail::RegisterCharacterTextTail(DWORD dwGuildID, DWORD dwVirtualID, const D3DXCOLOR & c_rColor, float fAddHeight)
{
	CInstanceBase * pCharacterInstance = CPythonCharacterManager::Instance().GetInstancePtr(dwVirtualID);

	if (!pCharacterInstance)
		return;

	TTextTail * pTextTail = RegisterTextTail(dwVirtualID,
											 pCharacterInstance->GetNameString(),
											 pCharacterInstance->GetGraphicThingInstancePtr(),
											 pCharacterInstance->GetGraphicThingInstanceRef().GetHeight() + fAddHeight,
											 c_rColor);

	CGraphicTextInstance * pTextInstance = pTextTail->pTextInstance;
	pTextInstance->SetOutline(true);
	pTextInstance->SetVerticalAlign(CGraphicTextInstance::VERTICAL_ALIGN_BOTTOM);

	pTextTail->pMarkInstance=NULL;	
	pTextTail->pGuildNameTextInstance=NULL;
	pTextTail->pTitleTextInstance=NULL;
	pTextTail->pLevelTextInstance=NULL;

	if (0 != dwGuildID)
	{
		pTextTail->pMarkInstance = CGraphicMarkInstance::New();

		DWORD dwMarkID = CGuildMarkManager::Instance().GetMarkID(dwGuildID);

		if (dwMarkID != CGuildMarkManager::INVALID_MARK_ID)
		{
			std::string markImagePath;

			if (CGuildMarkManager::Instance().GetMarkImageFilename(dwMarkID / CGuildMarkImage::MARK_TOTAL_COUNT, markImagePath))
			{
				CPackManager::instance().SetFileLoadMode();
				pTextTail->pMarkInstance->SetImageFileName(markImagePath.c_str());
				pTextTail->pMarkInstance->Load();
				pTextTail->pMarkInstance->SetIndex(dwMarkID % CGuildMarkImage::MARK_TOTAL_COUNT);
				CPackManager::instance().SetPackLoadMode();
			}
		}

		std::string strGuildName;
		if (!CPythonGuild::Instance().GetGuildName(dwGuildID, &strGuildName))
			strGuildName = "Noname";

		CGraphicTextInstance *& prGuildNameInstance = pTextTail->pGuildNameTextInstance;
		prGuildNameInstance = CGraphicTextInstance::New();
		prGuildNameInstance->SetTextPointer(ms_pFont);
		prGuildNameInstance->SetOutline(true);
		prGuildNameInstance->SetHorizonalAlign(CGraphicTextInstance::HORIZONTAL_ALIGN_CENTER);
		prGuildNameInstance->SetVerticalAlign(CGraphicTextInstance::VERTICAL_ALIGN_BOTTOM);
		prGuildNameInstance->SetValue(strGuildName.c_str());
		prGuildNameInstance->SetColor(c_TextTail_Guild_Name_Color.r, c_TextTail_Guild_Name_Color.g, c_TextTail_Guild_Name_Color.b);
		prGuildNameInstance->Update();
	}

	m_CharacterTextTailMap.insert(TTextTailMap::value_type(dwVirtualID, pTextTail));
}

void CPythonTextTail::RegisterItemTextTail(DWORD VirtualID, const char * c_szText, CGraphicObjectInstance * pOwner)
{
#ifdef __DEBUG
	char szName[256];
	spritnf(szName, "%s[%d]", c_szText, VirtualID);

	TTextTail * pTextTail = RegisterTextTail(VirtualID, c_szText, pOwner, c_TextTail_Name_Position, c_TextTail_Item_Color);
	m_ItemTextTailMap.insert(TTextTailMap::value_type(VirtualID, pTextTail));
#else
	TTextTail * pTextTail = RegisterTextTail(VirtualID, c_szText, pOwner, c_TextTail_Name_Position, c_TextTail_Item_Color);
	m_ItemTextTailMap.insert(TTextTailMap::value_type(VirtualID, pTextTail));
#endif
}

void CPythonTextTail::RegisterChatTail(DWORD VirtualID, const char * c_szChat)
{
	CInstanceBase * pCharacterInstance = CPythonCharacterManager::Instance().GetInstancePtr(VirtualID);

	if (!pCharacterInstance)
		return;

	TChatTailMap::iterator itor = m_ChatTailMap.find(VirtualID);

	if (m_ChatTailMap.end() != itor)
	{
		TTextTail * pTextTail = itor->second;

		pTextTail->pTextInstance->SetValue(c_szChat);
		pTextTail->pTextInstance->Update();
		pTextTail->Color = c_TextTail_Chat_Color;
		pTextTail->pTextInstance->SetColor(c_TextTail_Chat_Color);

		// TEXTTAIL_LIVINGTIME_CONTROL
		pTextTail->LivingTime = CTimer::Instance().GetCurrentMillisecond() + TextTail_GetLivingTime();
		// END_OF_TEXTTAIL_LIVINGTIME_CONTROL

		pTextTail->bNameFlag = TRUE;

		return;
	}

	TTextTail * pTextTail = RegisterTextTail(VirtualID,
											 c_szChat,
											 pCharacterInstance->GetGraphicThingInstancePtr(),
											 pCharacterInstance->GetGraphicThingInstanceRef().GetHeight() + 10.0f,
											 c_TextTail_Chat_Color);

	// TEXTTAIL_LIVINGTIME_CONTROL
	pTextTail->LivingTime = CTimer::Instance().GetCurrentMillisecond() + TextTail_GetLivingTime();
	// END_OF_TEXTTAIL_LIVINGTIME_CONTROL

	pTextTail->bNameFlag = TRUE;
	pTextTail->pTextInstance->SetOutline(true);
	pTextTail->pTextInstance->SetVerticalAlign(CGraphicTextInstance::VERTICAL_ALIGN_BOTTOM);
	m_ChatTailMap.insert(TTextTailMap::value_type(VirtualID, pTextTail));
}

void CPythonTextTail::RegisterInfoTail(DWORD VirtualID, const char * c_szChat)
{
	CInstanceBase * pCharacterInstance = CPythonCharacterManager::Instance().GetInstancePtr(VirtualID);

	if (!pCharacterInstance)
		return;

	TChatTailMap::iterator itor = m_ChatTailMap.find(VirtualID);

	if (m_ChatTailMap.end() != itor)
	{
		TTextTail * pTextTail = itor->second;

		pTextTail->pTextInstance->SetValue(c_szChat);
		pTextTail->pTextInstance->Update();
		pTextTail->Color = c_TextTail_Info_Color;
		pTextTail->pTextInstance->SetColor(c_TextTail_Info_Color);

		// TEXTTAIL_LIVINGTIME_CONTROL
		pTextTail->LivingTime = CTimer::Instance().GetCurrentMillisecond() + TextTail_GetLivingTime();
		// END_OF_TEXTTAIL_LIVINGTIME_CONTROL

		pTextTail->bNameFlag = FALSE;

		return;
	}

	TTextTail * pTextTail = RegisterTextTail(VirtualID,
											 c_szChat,
											 pCharacterInstance->GetGraphicThingInstancePtr(),
											 pCharacterInstance->GetGraphicThingInstanceRef().GetHeight() + 10.0f,
											 c_TextTail_Info_Color);

	// TEXTTAIL_LIVINGTIME_CONTROL
	pTextTail->LivingTime = CTimer::Instance().GetCurrentMillisecond() + TextTail_GetLivingTime();
	// END_OF_TEXTTAIL_LIVINGTIME_CONTROL

	pTextTail->bNameFlag = FALSE;
	pTextTail->pTextInstance->SetOutline(true);
	pTextTail->pTextInstance->SetVerticalAlign(CGraphicTextInstance::VERTICAL_ALIGN_BOTTOM);
	m_ChatTailMap.insert(TTextTailMap::value_type(VirtualID, pTextTail));
}

bool CPythonTextTail::GetTextTailPosition(DWORD dwVID, float* px, float* py, float* pz)
{
	TTextTailMap::iterator itorCharacter = m_CharacterTextTailMap.find(dwVID);

	if (m_CharacterTextTailMap.end() == itorCharacter)
	{
		return false;
	}

	TTextTail * pTextTail = itorCharacter->second;
	*px=pTextTail->x;
	*py=pTextTail->y;
	*pz=pTextTail->z;

	return true;
}

bool CPythonTextTail::IsChatTextTail(DWORD dwVID)
{
	TChatTailMap::iterator itorChat = m_ChatTailMap.find(dwVID);

	if (m_ChatTailMap.end() == itorChat)
		return false;

	return true;
}

void CPythonTextTail::SetCharacterTextTailColor(DWORD VirtualID, const D3DXCOLOR & c_rColor)
{
	TTextTailMap::iterator itorCharacter = m_CharacterTextTailMap.find(VirtualID);

	if (m_CharacterTextTailMap.end() == itorCharacter)
		return;

	TTextTail * pTextTail = itorCharacter->second;
	pTextTail->pTextInstance->SetColor(c_rColor);
	pTextTail->Color = c_rColor;
}

void CPythonTextTail::SetItemTextTailOwner(DWORD dwVID, const char * c_szName)
{
	TTextTailMap::iterator itor = m_ItemTextTailMap.find(dwVID);
	if (m_ItemTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;

	if (strlen(c_szName) > 0)
	{
		if (!pTextTail->pOwnerTextInstance)
		{
			pTextTail->pOwnerTextInstance = CGraphicTextInstance::New();
		}

		std::string strName = c_szName;
		pTextTail->pOwnerTextInstance->SetTextPointer(ms_pFont);
		pTextTail->pOwnerTextInstance->SetHorizonalAlign(CGraphicTextInstance::HORIZONTAL_ALIGN_CENTER);
		pTextTail->pOwnerTextInstance->SetValue(strName.c_str());
		pTextTail->pOwnerTextInstance->SetColor(1.0f, 1.0f, 0.0f);
		pTextTail->pOwnerTextInstance->Update();

		int xOwnerSize, yOwnerSize;
		pTextTail->pOwnerTextInstance->GetTextSize(&xOwnerSize, &yOwnerSize);
		pTextTail->yStart = -2.0f;
		pTextTail->yEnd += float(yOwnerSize + 4);
		pTextTail->xStart = fMIN(pTextTail->xStart, float(-xOwnerSize / 2 - 1));
		pTextTail->xEnd = fMAX(pTextTail->xEnd, float(xOwnerSize / 2 + 1));
	}
	else
	{
		if (pTextTail->pOwnerTextInstance)
		{
			CGraphicTextInstance::Delete(pTextTail->pOwnerTextInstance);
			pTextTail->pOwnerTextInstance = NULL;
		}

		int xSize, ySize;
		pTextTail->pTextInstance->GetTextSize(&xSize, &ySize);
		pTextTail->xStart = (float) (-xSize / 2 - 2);
		pTextTail->yStart = -2.0f;
		pTextTail->xEnd = (float) (xSize / 2 + 2);
		pTextTail->yEnd = (float) ySize;
	}
}

void CPythonTextTail::DeleteCharacterTextTail(DWORD VirtualID)
{
	TTextTailMap::iterator itorCharacter = m_CharacterTextTailMap.find(VirtualID);
	TTextTailMap::iterator itorChat = m_ChatTailMap.find(VirtualID);

	if (m_CharacterTextTailMap.end() != itorCharacter)
	{
		DeleteTextTail(itorCharacter->second);
		m_CharacterTextTailMap.erase(itorCharacter);
	}
	else
	{
		Tracenf("CPythonTextTail::DeleteCharacterTextTail - Find VID[%d] Error", VirtualID);
	}

	if (m_ChatTailMap.end() != itorChat)
	{
		DeleteTextTail(itorChat->second);
		m_ChatTailMap.erase(itorChat);
	}
}

void CPythonTextTail::DeleteItemTextTail(DWORD VirtualID)
{
	TTextTailMap::iterator itor = m_ItemTextTailMap.find(VirtualID);

	if (m_ItemTextTailMap.end() == itor)
	{
		Tracef(" CPythonTextTail::DeleteItemTextTail - None Item Text Tail\n");
		return;
	}

	DeleteTextTail(itor->second);
	m_ItemTextTailMap.erase(itor);
}

CPythonTextTail::TTextTail * CPythonTextTail::RegisterTextTail(DWORD dwVirtualID, const char * c_szText, CGraphicObjectInstance * pOwner, float fHeight, const D3DXCOLOR & c_rColor)
{
	TTextTail * pTextTail = m_TextTailPool.Alloc();

	pTextTail->dwVirtualID = dwVirtualID;
	pTextTail->pOwner = pOwner;
	pTextTail->pTextInstance = CGraphicTextInstance::New();
	pTextTail->pOwnerTextInstance = NULL;
	pTextTail->fHeight = fHeight;

	pTextTail->pTextInstance->SetTextPointer(ms_pFont);
	pTextTail->pTextInstance->SetHorizonalAlign(CGraphicTextInstance::HORIZONTAL_ALIGN_CENTER);
	pTextTail->pTextInstance->SetValue(c_szText);
	pTextTail->pTextInstance->SetColor(c_rColor.r, c_rColor.g, c_rColor.b);
	pTextTail->pTextInstance->Update();

	int xSize, ySize;
	pTextTail->pTextInstance->GetTextSize(&xSize, &ySize);
	pTextTail->xStart				= (float) (-xSize / 2 - 2);
	pTextTail->yStart				= -2.0f;
	pTextTail->xEnd					= (float) (xSize / 2 + 2);
	pTextTail->yEnd					= (float) ySize;
	pTextTail->Color				= c_rColor;
	pTextTail->fDistanceFromPlayer	= 0.0f;
	pTextTail->x = -100.0f;
	pTextTail->y = -100.0f;
	pTextTail->z = 0.0f;
	pTextTail->pMarkInstance = NULL;
	pTextTail->pGuildNameTextInstance = NULL;
	pTextTail->pTitleTextInstance = NULL;
	pTextTail->pLevelTextInstance = NULL;
	return pTextTail;
}

void CPythonTextTail::DeleteTextTail(TTextTail * pTextTail)
{
	if (pTextTail->pTextInstance)
	{
		CGraphicTextInstance::Delete(pTextTail->pTextInstance);
		pTextTail->pTextInstance = NULL;
	}
	if (pTextTail->pOwnerTextInstance)
	{
		CGraphicTextInstance::Delete(pTextTail->pOwnerTextInstance);
		pTextTail->pOwnerTextInstance = NULL;
	}
	if (pTextTail->pMarkInstance)
	{
		CGraphicMarkInstance::Delete(pTextTail->pMarkInstance);
		pTextTail->pMarkInstance = NULL;
	}
	if (pTextTail->pGuildNameTextInstance)
	{
		CGraphicTextInstance::Delete(pTextTail->pGuildNameTextInstance);
		pTextTail->pGuildNameTextInstance = NULL;
	}
	if (pTextTail->pTitleTextInstance)
	{
		CGraphicTextInstance::Delete(pTextTail->pTitleTextInstance);
		pTextTail->pTitleTextInstance = NULL;
	}
	if (pTextTail->pLevelTextInstance)
	{
		CGraphicTextInstance::Delete(pTextTail->pLevelTextInstance);
		pTextTail->pLevelTextInstance = NULL;
	}

	m_TextTailPool.Free(pTextTail);	
}

int CPythonTextTail::Pick(int ixMouse, int iyMouse)
{
	for (TTextTailMap::iterator itor = m_ItemTextTailMap.begin(); itor != m_ItemTextTailMap.end(); ++itor)
	{
		TTextTail * pTextTail = itor->second;

		if (ixMouse >= pTextTail->x + pTextTail->xStart && ixMouse <= pTextTail->x + pTextTail->xEnd &&
			iyMouse >= pTextTail->y + pTextTail->yStart && iyMouse <= pTextTail->y + pTextTail->yEnd)
		{
			SelectItemName(itor->first);
			return (itor->first);
		}
	}

	return -1;
}

void CPythonTextTail::SelectItemName(DWORD dwVirtualID)
{
	TTextTailMap::iterator itor = m_ItemTextTailMap.find(dwVirtualID);

	if (m_ItemTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;
	pTextTail->pTextInstance->SetColor(0.1f, 0.9f, 0.1f);
}

void CPythonTextTail::AttachTitle(DWORD dwVID, const char * c_szName, const D3DXCOLOR & c_rColor)
{
	if (!bPKTitleEnable)
		return;

	TTextTailMap::iterator itor = m_CharacterTextTailMap.find(dwVID);
	if (m_CharacterTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;

	CGraphicTextInstance *& prTitle = pTextTail->pTitleTextInstance;
	if (!prTitle)
	{
		prTitle = CGraphicTextInstance::New();
		prTitle->SetTextPointer(ms_pFont);
		prTitle->SetOutline(true);
		prTitle->SetHorizonalAlign(CGraphicTextInstance::HORIZONTAL_ALIGN_LEFT);
		prTitle->SetVerticalAlign(CGraphicTextInstance::VERTICAL_ALIGN_BOTTOM);
	}

	prTitle->SetValue(c_szName);
	prTitle->SetColor(c_rColor.r, c_rColor.g, c_rColor.b);
	prTitle->Update();
}

void CPythonTextTail::DetachTitle(DWORD dwVID)
{
	if (!bPKTitleEnable)
		return;

	TTextTailMap::iterator itor = m_CharacterTextTailMap.find(dwVID);
	if (m_CharacterTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;

	if (pTextTail->pTitleTextInstance)
	{
		CGraphicTextInstance::Delete(pTextTail->pTitleTextInstance);
		pTextTail->pTitleTextInstance = NULL;
	}
}

void CPythonTextTail::EnablePKTitle(BOOL bFlag)
{
	bPKTitleEnable = bFlag;
}

void CPythonTextTail::AttachLevel(DWORD dwVID, const char * c_szText, const D3DXCOLOR & c_rColor)
{
	if (!bPKTitleEnable)
		return;

	TTextTailMap::iterator itor = m_CharacterTextTailMap.find(dwVID);
	if (m_CharacterTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;

	CGraphicTextInstance *& prLevel = pTextTail->pLevelTextInstance;
	if (!prLevel)
	{
		prLevel = CGraphicTextInstance::New();
		prLevel->SetTextPointer(ms_pFont);
		prLevel->SetOutline(true);

		prLevel->SetHorizonalAlign(CGraphicTextInstance::HORIZONTAL_ALIGN_LEFT);
		prLevel->SetVerticalAlign(CGraphicTextInstance::VERTICAL_ALIGN_BOTTOM);
	}

	prLevel->SetValue(c_szText);
	prLevel->SetColor(c_rColor.r, c_rColor.g, c_rColor.b);
	prLevel->Update();
}

void CPythonTextTail::DetachLevel(DWORD dwVID)
{
	if (!bPKTitleEnable)
		return;

	TTextTailMap::iterator itor = m_CharacterTextTailMap.find(dwVID);
	if (m_CharacterTextTailMap.end() == itor)
		return;

	TTextTail * pTextTail = itor->second;

	if (pTextTail->pLevelTextInstance)
	{
		CGraphicTextInstance::Delete(pTextTail->pLevelTextInstance);
		pTextTail->pLevelTextInstance = NULL;
	}
}


void CPythonTextTail::RefreshAllGuildMark()
{
	CPackManager::instance().SetFileLoadMode();

	for (TTextTailMap::iterator itor = m_CharacterTextTailMap.begin(); itor != m_CharacterTextTailMap.end(); ++itor)
	{
		TTextTail* pTextTail = itor->second;

		if (!pTextTail->pMarkInstance)
			continue;

		// Re-load the mark instance to get the updated texture
		pTextTail->pMarkInstance->Load();
	}

	CPackManager::instance().SetPackLoadMode();
}

void CPythonTextTail::Initialize()
{
	// DEFAULT_FONT
	//ms_pFont = (CGraphicText *)CResourceManager::Instance().GetTypeResourcePointer(g_strDefaultFontName.c_str());

	CGraphicText* pkDefaultFont = static_cast<CGraphicText*>(DefaultFont_GetResource());
	if (!pkDefaultFont)
	{
		TraceError("CPythonTextTail::Initialize - CANNOT_FIND_DEFAULT_FONT");
		return;
	}

	ms_pFont = pkDefaultFont;
	// END_OF_DEFAULT_FONT
}

void CPythonTextTail::Destroy()
{
	m_TextTailPool.Clear();
}

void CPythonTextTail::Clear()
{
	m_CharacterTextTailMap.clear();
	m_CharacterTextTailList.clear();
	m_ItemTextTailMap.clear();
	m_ItemTextTailList.clear();
	m_ChatTailMap.clear();
	m_dwLastCollisionChecks = 0;
	m_dwLastArrangeMS = 0;
	m_dwLastRenderMS = 0;

	m_TextTailPool.Clear();
}

CPythonTextTail::CPythonTextTail()
{
	m_bOptimizationEnabled = true;
	m_iOptimizationProfile = 1;
	m_bGridOptimizationEnabled = true;
	m_dwLastArrangeTime = 0;
	m_dwArrangeIntervalMS = 100;
	m_iMaxRenderCount = 80;
	m_fOptimizationMaxDistance = DX11RuntimeConfig::kTextTailDefaultMaxDistance;
	m_dwLastArrangeMS = 0;
	m_dwLastRenderMS = 0;
	m_dwLastCollisionChecks = 0;
	Clear();
}

CPythonTextTail::~CPythonTextTail()
{
	Destroy();
}
