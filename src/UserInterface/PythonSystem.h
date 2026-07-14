#pragma once

class CPythonSystem : public CSingleton<CPythonSystem>
{
	public:
		enum EWindow
		{
			WINDOW_STATUS,
			WINDOW_INVENTORY,
			WINDOW_ABILITY,
			WINDOW_SOCIETY,
			WINDOW_JOURNAL,
			WINDOW_COMMAND,

			WINDOW_QUICK,
			WINDOW_GAUGE,
			WINDOW_MINIMAP,
			WINDOW_CHAT,

			WINDOW_MAX_NUM,
		};

		enum
		{
			FREQUENCY_MAX_NUM  = 30,
			RESOLUTION_MAX_NUM = 100
		};

		typedef struct SResolution
		{
			DWORD	width;
			DWORD	height;
			DWORD	bpp;		// bits per pixel (high-color = 16bpp, true-color = 32bpp)

			DWORD	frequency[20];
			BYTE	frequency_count;
		} TResolution;

		typedef struct SWindowStatus
		{
			int		isVisible;
			int		isMinimized;

			int		ixPosition;
			int		iyPosition;
			int		iHeight;
		} TWindowStatus;

		typedef struct SConfig
		{
			DWORD			width;
			DWORD			height;
			DWORD			bpp;
			DWORD			frequency;
			int				iRenderAPI;
			int				iShaderCacheVersion;
			int				iRenderFPSLimit;
			bool			bVSync;
			bool			bDX11ExperimentalPresent;
			bool			bDX11FirstPassActive;
			bool			bDX11NativeVisible;
			bool			bDX11NativeUIMinimal;
			bool			bDX11NativeWorldMinimal;
			bool			bDX11NativeWorldForceVisible;
			bool			bDX11NativeWorldAutoGate;
			bool			bDX11StrictNativeOnly;
			bool			bDX11DisableDX9CompatDevice;
			bool			bDX11TerrainStabilizationMode;
			int				iDX11TexturePipelineMode;
			bool			bDX11VisibleBootstrap;
			bool			bDX11UIPassOnly;
			bool			bDX11UINativeTest;
			bool			bDX11UITextureTest;
			bool			bDX11WorldDepthTest;
			bool			bDX11WorldBatchTest;
			bool			bDX11WorldSpriteTest;
			bool			bDX11WorldStateTest;
			bool			bDX11WorldPassesTest;
			bool			bDX11WorldBridgeTest;
			bool			bDX11WorldSubsystemTest;
			bool			bDX11WorldRealtimeTest;
			bool			bDX11WorldMetricsTest;
			bool			bDX11WorldInstanceFeedTest;
			bool			bDX11WorldFinalcheckTest;
			bool			bDX11WorldHandoffTest;
			bool			bDX11WorldSwapchainTest;
			bool			bDX11WorldPresentPathTest;
			bool			bDX11WorldVisiblePass1Test;
			bool			bDX11WorldComposerTest;
			bool			bDX11WorldScenegraphTest;
			bool			bDX11WorldPipelineTest;
			bool			bDX11WorldFramegraphTest;
			int				iPerfProfile;
			bool			bFXAdaptive;
			bool			bAnimLOD;
			bool			bTextTailOpt;
			int				iShadowCadence;
			int				iFXStrideBias;
			bool			bShadowDynamicBoost;
			bool			bTextTailGridOpt;
			int				iTextTailOptRange;

			bool			is_software_cursor;
			bool			is_object_culling;
			int				iDistance;
			int				iShadowLevel;
			// MR-14: Fog update by Alaric
			int 			iFogLevel;
			// MR-14: -- END OF -- Fog update by Alaric

			FLOAT			music_volume;
			FLOAT			voice_volume;

			int				gamma;

			int				isSaveID;
			char			SaveID[20];

			bool			bWindowed;
			bool			bDecompressDDS;
			bool			bNoSoundCard;
			bool			bUseDefaultIME;
			BYTE			bSoftwareTiling;
			bool			bViewChat;
			bool			bAlwaysShowName;
			bool			bShowDamage;
			bool			bShowSalesText;
		} TConfig;

	public:
		CPythonSystem();
		virtual ~CPythonSystem();

		void Clear();
		void SetInterfaceHandler(PyObject * poHandler);
		void DestroyInterfaceHandler();

		// Config
		void							SetDefaultConfig();
		bool							LoadConfig();
		bool							SaveConfig();
		void							ApplyConfig();
		void							SetConfig(TConfig * set_config);
		TConfig *						GetConfig();
		void							ChangeSystem();

		// Interface
		bool							LoadInterfaceStatus();
		void							SaveInterfaceStatus();
		bool							isInterfaceConfig();
		const TWindowStatus &			GetWindowStatusReference(int iIndex);

		DWORD							GetWidth();
		DWORD							GetHeight();
		DWORD							GetBPP();
		DWORD							GetFrequency();
		int								GetRenderAPI();
		void							SetRenderAPI(int iRenderAPI);
		int								GetShaderCacheVersion();
		void							SetShaderCacheVersion(int iVersion);
		int								GetRenderFPSLimit();
		void							SetRenderFPSLimit(int iFPSLimit);
		bool							IsVSyncEnabled();
		void							SetVSyncEnabled(bool isEnabled);
		bool							IsDX11ExperimentalPresentEnabled();
		void							SetDX11ExperimentalPresentEnabled(bool isEnabled);
		bool							IsDX11FirstPassActiveEnabled();
		void							SetDX11FirstPassActiveEnabled(bool isEnabled);
		bool							IsDX11NativeVisibleEnabled();
		void							SetDX11NativeVisibleEnabled(bool isEnabled);
		bool							IsDX11NativeUIMinimalEnabled();
		void							SetDX11NativeUIMinimalEnabled(bool isEnabled);
		bool							IsDX11NativeWorldMinimalEnabled();
		void							SetDX11NativeWorldMinimalEnabled(bool isEnabled);
		bool							IsDX11NativeWorldForceVisibleEnabled();
		void							SetDX11NativeWorldForceVisibleEnabled(bool isEnabled);
		bool							IsDX11NativeWorldAutoGateEnabled();
		void							SetDX11NativeWorldAutoGateEnabled(bool isEnabled);
		bool							IsDX11StrictNativeOnlyEnabled();
		void							SetDX11StrictNativeOnlyEnabled(bool isEnabled);
		bool							IsDX11DisableDX9CompatDeviceEnabled();
		void							SetDX11DisableDX9CompatDeviceEnabled(bool isEnabled);
		bool							IsDX11TerrainStabilizationModeEnabled();
		void							SetDX11TerrainStabilizationModeEnabled(bool isEnabled);
		int								GetDX11TexturePipelineMode();
		void							SetDX11TexturePipelineMode(int iMode);
		bool							IsDX11VisibleBootstrapEnabled();
		void							SetDX11VisibleBootstrapEnabled(bool isEnabled);
		bool							IsDX11UIPassOnlyEnabled();
		void							SetDX11UIPassOnlyEnabled(bool isEnabled);
		bool							IsDX11UINativeTestEnabled();
		void							SetDX11UINativeTestEnabled(bool isEnabled);
		bool							IsDX11UITextureTestEnabled();
		void							SetDX11UITextureTestEnabled(bool isEnabled);
		bool							IsDX11WorldDepthTestEnabled();
		void							SetDX11WorldDepthTestEnabled(bool isEnabled);
		bool							IsDX11WorldBatchTestEnabled();
		void							SetDX11WorldBatchTestEnabled(bool isEnabled);
		bool							IsDX11WorldSpriteTestEnabled();
		void							SetDX11WorldSpriteTestEnabled(bool isEnabled);
		bool							IsDX11WorldStateTestEnabled();
		void							SetDX11WorldStateTestEnabled(bool isEnabled);
		bool							IsDX11WorldPassesTestEnabled();
		void							SetDX11WorldPassesTestEnabled(bool isEnabled);
		bool							IsDX11WorldBridgeTestEnabled();
		void							SetDX11WorldBridgeTestEnabled(bool isEnabled);
		bool							IsDX11WorldSubsystemTestEnabled();
		void							SetDX11WorldSubsystemTestEnabled(bool isEnabled);
		bool							IsDX11WorldRealtimeTestEnabled();
		void							SetDX11WorldRealtimeTestEnabled(bool isEnabled);
		bool							IsDX11WorldMetricsTestEnabled();
		void							SetDX11WorldMetricsTestEnabled(bool isEnabled);
		bool							IsDX11WorldInstanceFeedTestEnabled();
		void							SetDX11WorldInstanceFeedTestEnabled(bool isEnabled);
		bool							IsDX11WorldFinalcheckTestEnabled();
		void							SetDX11WorldFinalcheckTestEnabled(bool isEnabled);
		bool							IsDX11WorldHandoffTestEnabled();
		void							SetDX11WorldHandoffTestEnabled(bool isEnabled);
		bool							IsDX11WorldSwapchainTestEnabled();
		void							SetDX11WorldSwapchainTestEnabled(bool isEnabled);
		bool							IsDX11WorldPresentPathTestEnabled();
		void							SetDX11WorldPresentPathTestEnabled(bool isEnabled);
		bool							IsDX11WorldVisiblePass1TestEnabled();
		void							SetDX11WorldVisiblePass1TestEnabled(bool isEnabled);
		bool							IsDX11WorldComposerTestEnabled();
		void							SetDX11WorldComposerTestEnabled(bool isEnabled);
		bool							IsDX11WorldScenegraphTestEnabled();
		void							SetDX11WorldScenegraphTestEnabled(bool isEnabled);
		bool							IsDX11WorldPipelineTestEnabled();
		void							SetDX11WorldPipelineTestEnabled(bool isEnabled);
		bool							IsDX11WorldFramegraphTestEnabled();
		void							SetDX11WorldFramegraphTestEnabled(bool isEnabled);
		int								GetPerfProfile();
		void							SetPerfProfile(int iProfile);
		bool							IsFXAdaptiveEnabled();
		void							SetFXAdaptiveEnabled(bool isEnabled);
		bool							IsAnimLODEnabled();
		void							SetAnimLODEnabled(bool isEnabled);
		bool							IsTextTailOptEnabled();
		void							SetTextTailOptEnabled(bool isEnabled);
		int								GetShadowCadence();
		void							SetShadowCadence(int iCadence);
		int								GetFXStrideBias();
		void							SetFXStrideBias(int iBias);
		bool							IsShadowDynamicBoostEnabled();
		void							SetShadowDynamicBoostEnabled(bool isEnabled);
		bool							IsTextTailGridOptEnabled();
		void							SetTextTailGridOptEnabled(bool isEnabled);
		int								GetTextTailOptRange();
		void							SetTextTailOptRange(int iRange);
		bool							IsSoftwareCursor();
		bool							IsWindowed();
		bool							IsViewChat();
		bool							IsAlwaysShowName();
		bool							IsShowDamage();
		bool							IsShowSalesText();
		bool							IsUseDefaultIME();
		bool							IsNoSoundCard();
		bool							IsAutoTiling();
		bool							IsSoftwareTiling();
		void							SetSoftwareTiling(bool isEnable);
		void							SetViewChatFlag(int iFlag);
		void							SetAlwaysShowNameFlag(int iFlag);
		void							SetShowDamageFlag(int iFlag);
		void							SetShowSalesTextFlag(int iFlag);

		// Window
		void							SaveWindowStatus(int iIndex, int iVisible, int iMinimized, int ix, int iy, int iHeight);

		// SaveID
		int								IsSaveID();
		const char *					GetSaveID();
		void							SetSaveID(int iValue, const char * c_szSaveID);

		/// Display
		void							GetDisplaySettings();

		int								GetResolutionCount();
		int								GetFrequencyCount(int index);
		bool							GetResolution(int index, OUT DWORD *width, OUT DWORD *height, OUT DWORD *bpp);
		bool							GetFrequency(int index, int freq_index, OUT DWORD *frequncy);
		int								GetResolutionIndex(DWORD width, DWORD height, DWORD bpp);
		int								GetFrequencyIndex(int res_index, DWORD frequency);
		bool							isViewCulling();

		// Sound
		float							GetMusicVolume();
		float							GetSoundVolume();
		void							SetMusicVolume(float fVolume);
		void							SetSoundVolume(float fVolume);

		int								GetDistance();
		int								GetShadowLevel();
		void							SetShadowLevel(unsigned int level);

		// MR-14: Fog update by Alaric
		int								GetFogLevel();
		void							SetFogLevel(unsigned int level);
		// MR-14: -- END OF -- Fog update by Alaric

	protected:
		TResolution						m_ResolutionList[RESOLUTION_MAX_NUM];
		int								m_ResolutionCount;

		TConfig							m_Config;
		TConfig							m_OldConfig;

		bool							m_isInterfaceConfig;
		PyObject *						m_poInterfaceHandler;
		TWindowStatus					m_WindowStatus[WINDOW_MAX_NUM];
};
