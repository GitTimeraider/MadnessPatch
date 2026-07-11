#include "Common.hpp"
#include "Features.hpp"

static SafetyHookInline RangeAttackPawnCollisionCheck{};

static void __fastcall RangeAttackPawnCollisionCheck_Hook(int thisPtr, float DeltaTime)
{
	DeltaTime = TARGET_FRAME_TIME;
	RangeAttackPawnCollisionCheck.unsafe_fastcall<void>(thisPtr, DeltaTime);
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

// ---- Hair ----

safetyhook::InlineHook HairSimulator;
static safetyhook::MidHook hairDeltaTimeOverride{};
static safetyhook::MidHook hairDeltaTimeRestore{};
static safetyhook::MidHook hairGravityAttenuate{};
static safetyhook::MidHook hairWindAttenuate{};
static safetyhook::MidHook hairShapeMatchDecompound{};
static safetyhook::MidHook hairInnerPhaseFix{};

static float frameTimeScale = 0.0f;
static float savedHairDeltaTime = 0.0f;

static void __fastcall HairSimulator_Hook(void* thisPtr, int, float delta)
{
	frameTimeScale = TARGET_FRAME_TIME / delta;
	savedHairDeltaTime = delta;

	HairSimulator.unsafe_thiscall<void>(thisPtr, delta);
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

// ---- Cloth ----

safetyhook::InlineHook ClothSimulator;

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

void ApplyFixHighFPSClothPhysics()
{
	if (!FixHighFPSClothPhysics) return;

	ClothSimulator = HookHelper::CreateHook((void*)GetAddress(Addr::ClothSimulator), &ClothSimulator_Hook);
}

// ---- Walking ----

static safetyhook::MidHook WalkFloorStick{};
static safetyhook::MidHook WalkVelocityRecompute{};
static safetyhook::MidHook WalkFloorAccepted{};
static safetyhook::MidHook WalkFallGate{};
static safetyhook::MidHook WalkSetBaseGuard{};
static safetyhook::MidHook FallIntegratedVel{};
static safetyhook::MidHook FallVelRecompute{};
static safetyhook::MidHook FallLandVelGate{};

static uintptr_t WalkVelSkipTarget = 0;
static uintptr_t WalkFallGateResume = 0;
static uintptr_t WalkSetBaseSkip = 0;
static uintptr_t FallLandVelTake = 0;

constexpr float FloorGraceVoid = 0.05f;
constexpr float FloorGraceContact = 0.15f;
constexpr float FallRestoreCap = -80.0f;

constexpr float PhantomFallMaxSeconds = 0.5f;
constexpr float PhantomFallMaxDrop = 60.0f;
constexpr uint64_t PhantomNeedsFireMs = 1500;
constexpr uint64_t GateSuppressMs = 600;
constexpr uint64_t GateSuppressCapMs = 4000;

constexpr float BudgetStaleSeconds = 0.1f;

constexpr float DesignFrameCutoff = TARGET_FRAME_TIME * 0.9f;
constexpr float WindowNoiseFloor = 0.3f;
constexpr float WindowObstructedRatio = 0.55f;
constexpr float ObstructedExitCos = 0.57f;
constexpr float ObstructedFreeSpeed = 200.0f;
constexpr float ObstructedGoneRatio = 0.98f;
constexpr float GoodMoveMinExp = 1.0f;
constexpr uint64_t GoodMoveGraceMs = 600;

struct FloorState
{
	float badTime = 0.0f;
	float voidTime = 0.0f;
	FVector lastGood;
	uint64_t suppressUntil = 0;
	uint64_t suppressArmedAt = 0;
	float budgetStale = 0.0f;
	FVector frameLoc;
	bool frameLocValid = false;
	float winTime = 0.0f;
	float winDx = 0.0f, winDy = 0.0f, winExp = 0.0f;
	bool obstructed = false;
	float holdX = 0.0f, holdY = 0.0f;
	FVector obstructedDir;
	uint64_t lastBudgetPinnedAt = 0;
	uint64_t lastGoodMoveAt = 0;

	FloorState() { lastGood.Z = 1.0f; }
};
static std::unordered_map<uintptr_t, FloorState> floorStates;

static bool fallEpisode = false;
static uint64_t fallEpisodeStart = 0;
static uintptr_t fallPawn = 0;
static FVector fallIntVel;
static float fallBeginZ = 0.0f;
static uint64_t lastGateFireAt = 0;

static void OnWalkFloorStick(safetyhook::Context& ctx)
{
	float dt = *reinterpret_cast<float*>(ctx.ebx + 0x8);
	float s = TARGET_FRAME_TIME / dt;
	float* v = reinterpret_cast<float*>(ctx.ebp - 0x278);
	*v *= s; // the nudge is per-frame dt^2, rescale once to the 30fps design frame
}

static void OnWalkVelocityRecompute(safetyhook::Context& ctx)
{
	float dt = *reinterpret_cast<float*>(ctx.ebx + 0x8);
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.edi);
	FloorState& fs = floorStates[ctx.edi];

	if (dt >= DesignFrameCutoff)
	{
		fs.frameLocValid = false;
		return;
	}

	float dx = pawn->Location.X - fs.frameLoc.X;
	float dy = pawn->Location.Y - fs.frameLoc.Y;
	bool first = !fs.frameLocValid;
	fs.frameLoc = pawn->Location;
	fs.frameLocValid = true;

	float expected = sqrtf(pawn->Velocity.X * pawn->Velocity.X + pawn->Velocity.Y * pawn->Velocity.Y) * dt;

	float vbx = pawn->Velocity.X, vby = pawn->Velocity.Y;

	if (first)
	{
		fs.winTime = 0.0f;
		fs.winDx = 0.0f; fs.winDy = 0.0f; fs.winExp = 0.0f;
		fs.obstructed = false;
	}
	else
	{
		fs.winTime += dt;
		fs.winDx += dx;
		fs.winDy += dy;
		fs.winExp += expected;

		// Classify at the design timescale, where contact jitter divides away
		if (fs.winTime >= TARGET_FRAME_TIME)
		{
			float winMoved = sqrtf(fs.winDx * fs.winDx + fs.winDy * fs.winDy);
			float winSpeed = winMoved / fs.winTime;
			if (fs.winExp >= GoodMoveMinExp && winMoved >= fs.winExp * WindowObstructedRatio)
			{
				fs.lastGoodMoveAt = GetTickCount64();
			}

			if (GetTickCount64() - fs.lastBudgetPinnedAt < 250 && GetTickCount64() - fs.lastGoodMoveAt < GoodMoveGraceMs)
			{
				fs.obstructed = false;
			}
			else if (!fs.obstructed)
			{
				if (fs.winExp >= WindowNoiseFloor && winMoved < fs.winExp * WindowObstructedRatio)
				{
					fs.obstructed = true;
					fs.holdX = fs.winDx / fs.winTime;
					fs.holdY = fs.winDy / fs.winTime;
					float sp = sqrtf(vbx * vbx + vby * vby);

					if (sp > 1.0f)
					{
						fs.obstructedDir.X = vbx / sp;
						fs.obstructedDir.Y = vby / sp;
					}
				}
			}
			else if (winSpeed >= ObstructedFreeSpeed || (fs.winExp >= 0.15f && winMoved >= fs.winExp * ObstructedGoneRatio))
			{
				fs.obstructed = false; // genuinely moving again, or the obstruction is gone
			}
			else
			{
				fs.holdX = fs.winDx / fs.winTime;
				fs.holdY = fs.winDy / fs.winTime;
			}

			fs.winTime = 0.0f;
			fs.winDx = 0.0f; fs.winDy = 0.0f; fs.winExp = 0.0f;
		}
	}

	// steering away from the obstruction releases the hold instantly
	if (fs.obstructed)
	{
		float ax = pawn->Acceleration.X, ay = pawn->Acceleration.Y;
		float a2 = ax * ax + ay * ay;
		if (a2 > 1.0f)
		{
			float inv = 1.0f / sqrtf(a2);
			if (ax * inv * fs.obstructedDir.X + ay * inv * fs.obstructedDir.Y < ObstructedExitCos)
			{
				fs.obstructed = false;
			}
		}
	}

	if (fs.obstructed)
	{
		pawn->Velocity.X = fs.holdX;
		pawn->Velocity.Y = fs.holdY;
	}

	ctx.eip = WalkVelSkipTarget; // "Velocity.Z = 0"
}

static void ClearStaleStepUpBudget(AAlicePawn* pawn, FloorState& fs, float tick)
{
	// physWalking can skip the per-substep fStepUpAccumZ reset, and the residue pins stepUp against near-vertical hits indefinitely
	// Drop residue that outlives a few frames
	if (pawn->fStepUpAccumZ > pawn->fStepUpBugZ)
	{
		uint64_t now = GetTickCount64();
		fs.lastBudgetPinnedAt = now;
		fs.budgetStale += tick;

		// Residue during locomotion is stale, a pinned budget while she grinds in place is the anti-climb lock on jump geometry and stays
		if (fs.budgetStale >= BudgetStaleSeconds && now - fs.lastGoodMoveAt < GoodMoveGraceMs)
		{
			pawn->fStepUpAccumZ = 0.0f;
			fs.budgetStale = 0.0f;
		}
	}
	else
	{
		fs.budgetStale = 0.0f;
	}
}

static void OnWalkSetBase(safetyhook::Context& ctx)
{
	if (*reinterpret_cast<uintptr_t*>(ctx.ebp - 0xDC) == 0) // [ebp-0DCh] = Hit.Actor
	{
		ctx.eip = WalkSetBaseSkip;
	}
}

static void OnWalkFloorAccepted(safetyhook::Context& ctx)
{
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.edi);

	if (floorStates.size() > 64) floorStates.clear();

	FloorState& fs = floorStates[ctx.edi];
	fs.badTime = 0.0f;
	fs.voidTime = 0.0f;
	fs.lastGood = pawn->Floor;

	ClearStaleStepUpBudget(pawn, fs, *reinterpret_cast<float*>(ctx.ebp - 0x38)); // [ebp-38h] = timeTick

	if (fallEpisode)
	{
		fallEpisode = false;
		uint64_t now = GetTickCount64();
		float dur = (now - fallEpisodeStart) / 1000.0f;
		float dz = pawn->Location.Z - fallBeginZ;

		pawn->fStepUpAccumZ = 0.0f;
		fs.budgetStale = 0.0f;
		fs.frameLocValid = false;
		fs.obstructed = false;
		fs.winTime = 0.0f;
		fs.winDx = 0.0f; fs.winDy = 0.0f; fs.winExp = 0.0f;

		if (now - lastGateFireAt < PhantomNeedsFireMs && dur < PhantomFallMaxSeconds && dz > -PhantomFallMaxDrop)
		{
			fs.suppressUntil = now + GateSuppressMs;
			fs.suppressArmedAt = now;
		}
	}
}

static void OnWalkFallGate(safetyhook::Context& ctx)
{
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.edi);
	float tick = *reinterpret_cast<float*>(ctx.ebp - 0x38); // [ebp-38h] = timeTick
	float hitTime = *reinterpret_cast<float*>(ctx.ebp - 0xD8); // [ebp-0D8h] = floor-probe Hit.Time
	bool voidBelow = hitTime >= 1.0f;

	if (floorStates.size() > 64) floorStates.clear();

	FloorState& fs = floorStates[ctx.edi];

	ClearStaleStepUpBudget(pawn, fs, tick);

	fs.badTime += tick;
	if (voidBelow)
	{
		fs.voidTime += tick;
	}
	else
	{
		fs.voidTime = 0.0f;
	}

	bool voidFire = fs.voidTime >= FloorGraceVoid;
	bool contactFire = fs.badTime >= FloorGraceContact;

	uint64_t now = GetTickCount64();

	if (now < fs.suppressUntil && !voidFire)
	{
		if (now - fs.suppressArmedAt < GateSuppressCapMs)
		{
			fs.suppressUntil = now + GateSuppressMs;
		}

		pawn->Floor = fs.lastGood;
		ctx.eip = WalkFallGateResume;
		return;
	}

	if (!voidFire && !contactFire)
	{
		pawn->Floor = fs.lastGood;
		ctx.eip = WalkFallGateResume;
		return;
	}

	lastGateFireAt = now;
	fs.badTime = 0.0f;
	fs.voidTime = 0.0f;
}

static void OnFallIntegrated(safetyhook::Context& ctx)
{
	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.esi);

	fallPawn = ctx.esi;
	fallIntVel = pawn->Velocity;

	if (!fallEpisode)
	{
		fallEpisode = true;
		fallEpisodeStart = GetTickCount64();
		fallBeginZ = pawn->Location.Z;
	}
}

static void OnFallVelRecompute(safetyhook::Context& ctx)
{
	if (ctx.esi != fallPawn) return;

	AAlicePawn* pawn = reinterpret_cast<AAlicePawn*>(ctx.esi);
	float dz = pawn->Location.Z - *reinterpret_cast<float*>(ctx.ebp - 0x9C); // [ebp-9Ch] = OldLocation.Z
	float tick = *reinterpret_cast<float*>(ctx.ebp - 0x24); // [ebp-24h] = timeTick
	float intZ = fallIntVel.Z;

	if (tick < 0.012f && fabsf(dz) < 0.05f && intZ < 0.0f && pawn->Velocity.Z > 0.5f * intZ)
	{
		pawn->Velocity.Z = intZ < FallRestoreCap ? FallRestoreCap : intZ;
	}
}

static void OnFallLandVelGate(safetyhook::Context& ctx)
{
	if (ctx.xmm4.f32[0] > 1e-7f)
	{
		ctx.eip = FallLandVelTake;
	}
}

void ApplyFixHighFPSWalkingPhysics()
{
	if (!FixHighFPSWalkingPhysics) return;

	WalkVelSkipTarget = GetAddress(Addr::WalkVelocityRecomputeSkip);
	WalkFallGateResume = GetAddress(Addr::WalkFallGateResume);
	WalkSetBaseSkip = GetAddress(Addr::WalkSetBaseSkip);
	FallLandVelTake = GetAddress(Addr::FallLandVelTake);
	WalkFloorStick = safetyhook::create_mid(GetAddress(Addr::WalkFloorStick), OnWalkFloorStick);
	WalkVelocityRecompute = safetyhook::create_mid(GetAddress(Addr::WalkVelocityRecompute), OnWalkVelocityRecompute);
	WalkFloorAccepted = safetyhook::create_mid(GetAddress(Addr::WalkFloorAccepted), OnWalkFloorAccepted);
	WalkFallGate = safetyhook::create_mid(GetAddress(Addr::WalkFallGate), OnWalkFallGate);
	WalkSetBaseGuard = safetyhook::create_mid(GetAddress(Addr::WalkSetBaseGuard), OnWalkSetBase);
	FallIntegratedVel = safetyhook::create_mid(GetAddress(Addr::FallIntegrated), OnFallIntegrated);
	FallVelRecompute = safetyhook::create_mid(GetAddress(Addr::FallVelRecompute), OnFallVelRecompute);
	FallLandVelGate = safetyhook::create_mid(GetAddress(Addr::FallLandVelGate), OnFallLandVelGate);
}