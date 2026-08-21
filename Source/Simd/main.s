.intel_syntax noprefix
.global _start

.section .rodata
msg:
    .ascii "Hello from GAS x86_64!\n"
    msg_len = . - msg

.section .text
_start:
    mov rax, 1          # sys_write
    mov rdi, 1          # stdout
    lea rsi, [msg]      # buffer
    mov rdx, msg_len    # count
    syscall

    mov rax, 60         # sys_exit
    xor rdi, rdi        # status = 0
    syscall
