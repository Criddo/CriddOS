; Initialise IDT in long mode, remap PIC and install keyboard ISR (vector 0x21)
bits 64
global init_idt64
extern keyboard_isr64

section .bss
align 16
idt_table:    ; Reserve space for 256 IDT entries (each entry is 16 bytes)
    resb 256 * 16

section .data
align 16
idtr:                 ; Set IDT Register structure
    dw 0              ; limit (filled by code)
    dq 0              ; base (filled by code)

section .text
init_idt64:
    cli                         ; disable interrupts during setup

    ; ---------------------
    ; Remap Programmable Interrupt Controllers (PICs) to Master PIC - 0x20-0x27, Slave PIC - 0x28-0x2F
    ; ---------------------
    ; Send initialisation command (ICW1) to both PICs
    mov al, 0x11 ; 0x11 = begin initialization sequence (cascade mode)
    out 0x20, al ; Send to master PIC command port
    out 0xA0, al ; Send to slave PIC command port

    ; Set vector offset (ICW2) - where IRQs start in the interrupt table
    mov al, 0x20 ; Master PIC starts at interrupt 0x20 (32)
    out 0x21, al ; Send to master PIC data port
    mov al, 0x28 ; Slave PIC starts at interrupt 0x28 (40)
    out 0xA1, al ; Send to slave PIC data port

    ; Configure PIC cascade (ICW3) - how master and slave are connected
    mov al, 0x04 ; Tell master that slave is on IRQ2
    out 0x21, al ; Send to master PIC data port
    mov al, 0x02 ; Tell slave its cascade identity is 2
    out 0xA1, al ; Send to slave PIC data port

    ; Set mode (ICW4) - 8086/88 mode
    mov al, 0x01 ; 0x01 = 8086/88 mode
    out 0x21, al ; Send to master PIC data port
    out 0xA1, al ; Send to slave PIC data port

    ; Configure interrupt masks (OCW1) - which IRQs are enabled
    mov al, 0xFD
    out 0x21, al ; Unmask only IRQ1 (keyboard) on master PIC, mask all others
    mov al, 0xFF
    out 0xA1, al ;  Mask all interrupts on slave PIC (disable all)

    ; ---------------------
    ; Build IDT entry for interrupt vector 0x21 (keyboard IRQ1)
    ; ---------------------
    lea rax, [rel keyboard_isr64]    ; Get address of keyboard interrupt handler

    ; Store bits 0-15 of handler address in bytes 0-1
    mov word [idt_table + 0x21 * 16 + 0], ax

    ; Store code segment selector in bytes 2-3. Use kernel code selector 0x08 (GDT entry 1)
    mov word [idt_table + 0x21 * 16 + 2], 0x0008

    ; Store IST index in byte 4
    mov byte [idt_table + 0x21 * 16 + 4], 0 ; Don't use Interrupt Stack Table, set to zero

    ; Store type/attributes in byte 5 - 64-bit interrupt gate, present (0x8E)
    mov byte [idt_table + 0x21 * 16 + 5], 0x8E

    ; Store bits 16-31 of handler address in bytes 6-7
    mov rbx, rax ; Copy full address to rbx
    shr rbx, 16 ; Shift right 16 bits to get bits 16-31
    mov word [idt_table + 0x21 * 16 + 6], bx

    ; Store bits 32-63 of handler address in bytes 8-11
    mov rbx, rax ; Copy full address again
    shr rbx, 32 ; Shift right 32 bits to get bits 32-63
    mov dword [idt_table + 0x21 * 16 + 8], ebx

    ; Clear reserved bytes 12-15
    mov dword [idt_table + 0x21 * 16 + 12], 0

    ; ---------------------
    ; Load IDT Register
    ; ---------------------
    mov word [idtr], 256 * 16 - 1 ; Set the limit of the IDT table. 256 entries, 16 bytes each minus 1
    lea rax, [rel idt_table] ; Get address of IDT
    mov qword [idtr + 2], rax ; Store it in the IDTR structure
    lidt [idtr] ; Tell CPU where IDT is located

    sti ; Re-enable interrupts now that setup is complete
    ret ; Return to caller
