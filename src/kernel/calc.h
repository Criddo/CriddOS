/* calc.h */
#ifndef CALC_H
#define CALC_H

#include <stdint.h>
#include <stddef.h>

/* Callback functions for calculator to interact with kernel */
typedef struct {
    void (*clear_screen)(void);
    void (*draw_char)(size_t row, size_t col, char c, uint8_t attr);
} calc_callbacks_t;

/* Initialize the calculator module */
void calc_init(void);

/* Set callback functions */
void calc_set_callbacks(calc_callbacks_t *callbacks);

/* Start the calculator (called by kernel when Ctrl+C is pressed) */
void calc_start(void);

/* Check if calculator is currently active */
int calc_is_active(void);

/* Handle a scancode when calculator is active. Returns 1 if calculator handled it, 0 if calculator exited */
int calc_handle_scancode(uint8_t scancode);

#endif
