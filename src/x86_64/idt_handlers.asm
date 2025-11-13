bits 64
global keyboard_isr64
extern handle_scancode

section .text

keyboard_isr64: ; Save registers that must be preserved across function calls
    ; Save "callee-saved" registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Read the keyboard scancode from the keyboard controller
    in al, 0x60 ; Read one byte from I/O port 0x60 (keyboard data port)
    movzx rdi, al        ; Zero-extend AL to RDI (first argument for function call)

    ; Align stack to 16-bytes before a call instruction
    sub rsp, 8 ; Adjust stack pointer down by 8 bytes for alignment

    call handle_scancode ; Call C function: void handle_scancode(uint8_t scancode). The scancode is already in RDI (first argument)
    add rsp, 8 ; If remainder is 8, skip

    ; Send End-Of-Interrupt (EOI) signal to PIC
    mov al, 0x20 ; 0x20 is the EOI command
    out 0x20, al ; Send EOI to master PIC command port (0x20)

    ; Restore the callee-saved registers saved at the beginning. Restore in reverse order (last in first out)
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    iretq ; Interrupt return (64-bit)
