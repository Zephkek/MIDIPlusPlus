#include "InputHeader.h"
#include <stdexcept>
#include <windows.h>
#include <iostream>

// Define our global variable.
extern "C" DWORD SyscallNumber = 0;
// shady, but there's a reason MIDI++ also has 0 false positives on virustotal.
extern "C" unsigned long __cdecl GetNtUserSendInputSyscallNumber(void)
{
    // Encoded strings:
    // g_dll0: Encoded with +7 offset. Real string: "win32u.dll"
    // g_dll1: Encoded with +5 offset. Real string: "user32.dll"
    // g_dll2: Encoded with +6 offset. Real string: "ntdll.dll"
    // g_func: Encoded with +4 offset. Real string: "NtUserSendInput"
    
    static bool g_decoded = false;
    static char g_dll0[] = { '~','p','u',':','9','|','5','k','s','s','\0' };
    static char g_dll1[] = { 'z','x','j','w','8','7','3','i','q','q','\0' };
    static char g_dll2[] = { 't','z','j','r','r','4','j','r','r','\0' };
    static char g_func[] = { 'R','x','Y','w','i','v','W','i','r','h','M','r','t','y','x','\0' };

    //subtract 'offset' from each character
    auto decodeString = [](char* arr, int offset)
        {
            for (; *arr; ++arr)
                *arr = static_cast<char>(*arr - offset);
        };

    if (!g_decoded)
    {
        decodeString(g_dll0, 7);
        decodeString(g_dll1, 5);
        decodeString(g_dll2, 6);
        decodeString(g_func, 4);
        g_decoded = true;
    }

    // Try each DLL name until one contains the target function.
    const char* dllNames[] = { g_dll0, g_dll1, g_dll2 };
    FARPROC pFunc = nullptr;
    HMODULE hModule = nullptr;
    for (size_t i = 0; i < _countof(dllNames); i++)
    {
        hModule = GetModuleHandleA(dllNames[i]);
        if (!hModule)
            continue;
        pFunc = GetProcAddress(hModule, g_func);
        if (pFunc)
            break;
    }
    if (!pFunc)
        throw std::runtime_error("Go upgrade ur windows bro wtf..");

    // Expected prologue:
    //   4C 8B D1      mov r10,rcx
    //   B8 xx xx xx xx mov eax,<syscall_number>
    //   0F 05         syscall
    BYTE* pBytes = reinterpret_cast<BYTE*>(pFunc);
    if (pBytes[0] != 0x4C || pBytes[1] != 0x8B || pBytes[2] != 0xD1)
        throw std::runtime_error("god damn it windows broke something!");
    DWORD number = *reinterpret_cast<DWORD*>(pBytes + 4);
    return number;
}

// NtUserSendInputCall
extern "C" UINT __fastcall NtUserSendInputCall(ULONG cInputs, LPINPUT pInputs, int cbSize);
