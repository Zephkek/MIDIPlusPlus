#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

	// Global syscall number (defined in NtUserSendInputSyscall.cpp).
	extern DWORD SyscallNumber;

	// Returns the NtUserSendInput syscall number by decoding one of several DLL exports.
	unsigned long __cdecl GetNtUserSendInputSyscallNumber(void);

	// The assembly routine for performing the syscall.
	UINT __fastcall NtUserSendInputCall(ULONG cInputs, LPINPUT pInputs, int cbSize);

#ifdef __cplusplus
}
#endif
