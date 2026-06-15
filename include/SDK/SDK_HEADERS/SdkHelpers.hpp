#pragma once
namespace SDKHelpers
{
	inline class UGameEngine* GetGameEngine()
	{
		static UGameEngine* cachedEngine = nullptr;

		if (cachedEngine && (cachedEngine->GamePlayers.size() > 0))
		{
			return cachedEngine;
		}

		cachedEngine = nullptr;

		for (int32_t i = 0; i < UObject::GObjObjects()->size(); i++)
		{
			UObject* uObject = UObject::GObjObjects()->at(i);

			if (uObject && uObject->IsA<UGameEngine>() && !uObject->IsDefaultObject())
			{
				UGameEngine* gameEngine = static_cast<UGameEngine*>(uObject);

				if (gameEngine->GamePlayers.size() > 0)
				{
					cachedEngine = gameEngine;
					break;
				}
			}
		}

		return cachedEngine;
	}

	inline class APlayerController* GetLocalPlayerController()
	{
		return UObject::FindFirstOf<APlayerController>();
	}

	template<typename T>
	T* GetLocalPlayerController()
	{
		return UObject::FindFirstOf<T>();
	}

	inline class AWorldInfo* GetWorldInfo()
	{
		APlayerController* playerController = GetLocalPlayerController();

		if (playerController && playerController->WorldInfo)
		{
			return playerController->WorldInfo;
		}

		return UObject::FindFirstOf<AWorldInfo>();
	}
}