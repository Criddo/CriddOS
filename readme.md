# Project Title: CriddOS
#### Video Demo:  <URL HERE>
#### Description:
---INTRODUCTION---
My project is a 64-bit operating system with a FAT16 filesystem, which includes a built-in text editor and calculator. The bootloader is written in Assembly, and the kernel, text editor and calculator are written in C. The operating system can run in QEMU (tested and works), and should be able to work on real hardware using a PS2 keyboard (untested).

---OPERATING INSTRUCTIONS---
To build and run the operating system, the following commands can be pasted to the terminal.

```
sudo docker build buildenv -t myos-buildenv
sudo docker run --rm -it -v "$(pwd)":/root/env myos-buildenv
make all
exit
qemu-system-x86_64 \
    -cdrom dist/x86_64/kernel.iso \
    -drive file=disk.img,format=raw,if=ide`
```

---BOOTLOADER---
The bootloader is comprised of four main files named idt64.asm, idt_handlers.asm, main.asm and main64.asm. This is explained below:

idt64.asm - IDT Initialisation
This file sets up the interrupt handling system for x86-64 long mode. It remaps the Programmable Interrupt Controller so its interrupts don't conflict with CPU exception vectors - the master PIC is moved to vectors 0x20-0x27 and the slave to 0x28-0x2F. It then configures the interrupt masks so that only the keyboard interrupt (IRQ1, which becomes vector 0x21) is enabled while all other hardware interrupts are blocked. It then builds an IDT (Interrupt Descriptor Table) entry for the keyboard interrupt that points to the keyboard handler function, filling in all 16 bytes of the entry with the handler's address, code segment selector, and appropriate flags. Finally, it loads the IDT into the CPU using the LIDT instruction and re-enables interrupts. This file essentially bridges the gap between hardware interrupts and software interrupt handlers.

idt_handlers.asm - Keyboard Interrupt Handler
This file contains the actual interrupt service routine that executes whenever a keyboard key is pressed or released. When the keyboard interrupt is initiated, this handler first saves all "callee-saved" registers to preserve the state of whatever code was interrupted. It then reads the scancode byte from the keyboard controller's data port (0x60), which indicates which key was pressed. Before calling the C function handle_scancode(), it carefully adjusts the stack pointer to ensure 16-byte alignment as required by the x86-64 calling convention. After the C function processes the scancode, the handler sends an End-Of-Interrupt (EOI) signal to the PIC to acknowledge that the interrupt has been handled, restores all saved registers, and uses the iretq instruction to return control back to the interrupted code with all CPU flags and state intact.

main.asm
...

main64.asm - 64-bit Mode Entry Point
This file serves as the entry point after the CPU has been switched into 64-bit long mode. It sets all the segment registers (SS, DS, ES, FS, GS) to null to set up a clean minimal environment and calls the main kernel function to load the kernel.


---KERNEL---
FAT16 was chosen because...
In this case, it is acknowledged that ATA PIO (Programmed Input/Output) comes at a significant cost in performance and efficiency than the more modern Direct Memory Access, however for ease of coding this project, ATA PIO has been chosen.


---TEXT EDITOR---
The text editor has a true RAM buffer, storing text in memory and not tied to screen positions.
Supports scrolling, and clears when exiting the editor.

---CALCULATOR---


---MAIN REFERENCE LIST/SOURCES---
"CodePulse" - https://www.youtube.com/@CodePulse and https://github.com/davidcallanan/os-series (Cross compiler, docker and makefile setup, bootloader, Kernel and Input mapping/keyboard)

"Screeck" - https://www.youtube.com/@screeck and https://github.com/screeck/YouTube/tree/main/How_to_write_a_bootloader (Bootloader and Kernel)

OSDev - https://wiki.osdev.org/Creating_an_Operating_System (Cross compiler setup, bootloader, Kernel, filesystem, keyboard input)

Kilo Text Editor - https://viewsourcecode.org/snaptoken/kilo/ and https://github.com/snaptoken/kilo-src/blob/master/kilo.c (Text editor)

ChatGPT - https://chatgpt.com/ (implementation of FAT16 filesystem, text editor and calculator)

Claude.ai - https://claude.ai/ (implementation of FAT16 filesystem, text editor and calculator)


---VIDEO TRANSCRIPT---
I won't go into a lot of technical detail in this video as this is covered in the Readme file, instead I will focus in demonstrating the project in action.
