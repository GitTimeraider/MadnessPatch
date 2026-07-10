#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook Localize;

static uintptr_t Unattached_Collision_Skip = 0;
static uintptr_t FxActorLoopExit = 0;
static uintptr_t RenderDispatchContinue = 0;
static uintptr_t FieldF0CacheExit = 0;
static uintptr_t FieldF0BuildPrimaryContinue = 0;
static uintptr_t FieldF0BuildFallbackContinue = 0;
static safetyhook::MidHook UnattachedCollisionGuard{};
static safetyhook::MidHook FxActorLoopGuard{};
static safetyhook::MidHook RenderDispatchGuard{};
static safetyhook::MidHook FieldF0CacheGuard{};
static safetyhook::MidHook FieldF0BuildPrimaryGuard{};
static safetyhook::MidHook FieldF0BuildFallbackGuard{};

static DWORD __cdecl Localize_Hook(DWORD* a1, void* a2, const wchar_t* a3, int a4, wchar_t* String1, int a6)
{
	const bool wantCannon = FixPinballCannonPrompt && a3 && _wcsicmp(a3, L"ACT_OWHH_CANNON_FIRE") == 0;
	const bool wantMatinee = SkipCutscenesWithEnter && a3 && _wcsicmp(a3, L"SKIP_CANCELMATINEE") == 0;
	const bool pcIcons = (wantCannon || wantMatinee) && QueryCalloutPlatform() == 1;

	if (a3 && _wcsicmp(a3, L"FlashUI_Chalkboard_Gryphon_2") == 0 && QueryCalloutPlatform() != 1)
	{
		a3 = L"FlashUI_Chalkboard_Gryphon_2_xbox";
	}

	// Fix a race condition
	static std::mutex locMutex;
	std::lock_guard<std::mutex> lock(locMutex);
	DWORD ret = Localize.unsafe_ccall<DWORD>(a1, a2, a3, a4, String1, a6);

	if (wantCannon && a1[0] && pcIcons)
	{
		std::wstring string(reinterpret_cast<const wchar_t*>(a1[0]));
		size_t pos = string.find(L"%MELEEICON%");

		if (pos != std::wstring::npos)
		{
			string.replace(pos, 11, L"%OBJECTIVEICON%");
			int n = (int)string.size() + 1;

			reinterpret_cast<void(__cdecl*)(void*, int)>(GetAddress(Addr::FStringFree))((void*)a1[0], 1); // free old
			a1[0] = a1[1] = a1[2] = 0;
			reinterpret_cast<void(__thiscall*)(DWORD*, int)>(GetAddress(Addr::FStringAlloc))(a1, n); // alloc
			memcpy((void*)a1[0], string.c_str(), n * sizeof(wchar_t));
			a1[1] = n;
		}
	}
	if (wantMatinee && a1[0] && pcIcons)
	{
		std::wstring string(reinterpret_cast<const wchar_t*>(a1[0]));
		size_t pos = string.find(L"%OBJECTIVEICON%");

		if (pos != std::wstring::npos)
		{
			string.replace(pos, 15, L"%CLICKLSICON%");
			int n = (int)string.size() + 1;
			memcpy((void*)a1[0], string.c_str(), n * sizeof(wchar_t));
			a1[1] = n;
		}
	}

	return ret;
}

static void OnUnattachedCollision(safetyhook::Context& ctx)
{
	*reinterpret_cast<uint32_t*>(ctx.esi + 0x3C) |= 0x80;
	ctx.eax = 0;
	ctx.eip = Unattached_Collision_Skip;
}

static void OnFxActorLoop(safetyhook::Context& ctx)
{
	if (!MemoryHelper::IsReadable(reinterpret_cast<const void*>(ctx.eax), 4))
	{
		ctx.eip = FxActorLoopExit;
	}
}

static void OnRenderElementDispatch(safetyhook::Context& ctx)
{
	if (!MemoryHelper::IsExeCode(ctx.edx))
	{
		ctx.eip = RenderDispatchContinue;
	}
}

static void OnFieldF0Cache(safetyhook::Context& ctx)
{
	if (*reinterpret_cast<const uint32_t*>(ctx.esi + 0xF0) == 0)
	{
		ctx.ecx = 0;
		ctx.eip = FieldF0CacheExit;
	}
}

static void OnFieldF0BuildPrimaryDispatch(safetyhook::Context& ctx)
{
	if (!MemoryHelper::IsExeCode(ctx.eax))
	{
		ctx.eax = 0;
		ctx.eip = FieldF0BuildPrimaryContinue;
	}
}

static void OnFieldF0BuildFallbackDispatch(safetyhook::Context& ctx)
{
	if (!MemoryHelper::IsExeCode(ctx.edx))
	{
		ctx.eax = 0;
		ctx.eip = FieldF0BuildFallbackContinue;
	}
}

void ApplyCrashFixes()
{
	if (!CrashFixes) return;

	// Hash-table race condition (Localize)
	Localize = HookHelper::CreateHook((void*)GetAddress(Addr::Localize), &Localize_Hook);

	// Fix infinite loading screen
	MemoryHelper::MakeNOP(GetAddress(Addr::HashLoop), 2);

	// Unattached-collision panic
	DWORD addr_FixUnattachedCollisionPanic = GetAddress(Addr::FixUnattachedCollisionPanic);
	Unattached_Collision_Skip = addr_FixUnattachedCollisionPanic + 0x1B0B;
	UnattachedCollisionGuard = safetyhook::create_mid(addr_FixUnattachedCollisionPanic + 0x6, OnUnattachedCollision);

	// Hair-curve over-read
	MemoryHelper::WriteMemory<uint8_t>(GetAddress(Addr::FixHairCurveOverRead), 0x73);

	// MMX over-read + spline-prefetch overrun
	MemoryHelper::WriteMemory<uint8_t>(GetAddress(Addr::MmxOverRead), 0x6E);
	MemoryHelper::WriteMemory<uint8_t>(GetAddress(Addr::SplinePrefetchOverrun), 0x73);

	// FaceFX actor teardown: skip the corrupt loop when the container is stale
	FxActorLoopExit = GetAddress(Addr::FaceFxActorLoopExit);
	FxActorLoopGuard = safetyhook::create_mid(GetAddress(Addr::FaceFxActorLoopGuard), OnFxActorLoop);

	// Render-element dispatch: a stale element's garbage vtable corrupts "this"
	DWORD addr_RenderDispatch = GetAddress(Addr::RenderDispatchGuard);
	RenderDispatchContinue = addr_RenderDispatch + 0x9;
	RenderDispatchGuard = safetyhook::create_mid(addr_RenderDispatch, OnRenderElementDispatch);

	DWORD addr_RenderCache = GetAddress(Addr::RenderCacheGuard);
	FieldF0CacheExit = addr_RenderCache + 0x88;
	FieldF0CacheGuard = safetyhook::create_mid(addr_RenderCache, OnFieldF0Cache);

	DWORD addr_FieldF0BuildPrimary = GetAddress(Addr::FieldF0BuildPrimaryDispatch);
	FieldF0BuildPrimaryContinue = addr_FieldF0BuildPrimary + 0x2;
	FieldF0BuildPrimaryGuard = safetyhook::create_mid(addr_FieldF0BuildPrimary, OnFieldF0BuildPrimaryDispatch);

	DWORD addr_FieldF0BuildFallback = GetAddress(Addr::FieldF0BuildFallbackDispatch);
	FieldF0BuildFallbackContinue = addr_FieldF0BuildFallback + 0x2;
	FieldF0BuildFallbackGuard = safetyhook::create_mid(addr_FieldF0BuildFallback, OnFieldF0BuildFallbackDispatch);
}