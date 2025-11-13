global long_mode_start ; make global so main.asm can access this file
extern kernel_main

section .text
bits 64
long_mode_start: ; set all segment registers to null
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax


    call kernel_main
	hlt
