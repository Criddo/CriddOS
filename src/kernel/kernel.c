#include "print.h"
#include "kernel.h"

void kernel_main() {
    print_clear(); //clears the screen
    print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK); //change foreground and background colours
    print_str("Welcome to the 64-bit kernel!"); //the text to print
}
