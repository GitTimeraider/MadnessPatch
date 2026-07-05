#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook HairSimulator;
safetyhook::InlineHook ClothSimulator;
static SafetyHookInline RangeAttackPawnCollisionCheck{};
static safetyhook::MidHook hairDeltaTimeOverride{};
static safetyhook::MidHook hairDeltaTimeRestore{};
static safetyhook::MidHook hairGravityAttenuate{};
static safetyhook::MidHook hairWindAttenuate{};
static safetyhook::MidHook hairShapeMatchDecompound{};
static safetyhook::MidHook hairInnerPhaseFix{};
static safetyhook::MidHook walkStationaryAbortX{};
static safetyhook::MidHook walkStationaryAbortY{};
static safetyhook::MidHook walkFloorStick{};
static safetyhook::MidHook walkVelocityRecompute{};

static uintptr_t WalkVelSkipTarget = 0;
static float frameTimeScale = 0.0f;
static float savedHairDeltaTime = 0.0f;

constexpr int CLOTH_MAX_INSTANCES = 32;
constexpr int CLOTH_MAX_PARTICLES = 80;
constexpr int CLOTH_MAX_FLOATS = CLOTH_MAX_PARTICLES * 3;

struct ClothInstanceState
{
	uint32_t lastUsed = 0;
	int numFloats = 0;
	float accumulator = 0.0f;
	bool primed = false;
	float trueP1[CLOTH_MAX_FLOATS];
	float relPrev[CLOTH_MAX_FLOATS];
	float relCurr[CLOTH_MAX_FLOATS];
	float applied[CLOTH_MAX_FLOATS];
};

static uintptr_t clothKeys[CLOTH_MAX_INSTANCES] = {};
static ClothInstanceState clothes[CLOTH_MAX_INSTANCES];
static uint32_t clothCounter = 0;

static void __fastcall HairSimulator_Hook(void* thisPtr, int, float delta)
{
	frameTimeScale = TARGET_FRAME_TIME / delta;
	savedHairDeltaTime = delta;

	HairSimulator.unsafe_thiscall<void>(thisPtr, delta);
}

static uint32_t __fastcall ClothSimulator_Hook(void* thisPtr, int, float delta)
{
	uint8_t* cloth = (uint8_t*)thisPtr;
	int numParticles = *(int*)(cloth + 0xB8);
	uint8_t* particles = *(uint8_t**)(cloth + 0xC4);

	if (!particles || numParticles <= 0)
		return ClothSimulator.unsafe_thiscall<uint32_t>(thisPtr, delta);

	bool bypass = numParticles > CLOTH_MAX_PARTICLES || delta > (1.0f / 59.0f);

	// Dollmaker strings
	if (!bypass && *(int*)(cloth + 0xC0) == 0)
	{
		int constraints = *(int*)(cloth + 0xBC);
		bypass = (numParticles == 15 && constraints == 27) || (numParticles == 20 && constraints == 37);
	}

	uintptr_t key = (uintptr_t)thisPtr;

	if (bypass)
	{
		for (int i = 0; i < CLOTH_MAX_INSTANCES; i++)
		{
			if (clothKeys[i] != key)
				continue;

			ClothInstanceState& state = clothes[i];

			if (state.primed && state.numFloats == numParticles * 3)
			{
				for (int p = 0; p < numParticles; p++)
				{
					float* p1 = (float*)(particles + p * 0x70 + 0x10);

					for (int j = 0; j < 3; j++)
					{
						int k = p * 3 + j;
						if (p1[j] == state.applied[k])
						{
							p1[j] = state.trueP1[k];
						}
					}
				}
			}

			state.primed = false;
			break;
		}

		return ClothSimulator.unsafe_thiscall<uint32_t>(thisPtr, delta);
	}

	int slot = -1;
	for (int i = 0; i < CLOTH_MAX_INSTANCES; i++)
	{
		if (clothKeys[i] == key)
		{
			slot = i;
			break;
		}
	}

	if (slot == -1)
	{
		slot = 0;
		for (int i = 1; i < CLOTH_MAX_INSTANCES; i++)
		{
			if (clothes[i].lastUsed < clothes[slot].lastUsed)
			{
				slot = i;
			}
		}

		clothKeys[slot] = key;
		clothes[slot].numFloats = 0;
	}

	ClothInstanceState& state = clothes[slot];
	state.lastUsed = ++clothCounter;

	int numFloats = numParticles * 3;
	if (state.numFloats != numFloats)
	{
		state.numFloats = numFloats;
		state.primed = false;
	}

	// Restore the simulation-true p1. 
	// If the engine rewrote p1 since we set it (teleport reset, instance respawn), adopt its value and resync instead
	if (state.primed)
	{
		for (int i = 0; i < numParticles; i++)
		{
			float* p1 = (float*)(particles + i * 0x70 + 0x10);
			float* p3 = (float*)(particles + i * 0x70 + 0x30);

			for (int j = 0; j < 3; j++)
			{
				int k = i * 3 + j;
				if (p1[j] == state.applied[k])
				{
					p1[j] = state.trueP1[k];
				}
				else
				{
					state.trueP1[k] = p1[j];
					state.relCurr[k] = p1[j] - p3[j];
					state.relPrev[k] = state.relCurr[k];
				}
			}
		}
	}

	state.accumulator += delta;

	if (!state.primed)
		state.accumulator = TARGET_FRAME_TIME; // first sight of this instance: tick now

	uint32_t result = 0;
	if (state.accumulator >= TARGET_FRAME_TIME)
	{
		memcpy(state.relPrev, state.relCurr, numFloats * sizeof(float));
		result = ClothSimulator.unsafe_thiscall<uint32_t>(thisPtr, TARGET_FRAME_TIME);
		state.accumulator -= TARGET_FRAME_TIME;

		// Capture the post-tick anchor-relative state
		for (int i = 0; i < numParticles; i++)
		{
			float* p1 = (float*)(particles + i * 0x70 + 0x10);
			float* p3 = (float*)(particles + i * 0x70 + 0x30);

			for (int j = 0; j < 3; j++)
			{
				state.relCurr[i * 3 + j] = p1[j] - p3[j];
			}
		}

		if (!state.primed)
		{
			memcpy(state.relPrev, state.relCurr, numFloats * sizeof(float));
			state.primed = true;
		}
	}

	// Write the render state: live anchor + interpolated relative motion
	float alpha = state.accumulator / TARGET_FRAME_TIME;
	const float* matrix = (const float*)cloth;

	for (int i = 0; i < numParticles; i++)
	{
		uint8_t* particle = particles + i * 0x70;
		float* p1 = (float*)(particle + 0x10);
		const float* localAnchor = (const float*)(particle + 0x20);

		for (int j = 0; j < 3; j++)
		{
			// anchor = p2.x * M0 + p2.y * M1 + p2.z * M2 + M3
			float anchor = localAnchor[0] * matrix[j] + localAnchor[1] * matrix[4 + j] + localAnchor[2] * matrix[8 + j] + matrix[12 + j];

			int k = i * 3 + j;
			state.trueP1[k] = p1[j];
			p1[j] = anchor + state.relPrev[k] + (state.relCurr[k] - state.relPrev[k]) * alpha;
			state.applied[k] = p1[j];
		}
	}

	return result;
}

static void __fastcall RangeAttackPawnCollisionCheck_Hook(int thisPtr, float DeltaTime)
{
	DeltaTime = TARGET_FRAME_TIME;
	RangeAttackPawnCollisionCheck.unsafe_fastcall<void>(thisPtr, DeltaTime);
}

static void OnHairDeltaTimeOverride(safetyhook::Context& ctx)
{
	float* dt = reinterpret_cast<float*>(ctx.ebx + 0x8);
	*dt *= frameTimeScale;
}

static void OnHairDeltaTimeRestore(safetyhook::Context& ctx)
{
	float* dt = reinterpret_cast<float*>(ctx.ebx + 0x8);
	*dt = savedHairDeltaTime;
}

static void OnHairGravityAttenuate(safetyhook::Context& ctx)
{
	float k = 1.0f / frameTimeScale;
	ctx.xmm2.f32[0] *= k;
	ctx.xmm2.f32[1] *= k;
	ctx.xmm2.f32[2] *= k;
}

static void OnHairWindAttenuate(safetyhook::Context& ctx)
{
	float k = 1.0f / frameTimeScale;
	ctx.xmm6.f32[0] *= k;
	ctx.xmm6.f32[1] *= k;
	ctx.xmm6.f32[2] *= k;
}

static void OnHairShapeMatchDecompound(safetyhook::Context& ctx)
{
	float f30 = ctx.xmm3.f32[0];
	float scale = frameTimeScale;
	float remain = 1.0f - f30;
	if (remain < 0.0f) remain = 0.0f;
	float fNew = 1.0f - std::pow(remain, 1.0f / scale);
	ctx.xmm3.f32[0] = fNew;
}

static void OnHairInnerPhaseFix(safetyhook::Context& ctx)
{
	float* innerDt = reinterpret_cast<float*>(ctx.ebx + 0x8);
	*innerDt = savedHairDeltaTime;
}

void ApplyFixHighFPSHairPhysics()
{
	if (!FixHighFPSHairPhysics) return;

	DWORD addr_DampingScaler = GetAddress(Addr::HairSimulator_DampingScaler);
	DWORD addr_DeltaTimeOverride = GetAddress(Addr::HairSimulator_DeltaTimeOverride);

	HairSimulator = HookHelper::CreateHook((void*)GetAddress(Addr::HairSimulator), &HairSimulator_Hook);
	hairDeltaTimeOverride = safetyhook::create_mid(addr_DeltaTimeOverride, OnHairDeltaTimeOverride);
	hairDeltaTimeRestore = safetyhook::create_mid(addr_DeltaTimeOverride + 0x8, OnHairDeltaTimeRestore);
	hairGravityAttenuate = safetyhook::create_mid(addr_DampingScaler, OnHairGravityAttenuate);
	hairWindAttenuate = safetyhook::create_mid(addr_DampingScaler + 0x6E, OnHairWindAttenuate);
	hairShapeMatchDecompound = safetyhook::create_mid(addr_DampingScaler + 0x179, OnHairShapeMatchDecompound);
	hairInnerPhaseFix = safetyhook::create_mid(addr_DampingScaler + 0x4B2, OnHairInnerPhaseFix);
}

void ApplyFixHighFPSClothPhysics()
{
	if (!FixHighFPSClothPhysics) return;

	ClothSimulator = HookHelper::CreateHook((void*)GetAddress(Addr::ClothSimulator), &ClothSimulator_Hook);
}

void ApplyFixHighFPSProjectileCollisionCheck()
{
	if (!FixHighFPSProjectileCollisionCheck) return;

	RangeAttackPawnCollisionCheck = HookHelper::CreateHook((void*)GetAddress(Addr::RangeAttackPawnCollisionCheck), &RangeAttackPawnCollisionCheck_Hook);
}

void ApplyFixHighFPSRagdollDeath()
{
	if (!FixHighFPSRagdollDeath) return;

	MemoryHelper::MakeNOP(GetAddress(Addr::RagdollDeath), 0x18);
}

static void OnWalkStationaryAbortX(safetyhook::Context& ctx)
{
	*reinterpret_cast<float*>(ctx.ebp - 0x194) = *reinterpret_cast<float*>(ctx.ebp - 0x12C) * TARGET_FRAME_TIME; // [ebp-12Ch] = Velocity.X
}

static void OnWalkStationaryAbortY(safetyhook::Context& ctx)
{
	*reinterpret_cast<float*>(ctx.ebp - 0x198) = *reinterpret_cast<float*>(ctx.ebp - 0x128) * TARGET_FRAME_TIME; // [ebp-128h] = Velocity.Y
}

static void OnWalkFloorStick(safetyhook::Context& ctx)
{
	float dt = *reinterpret_cast<float*>(ctx.ebx + 0x8);
	float s = TARGET_FRAME_TIME / dt;
	float* v = reinterpret_cast<float*>(ctx.ebp - 0x278);
	*v *= s * s; // v304 is proportional to dt^2, so scale by ((1/30)/dt)^2
}

static void OnWalkVelocityRecompute(safetyhook::Context& ctx)
{
	if (*reinterpret_cast<float*>(ctx.ebx + 0x8) < TARGET_FRAME_TIME)
		ctx.eip = WalkVelSkipTarget; // -> "Velocity.Z = 0" xmm4 is already zero on this path
}

void ApplyFixHighFPSWalkingPhysics()
{
	if (!FixHighFPSWalkingPhysics) return;

	WalkVelSkipTarget = GetAddress(Addr::WalkVelocityRecomputeSkip);
	walkStationaryAbortX = safetyhook::create_mid(GetAddress(Addr::WalkStationaryAbortX), OnWalkStationaryAbortX);
	walkStationaryAbortY = safetyhook::create_mid(GetAddress(Addr::WalkStationaryAbortY), OnWalkStationaryAbortY);
	walkFloorStick = safetyhook::create_mid(GetAddress(Addr::WalkFloorStick), OnWalkFloorStick);
	walkVelocityRecompute = safetyhook::create_mid(GetAddress(Addr::WalkVelocityRecompute), OnWalkVelocityRecompute);
}
