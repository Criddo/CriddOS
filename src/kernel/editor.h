/* editor.h - Simple text editor interface */
#ifndef EDITOR_H
#define EDITOR_H

#include <stdint.h>
#include <stddef.h>

/* Initialize the editor subsystem */
void editor_init(void);

/* Start the editor (clears screen and enters editor mode) */
void editor_start(void);

/* Handle a scancode when editor is active. Returns 1 if editor handled it, 0 if editor exited */
int editor_handle_scancode(uint8_t scancode);

/* Check if editor is currently active */
int editor_is_active(void);

/* Callback functions that editor needs from kernel - must be provided by kernel */
typedef struct {
    void (*clear_screen)(void);
    void (*draw_char)(size_t row, size_t col, char ch, uint8_t attr);
    int (*fat_write)(const char *name, const uint8_t *data, size_t len);
    int (*fat_read)(const char *name, uint8_t *buf, size_t maxlen);
    void (*print_message)(const char *msg);
} editor_callbacks_t;

/* Set the callbacks that editor will use */
void editor_set_callbacks(const editor_callbacks_t *callbacks);

#endif

