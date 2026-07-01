#include "Common.hpp"
#include "Features.hpp"

namespace BlackBarGuard
{
	void Tick()
	{
		static int menuState = -1;

		AAlicePlayerController* pc = g_State.AlicePlayerController;
		if (!pc) return;

		UAlicePlayerInput* input = (UAlicePlayerInput*)pc->PlayerInput;
		bool inputDisabled = input && input->bDisableInputInCinematic;
		bool active = !pc->bInMainMenu && !pc->bCinematicMode && !inputDisabled;

		bool canMove = active && !pc->IsPaused();

		int now = canMove ? 1 : 0;
		if (menuState == -1)
		{
			menuState = now;
		}
		else if (now != menuState)
		{
			menuState = now;
			g_State.shouldBlockBlackBar = (now == 1);
		}

		g_State.bRealGameplay = active && pc->Pawn;

		if (g_State.isWideScreen)
		{
			g_State.AliceEngine->ConstrainedAspectRatio = g_State.bRealGameplay ? g_State.currentAspectRatio : ASPECT_RATIO_16_9;
		}
	}
}
