global start

org 0x7c00 ; set the starting address of this code to where the boot sector is stored
[BITS 16] ; start in 16 bit mode

CODE_OFFSET equ 0x8 ; offset of code segment
DATA_OFFSET equ 0x10 ; offset of data segment

KERNEL_LOAD_SEG equ 0x1000
KERNEL_START_ADDR equ 0x100000

start:
    cli ; clear interrupts so it doesn't interrupt setting the settings
    mov ax, 0x00 ; initalise data segment, setup intemediary register
    mov ds, ax  ; set data segment to 0x00
    mov es, ax ; set extra segment to 0x00
    mov ss, ax ; set stack segment to 0x00
    mov sp, 0x7C00 ; set stack pointer to the top of the bootloader segment
    sti ; enable interrupts

load_pm: ; load protected mode
    cli ; clear all interrupts
    lgdt[gdt_descriptor] ; load gdt
    mov eax, cr0 ; set bit 0 in the control register, which enables protected mode, to 1
    or al, 1
    mov cr0, eax
    jmp CODE_OFFSET:pm_main:

; implement global descriptor table (GDT)
gdt_start:
    dd 0x0
    dbd0x0

    ; implement code segment descriptor
    dw 0xFFFF ;specifies the size (limit) of the segment
    dw 0x0000 ; base low
    db 0x00 ; base middle
    db 10011010b ; access byte (present bit, descriptor privilege level, descrptor type, etc). B for binary.
    db 11001111b ; flags (granularity flag, size flag, long-mode code flag)
    db 0x00 ; base high

    ; implement data segment descriptor
    dw 0xFFFF ;specifies the size (limit) of the segment
    dw 0x0000 ; base low
    db 0x00 ; base middle
    db 10010010b ; access byte (same as before, but executable bit changed).
    db 11001111b ; flags (granularity flag, size flag, long-mode code flag)
    db 0x00 ; base high

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1 ; size of GDT -1
    dd gdt_start ; base address of GDT

[BITS 32]
pm_main:
    mov ax, DATA_OFFSET
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov ss, ax
    mov gs, ax
    mov ebp, 0x9C00 ; a location in memory where the stack will not overflow the bootloader code
    mov esp, ebp

    ; enable memory to be accessed beyond 1MB 820
    in al, 0x92
    or al, 2
    out 0x92, al
    jmp $


; ------------------------------
; To go to 64-bit mode (still to do)
; ------------------------------

; enable PAE (required for long mode)

; Set up page tables for long mode

; Enable Long Mode in EFER MSR

; Enable paging

; jump into 64-bit long mode
; jmp 0x08:long_mode_start

times 510-($-$$) db 0 ; fill with 510 zeros minus the size of the previous code, which is worked out by the $ signs

dw 0AA55h ; identifies the code as bootable

; To compile and run, paste the following codes in the Terminal:
; nasm -f bin main.asm -o main.bin
; qemu-system-x86_64 main.bin
