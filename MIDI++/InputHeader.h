#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

	// Global syscall number (defined in implementation file)
	extern DWORD SyscallNumber;

	// Returns the NtUserSendInput syscall number
	unsigned long __cdecl GetNtUserSendInputSyscallNumber(void);

	// Direct function pointer - maximum speed
	// This replaces the regular function declaration for ultra-fast access
	extern UINT(__fastcall* NtUserSendInputCall)(ULONG cInputs, LPINPUT pInputs, int cbSize);

	// Initialize the direct syscall - call after setting SyscallNumber
	void InitializeNtUserSendInputCall(void);

#ifdef __cplusplus
}
#endif