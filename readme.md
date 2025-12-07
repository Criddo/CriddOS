# Project Title: CriddOS
#### Video Demo:  https://youtu.be/Sd8AhkEkV7c
#### Description:
---INTRODUCTION---
My project is a x86 64-bit operating system with a FAT16 filesystem, which includes a built-in text editor and calculator. The bootloader is written in Assembly, and the kernel, text editor and calculator are written in C. The operating system can run in QEMU (tested and works), and should be able to work on real hardware using a PS2 keyboard (untested).

---OPERATING INSTRUCTIONS---
To build and run the operating system, the following commands can be pasted to the terminal.

```
sudo docker build buildenv -t myos-buildenv
sudo docker run --rm -it -v "$(pwd)":/root/env myos-buildenv
make all
exit
qemu-system-x86_64 -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide
```

---BOOTLOADER---
The bootloader is comprised of four main files named idt64.asm, idt_handlers.asm, main.asm and main64.asm. This is explained below:

main.asm
This is the main bootloader, which implements a Multiboot2-compliant bootloader that prepares the system to enter 64-bit long mode. It begins by validating that it was launched by a proper Multiboot2 loader, then performs checks to ensure the CPU supports CPUID and long-mode operation. After confirming compatibility, it sets up a paging table structure using 2MB pages and enables Physical Address Extension (PAE), activates long mode and full paging via control registers and model-specific registers. The bootloader then loads a 64-bit Global Descriptor Table, switches to the 64-bit code segment, and jumps to the long-mode entry point.

main64.asm - 64-bit Mode Entry Point
This file serves as the entry point after the CPU has been switched into 64-bit long mode. It sets all the segment registers (SS, DS, ES, FS, GS) to null to set up a clean minimal environment and calls the main kernel function to load the kernel.

idt64.asm - IDT Initialisation
This file sets up the interrupt handling system for x86-64 long mode. It remaps the Programmable Interrupt Controller so its interrupts don't conflict with CPU exception vectors - the master PIC is moved to vectors 0x20-0x27 and the slave to 0x28-0x2F. It then configures the interrupt masks so that only the keyboard interrupt (IRQ1, which becomes vector 0x21) is enabled while all other hardware interrupts are blocked. It then builds an IDT (Interrupt Descriptor Table) entry for the keyboard interrupt that points to the keyboard handler function, filling in all 16 bytes of the entry with the handler's address, code segment selector, and appropriate flags. Finally, it loads the IDT into the CPU using the LIDT instruction and re-enables interrupts. This file essentially bridges the gap between hardware interrupts and software interrupt handlers.

idt_handlers.asm - Keyboard Interrupt Handler
This file contains the actual interrupt service routine that executes whenever a keyboard key is pressed or released. When the keyboard interrupt is initiated, this handler first saves all "callee-saved" registers to preserve the state of whatever code was interrupted. It then reads the scancode byte from the keyboard controller's data port (0x60), which indicates which key was pressed. Before calling the C function handle_scancode(), it carefully adjusts the stack pointer to ensure 16-byte alignment as required by the x86-64 calling convention. After the C function processes the scancode, the handler sends an End-Of-Interrupt (EOI) signal to the PIC to acknowledge that the interrupt has been handled, restores all saved registers, and uses the iretq instruction to return control back to the interrupted code with all CPU flags and state intact.


---KERNEL---
The kernel implements a minimalist operating system with three key features: a text editor, a calculator, and a FAT16 filesystem for persistent storage. It runs in VGA text mode and handles keyboard input through interrupt-driven scancode processing. The kernel includes VGA memory manipulation, hardware I/O port communication, interrupt handling and filesystem structure with boot sectors, FAT tables, root directories and cluster chain management. It can allocate and free clusters, and reads or writes files using DOS 8.3 filenames. It includes an ATA/IDE hard disk driver using PIO mode to read and write 512-byte sectors, which enables the FAT16 filesystem to store and retrieve files permanently on disk. Users can type freely at the kernel prompt, then press Ctrl+E to launch the text editor (which can save/load files to disk) or Ctrl+C to launch the calculator for performing computations.

In this case, it is acknowledged that ATA PIO (Programmed Input/Output) comes at a significant cost in performance and efficiency than the more modern Direct Memory Access, however for ease of coding this project, ATA PIO has been chosen. FAT16 was chosen because it offers improvements over the limitations of FAT 12 (e.g. FAT12 can only address 4096 clusters and performance), whilst still being much easier to code than FAT32.


---TEXT EDITOR---
This is a basic text editor using a VGA text mode environment (80x25 characters), lauched from the kernel by pressing Ctrl+E. It provides core editing functionality including text insertion and deletion, the ability to undo/redo, cursor navigation using the arrow keys, handles the tab key by indenting 4 spaces, automatic scrolling, basic file operations for saving and loading documents and clears the screen on exit. It has a true RAM buffer, storing text in memory and not tied to screen positions. The interface displays a title banner and instructions at the top, with the editing area occupying most of the screen and file prompts appearing at the bottom when triggered.

The commands/shortcuts for using the features are Ctrl+S to save, Ctrl+O to open files, Ctrl+Q to quit, Ctrl+Z to undo, and Ctrl+Y to redo. When saving or opening a file, the user is prompted to enter a filename which appears at the bottom of the screen.

---CALCULATOR---
The calculator portion of the operating system, launched from the kernel by pressing Ctrl+C. It is a fixed-point decimal calculator with 6 decimal places of precision (does not use floating-point operations due to the simple kernel). Numbers are stored as integers scaled by 1,000,000. The program handles keyboard scancodes directly and uses callback functions to interface with the kernel for screen drawing operations. The calculator supports basic operations (+,-,* and /), handles parentheses, supports the use of the backspace key and performs the calculation when the user presses enter. Typing a new equation will clear the screen and begin a new calculation. The calculator can be exited with Ctrl+Q.


---MAIN REFERENCE LIST/SOURCES---
"CodePulse" - https://www.youtube.com/@CodePulse and https://github.com/davidcallanan/os-series (Cross compiler, docker and makefile setup, bootloader, Kernel and Input mapping/keyboard)

"Screeck" - https://www.youtube.com/@screeck and https://github.com/screeck/YouTube/tree/main/How_to_write_a_bootloader (Bootloader and Kernel)

OSDev - https://wiki.osdev.org/Creating_an_Operating_System (Cross compiler setup, bootloader, Kernel, filesystem, keyboard input)

Kilo Text Editor - https://viewsourcecode.org/snaptoken/kilo/ and https://github.com/snaptoken/kilo-src/blob/master/kilo.c (Text editor)

ChatGPT - https://chatgpt.com/ (implementation of FAT16 filesystem, text editor and calculator)

Claude.ai - https://claude.ai/ (implementation of FAT16 filesystem, text editor and calculator)

---Youtube Demonstration Link---
https://youtu.be/Sd8AhkEkV7c

