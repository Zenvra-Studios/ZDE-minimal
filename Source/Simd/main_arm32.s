.syntax unified
.arch armv7-a
.global _start

.section .rodata
msg:
    .ascii "Hello from ARM 32-bit!\n"
    msg_len = . - msg

.section .text
_start:
    @ write(1, msg, msg_len)
    mov r0, #1          @ stdout
    ldr r1, =msg        @ buffer
    mov r2, #msg_len    @ count
    mov r7, #4          @ sys_write
    svc #0

    @ exit(0)
    mov r0, #0          @ status
    mov r7, #1          @ sys_exit
    svc #0
