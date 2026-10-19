global _start
extern main

section .text
_start:
    call main
    ; main returned: terminate the process (syscall 10, exit)
    mov ebx, eax
    mov eax, 10
    int 0x30
    jmp $
