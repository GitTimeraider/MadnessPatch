#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook ProcessEvent;

static UFunction* g_tickAlice = nullptr;
static UFunction* g_dlcStatusMovie = nullptr;
static UFunction* g_dlcStatusPC = nullptr;
static UFunction* g_calloutMovie = nullptr;
static UFunction* g_calloutPC = nullptr;
static UFunction* g_updateCam = nullptr;
static UFunction* g_getPlatform = nullptr;
static UFunction* g_finishFirstUpgrade = nullptr;
static UFunction* g_interpStarted = nullptr;
static UFunction* g_interpFinished = nullptr;

static AWeapon* volatile g_weaponToRefresh = nullptr;
static AWeapon* s_lastSeenWeapon = nullptr;

static inline int DesiredControllerIconSet()
{
	if (UsePS3ControllerIcons == 1) return 2; // Xbox 360 icons
	if (UsePS3ControllerIcons == 2) return 3; // PS3 icons
	return (ControllerHelper::GetGamepadStyle() == ControllerHelper::GamepadStyle::PlayStation) ? 3 : 2;
}

static void __fastcall ProcessEvent_Hook(int This, void*, UFunction* Function, int uParams, int uResult)
{
	bool isDlcStatus = (Function == g_dlcStatusMovie || Function == g_dlcStatusPC);
	bool isCallout = (Function == g_calloutMovie || Function == g_calloutPC);

	if (UnlockCompleteEditionDLC && Function == g_tickAlice)
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (pc && pc->WorldInfo && pc->WorldInfo->Game && pc->Pawn)
		{
			AAliceGameInfo* gi = (AAliceGameInfo*)pc->WorldInfo->Game;
			gi->DLC_VB_UnLock = 1;
			gi->DLC_ES_UnLock = 1;
			gi->DLC_HH_UnLock = 1;
			gi->DLC_TC_UnLock = 1;

			AWeapon* w = pc->Pawn->Weapon;
			if (w && w != s_lastSeenWeapon)
			{
				s_lastSeenWeapon = w;
				g_weaponToRefresh = w;
			}
		}
		else
		{
			s_lastSeenWeapon = nullptr;
			g_weaponToRefresh = nullptr;
		}
	}

	if (CutsceneFPSCapEnabled && uParams)
	{
		if (Function == g_interpStarted)
		{
			CutsceneFPSCap::OnInterpStarted(*reinterpret_cast<USeqAct_Interp**>(uParams));
		}
		else if (Function == g_interpFinished)
		{
			CutsceneFPSCap::OnInterpFinished(*reinterpret_cast<USeqAct_Interp**>(uParams));
		}
	}

	ProcessEvent.thiscall<void>(This, Function, uParams, uResult);

	if (UnlockCompleteEditionDLC && isDlcStatus && uParams)
	{
		*(int32_t*)((uint8_t*)uParams + 0x04) = 2;
	}

	// In-game prompts
	if (isCallout && uParams)
	{
		if (g_State.isUsingGamepad && *(int32_t*)((uint8_t*)uParams + 0x00) == 2 && DesiredControllerIconSet() == 3)
		{
			*(int32_t*)((uint8_t*)uParams + 0x00) = 3;
		}
	}

	// Menu prompts
	if (EnableControllerIcons && Function == g_getPlatform && uParams && ControllerHelper::IsConnected())
	{
		*(int32_t*)((uint8_t*)uParams + 0x00) = DesiredControllerIconSet();
	}

	if (FixUpgradeCursorLeak && Function == g_finishFirstUpgrade)
	{
		if (UGFxMovie* mv = (UGFxMovie*)UObject::FindFirstOf<UAliceGFxMovie_HUD>())
		{
			static UFunction* asVoid = UFunction::FindFunction("Function GFxUI.GFxMovie.ActionScriptVoid");
			if (asVoid)
			{
				struct { FString Path; } p;
				p.Path = FString(L"_root.cmc.removeMovieClip");
				mv->ProcessEvent(asVoid, &p, nullptr);
			}
		}
	}

	if (FixAspectRatio && g_State.isWideScreen && Function == g_updateCam && This && g_State.bRealGameplay)
	{
		AAlicePlayerController* pc = g_State.AlicePlayerController;
		AAlicePawn* pawn = pc ? (AAlicePawn*)pc->Pawn : nullptr;

		if (pawn)
		{
			float base = pawn->AliceCameraFOV;
			float want = ScaleFOV(base, ASPECT_RATIO_16_9, g_State.currentAspectRatio);
			ACamera* cam = (ACamera*)This;

			static float eased = 0.0f;
			if (eased <= 1.0f)
			{
				eased = want;
			}

			eased += (want - eased) * 0.05f;
			cam->CameraCache.POV.FOV = eased;
		}
	}
}

static void PatchPinballCannonPrompt()
{
	UFunction* fPoi = UFunction::FindFunction("Function AliceGame.AlicePlayerController.ShowPOIUIHint");
	UFunction* fCtx = UFunction::FindFunction("Function AliceGame.AlicePlayerController.ShowContextActionUIHint");
	UFunction* fTouch = UFunction::FindFunction("Function AliceGame.PinballCannon.Touch");
	UFunction* fShoot = UFunction::FindFunction("Function AliceGame.PinballCannon.ShootOut");

	if (!fPoi || !fCtx || !fTouch || !fShoot)
		return;

	const FName poiName = fPoi->Name; // the FName an EX_VirtualFunction call to ShowPOIUIHint carries
	const FName ctxName = fCtx->Name;

	auto redirect = [&](UFunction* fn, bool fixHideArg)
	{
		TArray<uint8_t>& code = fn->Script;
		for (int32_t i = 0; i + 9 <= code.size(); i++)
		{
			if (code[i] != 0x1B) // EX_VirtualFunction
				continue;

			FName* callName = reinterpret_cast<FName*>(&code[i + 1]);
			if (*callName != poiName) // not the ShowPOIUIHint call
				continue;

			*callName = ctxName; // ShowPOIUIHint -> ShowContextActionUIHint

			if (fixHideArg)
			{
				const int32_t fc = i + 9;
				if (fc + 5 <= code.size() && code[fc] == 0x1E)
				{
					*reinterpret_cast<int32_t*>(&code[fc + 1]) = -2;
				}
			}

			return; // one ShowPOIUIHint call per function
		}
	};

	redirect(fTouch, false); // Touch  : show call, arg 0.0 -> int 0 (show) -- leave as-is
	redirect(fShoot, true); // ShootOut: hide call, -1.0f -> int -2 (clears the hint)
}

void ResolveProcessEventFunctions()
{
	g_tickAlice = UFunction::FindFunction("Function AliceGame.AlicePlayerController.PlayerTick");
	g_dlcStatusMovie = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.DLCGetStatus");
	g_dlcStatusPC = UFunction::FindFunction("Function AliceGame.AlicePlayerController.DLCGetStatus");
	g_calloutMovie = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.GetCalloutPlatform");
	g_calloutPC = UFunction::FindFunction("Function AliceGame.AlicePlayerController.GetCalloutPlatform");
	g_updateCam = UFunction::FindFunction("Function AliceGame.AlicePlayerCamera.UpdateCamera");
	g_getPlatform = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.GetPlatform");
	g_finishFirstUpgrade = UFunction::FindFunction("Function AliceGame.AliceGFXMovie.finishFirstWeaponUpgrade");
	g_interpStarted = UFunction::FindFunction("Function Engine.Actor.InterpolationStarted");
	g_interpFinished = UFunction::FindFunction("Function Engine.Actor.InterpolationFinished");

	if (FixPinballCannonPrompt)
	{
		PatchPinballCannonPrompt();
	}
}

void RefreshDLCWeapon()
{
	AWeapon* w = g_weaponToRefresh;
	if (w)
	{
		g_weaponToRefresh = nullptr;
		((AWeaponForAlice*)w)->ChangeDLCData();
	}
}

int QueryCalloutPlatform()
{
	AAlicePlayerController* pc = g_State.AlicePlayerController;

	if (!pc || !g_calloutPC)
		return 0;

	int platform = 0;
	pc->ProcessEvent(g_calloutPC, &platform, nullptr);
	return platform;
}

void ApplyProcessEventHook()
{
	ProcessEvent = HookHelper::CreateHook((void*)GetAddress(Addr::ProcessEvent), &ProcessEvent_Hook);
}