/* calc.h */
#ifndef CALC_H
#define CALC_H

#include <stdint.h>
#include <stddef.h>

/* Callbacks that the calculator needs from the kernel */
typedef struct {
    void (*clear_screen)(void);
    void (*draw_char)(size_t row, size_t col, char c, uint8_t attr);
} calc_callbacks_t;

/* Initialize calculator subsystem */
void calc_init(void);

/* Set callback functions */
void calc_set_callbacks(calc_callbacks_t *callbacks);

/* Start the calculator (clears screen and shows interface) */
void calc_start(void);

/* Check if calculator is currently active */
int calc_is_active(void);

/* Handle keyboard scancode while calculator is active.
   Returns 1 if calculator is still active, 0 if user exited. */
int calc_handle_scancode(uint8_t scancode);

#endif /* CALC_H */

