.model flat, C
.code

EXTERN SyscallNumber:DWORD
NtUserSendInputCall PROC
    ; Load syscall number directly
    mov eax, SyscallNumber
    
    ; win syscall convention requires first parameter in R10
    mov r10, rcx
    
    ; Execute syscall
    syscall
    
    ; Return value is already in RAX
    ret
NtUserSendInputCall ENDP

END