#pragma once

enum class GameBuild
{
	Unknown = 0,
	Steam,
	EA,
	Current
};

enum class Addr
{
	// FixInputBinding
	InputFix,
	LoadStartupPackages,

	// FixHighFPSHairPhysics
	HairSimulator,
	HairSimulator_DampingScaler,
	HairSimulator_DeltaTimeOverride,

	// FixHighFPSClothPhysics
	ClothSimulator,

	// FixHighFPSProjectileCollisionCheck
	RangeAttackPawnCollisionCheck,

	// FixHighFPSRagdollDeath
	RagdollDeath,

	// FixHighFPSWalkingPhysics
	WalkStationaryAbortX,
	WalkStationaryAbortY,
	WalkFloorStick,
	WalkVelocityRecompute,
	WalkVelocityRecomputeSkip,

	// Hash-table race condition
	Localize,
	HashLoop,
	FStringFree,
	FStringAlloc,

	// Unattached-collision panic
	FixUnattachedCollisionPanic,

	// Hair-curve over-read
	FixHairCurveOverRead,

	// MMX over-read + spline-prefetch overrun
	MmxOverRead,
	SplinePrefetchOverrun,

	// FaceFX actor teardown
	FaceFxActorLoopGuard,
	FaceFxActorLoopExit,

	// Render-element dispatch
	RenderDispatchGuard,
	RenderCacheGuard,

	// FixCPUPhysX
	PhysXLoad,

	// FixWindowHandling
	UpdateMouseLock,
	ProcessDeferredMessage,
	BlockHook,
	BlockMessages_1,
	BlockMessages_2,

	// IntroSkip
	PlayMovie,
	SkipMovie,

	// WarnAlice1InstallFolder
	WarnAlice1InstallFolder,

	// FontScaling
	FontScaling_HeightFactor,
	FontScaling_Size,
	FontScaling_LayoutMetrics,
	FontScaling_LineSpacing,

	// AutoResolution
	DocPath,
	ApplyAutoResolution,
	Width,
	Height,

	// FixAspectRatio
	BlackBarDraw,

	// MenuScripts
	MenuScripts,

	// GFxLoadRootMovie
	GFxLoadRootMovie,

	// ImprovedTextureStreaming / ForceHighResTextures
	ShouldMipLevelsBeForcedResident,
	GetWantedMips,

	// ReducedMipMapBias
	MipMapBias,

	// DisableBackgroundLevelStreaming
	BackgroundLevelStreaming,

	// FixBinkVideoBT709
	Gyuvtorgb,

	// ResolutionHook
	UpdateViewportRHI,

	// EnginePointers
	PlayActorPtr,
	GetGEnginePtr,
	SetAlicePlayerController,
	SetConsole,
	EnginePostInit,

	// MainLoop
	EngineTick,
	ProcessEvent,

	// FixMissingMusic
	LoadingBinkIsFinished,
	UpdateMusicTrack,

	// UseWindowed
	WindowedMode,
	Fullscreen,

	// AchievementSupport
	PlayerControllerConsoleCommand,
	ActorConsoleCommand,
	MenuCursorRead,
	ProfileNameRead,
	GameLanguageSet,
	GameLanguageName,

	// AtomicSaves
	FileWriterOpen,
	FileWriterDtor,

	// AchievementOverlay
	D3D9DevicePtr,
	D3D9PresentWindowed,
	D3D9PresentFullscreen,
	D3D9ResetPre,
	D3D9ResetPost,

	Count
};

namespace Addresses
{
	inline GameBuild g_build = GameBuild::Unknown;
	inline uintptr_t g_moduleBase = 0;

	// { Steam, EA, Current }
	inline constexpr uintptr_t kAddressTable[static_cast<size_t>(Addr::Count)][3] =
	{
		// FixInputBinding
		/* InputFix                        */ { 0x4815E1, 0x481371, 0x472D91 },
		/* LoadStartupPackages             */ { 0xC90080, 0xC90690, 0xC36AE0 },

		// FixHighFPSHairPhysics
		/* HairSimulator                   */ { 0xBE4860, 0xBE4A70, 0xB99320 },
		/* HairSimulator_DampingScaler     */ { 0xBE415B, 0xBE436B, 0xB98D1B },
		/* HairSimulator_DeltaTimeOverride */ { 0xBE4892, 0xBE4AA2, 0xB99352 },

		// FixHighFPSClothPhysics
		/* ClothSimulator                  */ { 0xBE4DE0, 0xBE4FF0, 0xB99660 },

		// FixHighFPSProjectileCollisionCheck
		/* RangeAttackPawnCollisionCheck   */ { 0xD42A50, 0xD43030, 0xCD5A40 },

		// FixHighFPSRagdollDeath
		/* RagdollDeath                    */ { 0xCA6939, 0xCA6ED9, 0xC4BBEA },

		// FixHighFPSWalkingPhysics
		/* WalkStationaryAbortX            */ { 0xD1A349, 0xD1A9E9, 0xCB2FF9 },
		/* WalkStationaryAbortY            */ { 0xD1A3B1, 0xD1AA51, 0xCB3061 },
		/* WalkFloorStick                  */ { 0xD1B4E1, 0xD1BB81, 0xCB4191 },
		/* WalkVelocityRecompute           */ { 0xD1BC44, 0xD1C2E4, 0xCB48F4 },
		/* WalkVelocityRecomputeSkip       */ { 0xD1BCC5, 0xD1C365, 0xCB4975 },

		// Hash-table race condition
		/* Localize                        */ { 0x4E9E80, 0x4EA250, 0x4D3ED0 },
		/* HashLoop                        */ { 0x47C093, 0x47C003, 0x46DE63 },
		/* FStringFree                     */ { 0x48C930, 0x48C6C0, 0x47B950 },
		/* FStringAlloc                    */ { 0x4116F0, 0x4115C0, 0x4107C0 },

		// Unattached-collision panic
		/* FixUnattachedCollisionPanic     */ { 0x7AF324, 0x7AFBC4, 0x77E824 },

		// Hair-curve over-read
		/* FixHairCurveOverRead            */ { 0xBE1514, 0xBE1734, 0xB95924 },

		// MMX over-read + spline-prefetch overrun
		/* MmxOverRead                     */ { 0x875DFE, 0x87613E, 0x83D2FE },
		/* SplinePrefetchOverrun           */ { 0xBE151E, 0xBE173E, 0xB9592E },

		// FaceFX actor teardown
		/* FaceFxActorLoopGuard            */ { 0x1053D13, 0x10547D3, 0xFE0F63 },
		/* FaceFxActorLoopExit             */ { 0x1053D1C, 0x10547DC, 0xFE0F6C },

		// Render-element dispatch
		/* RenderDispatchGuard             */ { 0xBB3162, 0xBB31D2, 0xB69032 },
		/* RenderCacheGuard                */ { 0xBB3490, 0xBB3500, 0xB69360 },

		// FixCPUPhysX
		/* PhysXLoad                       */ { 0x9DAC90, 0x9DB430, 0x9982C0 },

		// FixWindowHandling
		/* UpdateMouseLock                 */ { 0xC7E370, 0xC7E9B0, 0xC278C0 },
		/* ProcessDeferredMessage          */ { 0xC84370, 0xC848D0, 0xC2C800 },
		/* BlockHook                       */ { 0xC86A09, 0xC86F69, 0xC2EBD6 },
		/* BlockMessages_1                 */ { 0xC8019B, 0xC8073B, 0xC28E51 },
		/* BlockMessages_2                 */ { 0xC810AE, 0xC8164E, 0xC29BDE },

		// IntroSkip
		/* PlayMovie                       */ { 0x5942E0, 0x5949F0, 0x574380 },
		/* SkipMovie                       */ { 0x594A50, 0x595160, 0x574AF0 },

		// WarnAlice1InstallFolder
		/* WarnAlice1InstallFolder         */ { 0xC93019, 0xC936BD, 0xC397A5 },

		// FontScaling
		/* FontScaling_HeightFactor        */ { 0xA89213, 0xA89AB3, 0xA43BF3 },
		/* FontScaling_Size                */ { 0xA0B703, 0xA0C093, 0x9C7463 },
		/* FontScaling_LayoutMetrics       */ { 0xA899B1, 0xA8A251, 0xA44391 },
		/* FontScaling_LineSpacing         */ { 0x96A64B, 0x96ADBB, 0x92D48B },

		// AutoResolution
		/* DocPath                         */ { 0x480004, 0x47FE64, 0x470DB4 },
		/* ApplyAutoResolution             */ { 0x773EC6, 0x774BAF, 0x745E8D },
		/* Width                           */ { 0x14FE25C, 0x14FE25C, 0x1479884 },
		/* Height                          */ { 0x14FE260, 0x14FE260, 0x1479888 },

		// FixAspectRatio
		/* BlackBarDraw                    */ { 0xC71FA4, 0xC72514, 0xC1D344 },

		// MenuScripts
		/* MenuScripts                     */ { 0xF11D99, 0xF121A9, 0xE9EA29 },

		// GFxLoadRootMovie
		/* GFxLoadRootMovie                */ { 0xF429C0, 0xF42ED0, 0xECF8C0 },

		// ImprovedTextureStreaming / ForceHighResTextures
		/* ShouldMipLevelsBeForcedResident */ { 0x646220, 0x6469F0, 0x620FE0 },
		/* GetWantedMips                   */ { 0x6CC5E0, 0x6CCD60, 0x6A2110 },

		// ReducedMipMapBias
		/* MipMapBias                      */ { 0xC7137A, 0xC7188A, 0xC1C6BA },

		// DisableBackgroundLevelStreaming
		/* BackgroundLevelStreaming        */ { 0x9918DE, 0x991F4E, 0x95288E },

		// FixBinkVideoBT709
		/* Gyuvtorgb                       */ { 0x1421DC0, 0x1421DC0, 0x139ECE0 },

		// ResolutionHook
		/* UpdateViewportRHI               */ { 0x6D73C0, 0x6D7B60, 0x6AD6E0 },

		// EnginePointers
		/* PlayActorPtr                    */ { 0x86497A, 0x864C2A, 0x8304B2 },
		/* GetGEnginePtr                   */ { 0xC9060F, 0xC90C1F, 0xC3712C },
		/* SetAlicePlayerController        */ { 0x65F7B0, 0x65FFB0, 0x63B330 },
		/* SetConsole                      */ { 0xC92CCE, 0xC932DE, 0xC39502 },
		/* EnginePostInit                  */ { 0xC90751, 0xC90D61, 0xC37267 },

		// MainLoop
		/* EngineTick                      */ { 0xC8F6C0, 0xC8FCD0, 0xC361F0 },
		/* ProcessEvent                    */ { 0x4C7840, 0x4C78F0, 0x4B5270 },

		// FixMissingMusic
		/* LoadingBinkIsFinished           */ { 0x991CA5, 0x992315, 0x952C55 },
		/* UpdateMusicTrack                */ { 0x9759F0, 0x976180, 0x9372D0 },

		// UseWindowed
		/* WindowedMode                    */ { 0x773EB1, 0x774B9A, 0x745E78 },
		/* Fullscreen                      */ { 0x14FE264, 0x14FE264, 0x147988C },

		// AchievementSupport
		/* PlayerControllerConsoleCommand  */ { 0x6DDE70, 0x6DE600, 0x6B32E0 },
		/* ActorConsoleCommand             */ { 0x728416, 0x728D36, 0x7021C6 },
		/* MenuCursorRead                  */ { 0xC7EAE0, 0xC7F120, 0xC27D80 },
		/* ProfileNameRead                 */ { 0xD72047, 0xD72547, 0xCFF6B7 },
		/* GameLanguageSet                 */ { 0x51F613, 0x51FA53, 0x50233D },
		/* GameLanguageName                */ { 0x14F78C0, 0x14F78C0, 0x1472DE0 },

		// AtomicSaves
		/* FileWriterOpen                  */ { 0x47B520, 0x47B490, 0x46D3D0 },
		/* FileWriterDtor                  */ { 0x47E520, 0x47E5C0, 0x470880 },

		// AchievementOverlay
		/* D3D9DevicePtr                   */ { 0x151C2AC, 0x151C2AC, 0x149521C },
		/* D3D9PresentWindowed             */ { 0xC7B69C, 0xC7BCEC, 0xC2513C },
		/* D3D9PresentFullscreen           */ { 0xC7B648, 0xC7BC98, 0xC250E8 },
		/* D3D9ResetPre                    */ { 0xC784EB, 0xC78B3B, 0xC2230D },
		/* D3D9ResetPost                   */ { 0xC784F2, 0xC78B42, 0xC22314 },
	};

	inline void SetBuild(GameBuild build, uintptr_t moduleBase)
	{
		g_build = build;
		g_moduleBase = moduleBase;
	}

	inline GameBuild GetBuild()
	{
		return g_build;
	}
}

inline uintptr_t GetAddress(Addr id)
{
	if (Addresses::g_build == GameBuild::Unknown)
		return 0;

	uintptr_t raw = Addresses::kAddressTable[static_cast<size_t>(id)][static_cast<int>(Addresses::g_build) - 1];
	if (raw == 0) return 0;

	return Addresses::g_moduleBase + (raw - 0x400000);
}
