section .multiboot_header
align 8
header_start:
    dd 0xe85250d6 ; signals multiboot2
    dd 0 ; protected mode for i386 architecture
    dd header_end - header_start ; header length
    dd -(0xe85250d6 + 0 + (header_end - header_start)) ; checksum

    ; end tag
    dd 0 ; tag type
    dd 8 ;size of tag
header_end:

section .text
bits 32
global start
extern long_mode_start ;jump to 64-bit assembly code

start:
	mov esp, stack_top ; address of the top of the stack

	call check_multiboot ; check that loaded by multiboot bootloader
	call check_cpuid
	call check_long_mode ; check is in long mode

	call setup_page_tables
	call enable_paging ; Paging (map virtual addresses to pysical addresses)

	lgdt [gdt64.pointer] ; load global descriptor table
	jmp gdt64.code_segment:long_mode_start ; load code segment into code selector

	hlt

check_multiboot:
	cmp eax, 0x36d76289 ; check whether eax value holds the multiboot value
	jne .no_multiboot ; if fails, jump to no multiboot
	ret ; otherwise return
.no_multiboot:
	mov al, "M" ; if no multiboot, use error code M (multiboot)
	jmp error

check_cpuid: ; check whether CPU supports CPU ID
	pushfd ; push flags register onto the stack
	pop eax ; pop it off the stack into the eax register
	mov ecx, eax ; make a copy in the ecx register
	xor eax, 1 << 21 ; flip the ID bit (bit 21) on the eax register
	push eax ; push onto stack
	popfd ; pop it into flags register
	pushfd
	pop eax ; copy flags back onto eax register
	push ecx
	popfd ; transfer value at ecx back into the flags register
	cmp eax, ecx ; if eax and ecx match, means CPU did not allow the bit to be flipped, therefore CPU ID not available
	je .no_cpuid ; jump to no_cpuid label
	ret ; otherwise return
.no_cpuid:
	mov al, "C" ; print C error message
	jmp error ; jump to error instructions

check_long_mode: ; check whether CPU ID supports extended processor info
	mov eax, 0x80000000 ; move this value into eax register
	cpuid
	cmp eax, 0x80000001 ; if eax less than this value
	jb .no_long_mode ; jump to no long mode

	mov eax, 0x80000001 ; check if long mode available
	cpuid
	test edx, 1 << 29 ; if lm bit (bit 29) is set, long mode is available
	jz .no_long_mode
	ret

.no_long_mode:
	mov al, "L" ; print L error message
	jmp error ; jump to error instructions

setup_page_tables: ;identity mapping (mapping physical address to virtual address)
	mov eax, page_table_l3 ; move address of level 3 table into eax
	or eax, 0b11 ; enable present and writable flags by setting first two bits to one
	mov [page_table_l4], eax ; put address into first entry of level 4 table

	mov eax, page_table_l2 ; move address of level 2 table into eax
	or eax, 0b11 ; enable present and writable flags by setting first two bits to one
	mov [page_table_l3], eax ; put address into first entry of level 3 table

	mov ecx, 0 ; counter

.loop:
	mov eax, 0x200000 ; for each iteration of the loop, map a 2MB page
	mul ecx ; multiply the value in eax by the counter (ecx) to give correct address for next page
	or eax, 0b10000011 ; put in present and writable bits, huge page flag
	mov [page_table_l2 + ecx * 8], eax ; put above entry into level 2 table

	inc ecx ; increment counter
	cmp ecx, 512 ; checks if the whole table is mapped
	jne .loop ; if not, continue

	ret

enable_paging:
	; pass page table location to cpu
	mov eax, page_table_l4 ; move address of level 4 table into eax register
	mov cr3, eax ; copy value into cr3 register, because cpu looks for page table location in cr3 register

	; enable physical address extension (PAE)
	mov eax, cr4 ; copy cr4 into eax register
	or eax, 1 << 5 ; enable 5th bit, whhich is the PAE flag
	mov cr4, eax ; save changes back into cr4 register

	; enable long mode
	mov ecx, 0xC0000080 ; put this value into ecx register
	rdmsr ; read model specific register instruction
	or eax, 1 << 8 ; enable long mode flag (bit 8)
	wrmsr ; write model specific register instruction

	; enable paging
	mov eax, cr0
	or eax, 1 << 31 ; enable paging bit (bit 31)
	mov cr0, eax ; move it back to cr0 register

	ret

error:
	; print "ERR: X" where X is the error code
	mov dword [0xb8000], 0x4f524f45
	mov dword [0xb8004], 0x4f3a4f52
	mov dword [0xb8008], 0x4f204f20
	mov byte  [0xb800a], al
	hlt

section .bss
align 4096
page_table_l4: ; reserve memory for page tables, each is 4KB
	resb 4096
page_table_l3:
	resb 4096
page_table_l2:
	resb 4096
stack_bottom:
	resb (4096 * 4)
stack_top:

section .rodata ; read-only data section
gdt64: ; create global descriptor table, which is required to enter 64 bit mode
	dq 0 ; must begin with a zero entry
.code_segment: equ $ - gdt64 ; offset inside the descriptor table, which is current address minus the start of the table
	dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53) ; code segment which enables to executable flag, set descriptor type to 1 for code and data segments, enable present flag and enable 64-bit flag
.pointer: ; pointer to the GDT
	dw $ - gdt64 - 1 ; length is current memory address minus start of table
	dq gdt64 ; store the address of the pointer using the gdt64 label
