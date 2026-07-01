#include "Common.hpp"
#include "Features.hpp"

safetyhook::InlineHook hkGetSystemInfo;
safetyhook::InlineHook hkGetNativeSystemInfo;
safetyhook::InlineHook hkGetProcessAffinityMask;
safetyhook::InlineHook hkGetLogicalProcessorInformation;

static DWORD_PTR MakeProcessorMask(int count)
{
	if (count >= (int)(sizeof(DWORD_PTR) * 8))
		return ~(DWORD_PTR)0;

	return (DWORD_PTR)((1ULL << count) - 1);
}

static void WINAPI GetSystemInfo_Hook(LPSYSTEM_INFO lpSystemInfo)
{
	hkGetSystemInfo.stdcall<void, LPSYSTEM_INFO>(lpSystemInfo);

	if (lpSystemInfo->dwNumberOfProcessors > (DWORD)MaxProcessorCount)
	{
		lpSystemInfo->dwNumberOfProcessors = (DWORD)MaxProcessorCount;
		lpSystemInfo->dwActiveProcessorMask = MakeProcessorMask(MaxProcessorCount);
	}
}

static void WINAPI GetNativeSystemInfo_Hook(LPSYSTEM_INFO lpSystemInfo)
{
	hkGetNativeSystemInfo.stdcall<void, LPSYSTEM_INFO>(lpSystemInfo);

	if (lpSystemInfo->dwNumberOfProcessors > (DWORD)MaxProcessorCount)
	{
		lpSystemInfo->dwNumberOfProcessors = (DWORD)MaxProcessorCount;
		lpSystemInfo->dwActiveProcessorMask = MakeProcessorMask(MaxProcessorCount);
	}
}

static BOOL WINAPI GetProcessAffinityMask_Hook(HANDLE hProcess, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask)
{
	BOOL result = hkGetProcessAffinityMask.stdcall<BOOL, HANDLE, PDWORD_PTR, PDWORD_PTR>(hProcess, lpProcessAffinityMask, lpSystemAffinityMask);

	if (result)
	{
		DWORD_PTR mask = MakeProcessorMask(MaxProcessorCount);
		if (lpProcessAffinityMask)
			*lpProcessAffinityMask &= mask;

		if (lpSystemAffinityMask)
			*lpSystemAffinityMask &= mask;
	}

	return result;
}

static BOOL WINAPI GetLogicalProcessorInformation_Hook(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer, PDWORD ReturnedLength)
{
	BOOL result = hkGetLogicalProcessorInformation.stdcall<BOOL, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD>(Buffer, ReturnedLength);

	if (result && Buffer && ReturnedLength)
	{
		DWORD_PTR mask = MakeProcessorMask(MaxProcessorCount);
		DWORD count = *ReturnedLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		DWORD writeIdx = 0;

		for (DWORD i = 0; i < count; i++)
		{
			Buffer[i].ProcessorMask &= mask;
			if (Buffer[i].ProcessorMask != 0)
			{
				if (writeIdx != i)
				{
					Buffer[writeIdx] = Buffer[i];
				}

				writeIdx++;
			}
		}

		*ReturnedLength = writeIdx * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
	}

	return result;
}

void ApplyFixCPUCores()
{
	if (MaxProcessorCount == -1) return;

	hkGetSystemInfo = HookHelper::CreateHookAPI(L"kernel32.dll", "GetSystemInfo", &GetSystemInfo_Hook);
	hkGetNativeSystemInfo = HookHelper::CreateHookAPI(L"kernel32.dll", "GetNativeSystemInfo", &GetNativeSystemInfo_Hook);
	hkGetProcessAffinityMask = HookHelper::CreateHookAPI(L"kernel32.dll", "GetProcessAffinityMask", &GetProcessAffinityMask_Hook);
	hkGetLogicalProcessorInformation = HookHelper::CreateHookAPI(L"kernel32.dll", "GetLogicalProcessorInformation", &GetLogicalProcessorInformation_Hook);
}
