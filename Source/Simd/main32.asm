global _start

section .rodata
    msg db "Hello from x86 32-bit Assembly!", 10
    msg_len equ $ - msg

section .text
_start:
    ; write(1, msg, msg_len)
    mov eax, 4          ; sys_write
    mov ebx, 1          ; stdout
    mov ecx, msg        ; buffer
    mov edx, msg_len    ; count
    int 0x80

    ; exit(0)
    mov eax, 1          ; sys_exit
    xor ebx, ebx        ; status 0
    int 0x80
