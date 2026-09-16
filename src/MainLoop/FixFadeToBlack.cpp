#include "Common.hpp"
#include "Features.hpp"

namespace FixFadeToBlack
{
	constexpr float EngageAmount = 0.9999f;
	constexpr float ReleaseAmount = 0.999f;

	static const FVector Zero{ 0.0f, 0.0f, 0.0f };

	// Zero the scene through the grading path
	static void Crush(FPostProcessSettings& pps)
	{
		pps.bOverride_EnableBloom = 1;
		pps.bEnableBloom = 0;
		pps.bOverride_Bloom_Scale = 1;
		pps.Bloom_Scale = 0.0f;
		pps.bOverride_EnableSceneEffect = 1;
		pps.bEnableSceneEffect = 1;
		pps.bOverride_Scene_HighLights = 1;
		pps.Scene_HighLights = FVector{ 10000.0f, 10000.0f, 10000.0f };
		pps.bOverride_Scene_MidTones = 1;
		pps.Scene_MidTones = FVector{ 8.0f, 8.0f, 8.0f };
		pps.bOverride_Scene_Shadows = 1;
		pps.Scene_Shadows = FVector{ 1.0f, 1.0f, 1.0f };
	}

	struct SavedPP
	{
		UObject* Owner;
		FPostProcessSettings* Settings;
		FPostProcessSettings Backup;
	};

	struct SavedAlpha
	{
		UObject* Owner;
		float* Alpha;
		float Backup;
	};

	static std::vector<SavedPP> g_savedSettings;
	static std::vector<SavedAlpha> g_savedAlphas;

	// Crush a settings block, saving its original once per engagement
	static void SaveAndCrush(UObject* owner, FPostProcessSettings* pps)
	{
		for (const SavedPP& entry : g_savedSettings)
		{
			if (entry.Settings == pps)
			{
				Crush(*pps);
				return;
			}
		}

		g_savedSettings.push_back({ owner, pps, *pps });
		Crush(*pps);
	}

	// Force an override alpha to full, saving its original once per engagement
	static void SaveAndForceAlpha(UObject* owner, float* alpha)
	{
		for (const SavedAlpha& entry : g_savedAlphas)
		{
			if (entry.Alpha == alpha)
			{
				*alpha = 1.0f;
				return;
			}
		}

		g_savedAlphas.push_back({ owner, alpha, *alpha });
		*alpha = 1.0f;
	}

	// Every place the view grading can be sourced from
	static void CrushEverySource(AAlicePlayerController* pc, ACamera* cam)
	{
		if (pc->WorldInfo)
		{
			SaveAndCrush(pc->WorldInfo, &pc->WorldInfo->DefaultPostProcessSettings);
		}

		for (APostProcessVolume* vol : UObject::FindAllOf<APostProcessVolume>(true))
		{
			if (vol)
			{
				SaveAndCrush(vol, &vol->Settings);
			}
		}

		for (ACameraActor* ca : UObject::FindAllOf<ACameraActor>(true))
		{
			if (ca)
			{
				SaveAndCrush(ca, &ca->CamOverridePostProcess);
				SaveAndForceAlpha(ca, &ca->CamOverridePostProcessAlpha);
			}
		}

		SaveAndCrush(cam, &cam->CamPostProcessSettings);
		SaveAndForceAlpha(cam, &cam->CamOverridePostProcessAlpha);
	}

	// Only restore into objects that still exist, streamed out levels take their actors with them
	static void RestoreEverything()
	{
		std::vector<UObject*> alive;
		alive.reserve(g_savedSettings.size());

		for (APostProcessVolume* vol : UObject::FindAllOf<APostProcessVolume>(true))
		{
			if (vol)
			{
				alive.push_back(vol);
			}
		}

		for (ACameraActor* ca : UObject::FindAllOf<ACameraActor>(true))
		{
			if (ca)
			{
				alive.push_back(ca);
			}
		}

		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (pc && pc->WorldInfo)
		{
			alive.push_back(pc->WorldInfo);
		}

		if (pc && pc->PlayerCamera)
		{
			alive.push_back(pc->PlayerCamera);
		}

		auto isAlive = [&alive](UObject* owner)
			{
				for (UObject* p : alive)
				{
					if (p == owner)
					{
						return true;
					}
				}
				return false;
			};

		for (const SavedPP& entry : g_savedSettings)
		{
			if (isAlive(entry.Owner))
			{
				*entry.Settings = entry.Backup;
			}
		}
		g_savedSettings.clear();

		for (const SavedAlpha& entry : g_savedAlphas)
		{
			if (isAlive(entry.Owner))
			{
				*entry.Alpha = entry.Backup;
			}
		}
		g_savedAlphas.clear();
	}

	void Tick()
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc || !pc->PlayerCamera)
			return;

		ACamera* cam = pc->PlayerCamera;

		static bool engaged = false;
		static ACamera* engagedCam = nullptr;
		static uint32_t savedScaling = 0;
		static uint32_t savedInterp = 0;
		static FVector savedScale{};
		static FVector savedDesired{};
		static FVector savedOriginal{};

		// Only step in for fades to black
		bool blackFade = cam->FadeColor.R < 32 && cam->FadeColor.G < 32 && cam->FadeColor.B < 32;
		bool wantFull = cam->bEnableFading && blackFade && cam->FadeAmount >= (engaged ? ReleaseAmount : EngageAmount);

		if (wantFull)
		{
			if (!engaged)
			{
				engaged = true;
				engagedCam = cam;
				savedScaling = cam->bEnableColorScaling;
				savedInterp = cam->bEnableColorScaleInterp;
				savedScale = cam->ColorScale;
				savedDesired = cam->DesiredColorScale;
				savedOriginal = cam->OriginalColorScale;
			}

			cam->bEnableColorScaling = 1;
			cam->bEnableColorScaleInterp = 0;
			cam->ColorScale = Zero;
			cam->DesiredColorScale = Zero;
			cam->OriginalColorScale = Zero;

			CrushEverySource(pc, cam);
		}
		else if (engaged)
		{
			engaged = false;

			if (cam == engagedCam)
			{
				cam->bEnableColorScaling = savedScaling;
				cam->bEnableColorScaleInterp = savedInterp;
				cam->ColorScale = savedScale;
				cam->DesiredColorScale = savedDesired;
				cam->OriginalColorScale = savedOriginal;
			}

			engagedCam = nullptr;
			RestoreEverything();
		}
	}
}