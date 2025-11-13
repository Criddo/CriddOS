/* ============================================================================
   Text editor
   ============================================================================ */
#include "editor.h"

/* ============================================================================
   CONSTANTS AND BUFFER DEFINITIONS
   ============================================================================ */

#define VGA_WIDTH 80           // Standard VGA text mode width (80 columns)
#define VGA_HEIGHT 25          // Standard VGA text mode height (25 rows)
#define VGA_ATTR 0x07          // VGA attribute: light gray text on black background

#define EDIT_BUF_SIZE (VGA_WIDTH * (VGA_HEIGHT - 3))  // Maximum buffer size (3 rows reserved for UI)

/* ============================================================================
   EDITOR STATE VARIABLES
   ============================================================================ */

static char edit_buf[EDIT_BUF_SIZE];    // Main text buffer holding the document
static size_t edit_len = 0;              // Current number of characters in buffer
static size_t edit_cursor = 0;           // Current cursor position (0 to edit_len)
static int editor_active = 0;            // Flag: 1 when editor is running, 0 otherwise

static size_t view_offset = 0;           // First character position visible on screen (used for scrolling)

/* ============================================================================
   FILE PROMPT STATE
   ============================================================================ */

// Enumeration defining the type of prompt currently shown to the user
typedef enum {
    PROMPT_NONE=0,      // No prompt active
    PROMPT_SAVE,        // "Save as:" prompt is active
    PROMPT_OPEN         // "Open file:" prompt is active
} prompt_mode_t;

static prompt_mode_t prompt_mode = PROMPT_NONE;  // Current prompt state
static char prompt_buf[32];                       // Buffer for filename input
static size_t prompt_len = 0;                     // Length of text in prompt buffer

/* ============================================================================
   KEYBOARD MODIFIER STATE
   ============================================================================ */

static int shift_down = 0;   // Flag: 1 when Shift key is pressed, 0 otherwise
static int ctrl_down = 0;    // Flag: 1 when Ctrl key is pressed, 0 otherwise

/* ============================================================================
   CALLBACK FUNCTION POINTERS
   ============================================================================ */

static editor_callbacks_t callbacks;  // Structure for holding function pointers

/* ============================================================================
   SCANCODE TO CHARACTER MAPPING TABLES
   ============================================================================ */

static char normal_map[256];  // Maps keyboard scancodes to normal characters
static char shift_map[256];   // Maps keyboard scancodes to shifted characters

/* ============================================================================
   UNDO/REDO SYSTEM DEFINITIONS
   ============================================================================ */

// Enumeration defining the type of action that can be undone/redone
typedef enum {
    ACT_INSERT,   // Character insertion action
    ACT_DELETE    // Character deletion action
} action_type_t;

// Structure representing a single reversible action
typedef struct {
    action_type_t type;   // Type of action (insert or delete)
    size_t pos;           // Position in buffer where action occurred
    char ch;              // Character that was inserted or deleted
} action_t;

#define UNDO_STACK_SIZE 512   // Maximum number of undo operations to remember

static action_t undo_stack[UNDO_STACK_SIZE];  // Stack of actions that can be undone
static int undo_top = 0;                       // Index of next free slot in undo stack

static action_t redo_stack[UNDO_STACK_SIZE];  // Stack of actions that can be redone
static int redo_top = 0;                       // Index of next free slot in redo stack

/* ============================================================================
   UNDO/REDO STACK OPERATIONS
   ============================================================================ */

// Push an action onto the undo stack
static void undo_push(action_t a) {
    if (undo_top < UNDO_STACK_SIZE)       // Check if there's space in the stack
        undo_stack[undo_top++] = a;       // Store action and increment top pointer
}

// Pop an action from the undo stack. Returns 1 if successful, 0 if stack is empty
static int undo_pop(action_t *out) {
    if (undo_top == 0) return 0;          // Stack is empty
    *out = undo_stack[--undo_top];        // Decrement pointer and retrieve action
    return 1;                             // Success
}

// Push an action onto the redo stack
static void redo_push(action_t a) {
    if (redo_top < UNDO_STACK_SIZE)       // Check if there's space in the stack
        redo_stack[redo_top++] = a;       // Store action and increment top pointer
}

// Pop an action from the redo stack. Returns 1 if successful, 0 if stack is empty
static int redo_pop(action_t *out) {
    if (redo_top == 0) return 0;          // Stack is empty
    *out = redo_stack[--redo_top];        // Decrement pointer and retrieve action
    return 1;                             // Success
}

// Clear the redo stack (called when new edit is made, invalidating redo history)
static void redo_clear(void) {
    redo_top = 0;                         // Reset stack pointer to empty
}

/* ============================================================================
   SCANCODE TO CHARACTER MAPPING INITIALISATION
   ============================================================================ */

static void scancode_map_init(void) {
    // Initialise all entries to 0 (unmapped scancodes)
    for (int i = 0; i < 256; ++i)
        normal_map[i] = shift_map[i] = 0;

    /* Letter Keys */
    normal_map[0x10]='q'; shift_map[0x10]='Q';  // Q key
    normal_map[0x11]='w'; shift_map[0x11]='W';  // W key
    normal_map[0x12]='e'; shift_map[0x12]='E';  // E key
    normal_map[0x13]='r'; shift_map[0x13]='R';  // R key
    normal_map[0x14]='t'; shift_map[0x14]='T';  // T key
    normal_map[0x15]='y'; shift_map[0x15]='Y';  // Y key
    normal_map[0x16]='u'; shift_map[0x16]='U';  // U key
    normal_map[0x17]='i'; shift_map[0x17]='I';  // I key
    normal_map[0x18]='o'; shift_map[0x18]='O';  // O key
    normal_map[0x19]='p'; shift_map[0x19]='P';  // P key
    normal_map[0x1E]='a'; shift_map[0x1E]='A';  // A key
    normal_map[0x1F]='s'; shift_map[0x1F]='S';  // S key
    normal_map[0x20]='d'; shift_map[0x20]='D';  // D key
    normal_map[0x21]='f'; shift_map[0x21]='F';  // F key
    normal_map[0x22]='g'; shift_map[0x22]='G';  // G key
    normal_map[0x23]='h'; shift_map[0x23]='H';  // H key
    normal_map[0x24]='j'; shift_map[0x24]='J';  // J key
    normal_map[0x25]='k'; shift_map[0x25]='K';  // K key
    normal_map[0x26]='l'; shift_map[0x26]='L';  // L key
    normal_map[0x2C]='z'; shift_map[0x2C]='Z';  // Z key
    normal_map[0x2D]='x'; shift_map[0x2D]='X';  // X key
    normal_map[0x2E]='c'; shift_map[0x2E]='C';  // C key
    normal_map[0x2F]='v'; shift_map[0x2F]='V';  // V key
    normal_map[0x30]='b'; shift_map[0x30]='B';  // B key
    normal_map[0x31]='n'; shift_map[0x31]='N';  // N key
    normal_map[0x32]='m'; shift_map[0x32]='M';  // M key

    /* Number Keys */
    normal_map[0x02]='1'; shift_map[0x02]='!';  // 1/! key
    normal_map[0x03]='2'; shift_map[0x03]='@';  // 2/@ key
    normal_map[0x04]='3'; shift_map[0x04]='#';  // 3/# key
    normal_map[0x05]='4'; shift_map[0x05]='$';  // 4/$ key
    normal_map[0x06]='5'; shift_map[0x06]='%';  // 5/% key
    normal_map[0x07]='6'; shift_map[0x07]='^';  // 6/^ key
    normal_map[0x08]='7'; shift_map[0x08]='&';  // 7/& key
    normal_map[0x09]='8'; shift_map[0x09]='*';  // 8/* key
    normal_map[0x0A]='9'; shift_map[0x0A]='(';  // 9/( key
    normal_map[0x0B]='0'; shift_map[0x0B]=')';  // 0/) key

    /* Punctuation and Special Keys */
    normal_map[0x0C]='-'; shift_map[0x0C]='_';  // -/_ key
    normal_map[0x0D]='='; shift_map[0x0D]='+';  // =/+ key
    normal_map[0x1C]='\n';                       // Enter key (newline)
    normal_map[0x39]=' ';                        // Space bar

    normal_map[0x27]=';'; shift_map[0x27]=':';  // ;/: key
    normal_map[0x28]='\''; shift_map[0x28]='"'; // '/" key
    normal_map[0x2B]='\\'; shift_map[0x2B]='|'; // \/| key
    normal_map[0x33]=','; shift_map[0x33]='<';  // ,/< key
    normal_map[0x34]='.'; shift_map[0x34]='>';  // ./> key
    normal_map[0x35]='/'; shift_map[0x35]='?';  // //? key
    normal_map[0x29]='`'; shift_map[0x29]='~';  // `/~ key

    normal_map[0x0E]='\b';                       // Backspace key
}

/* ============================================================================
   CURSOR POSITION CALCULATION
   ============================================================================ */

// Calculate the screen row and column for a given buffer position. Handles newlines and line wrapping at VGA_WIDTH
static void calc_cursor_pos(size_t pos, size_t *row, size_t *col) {
    size_t r=0, c=0;                      // Start at row 0, column 0

    // Iterate through buffer up to the specified position
    for (size_t i=0; i<pos && i<edit_len; i++) {
        if (edit_buf[i]=='\n') {          // Newline character
            r++;                          // Move to next row
            c=0;                          // Reset to column 0
        }
        else {                            // Regular character
            c++;                          // Move to next column
            if (c>=VGA_WIDTH) {           // Line wraps at screen width
                r++;                      // Move to next row
                c=0;                      // Reset to column 0
            }
        }
    }
    *row=r;                               // Return calculated row
    *col=c;                               // Return calculated column
}

/* ============================================================================
   VIEW SCROLLING ADJUSTMENT
   ============================================================================ */

// Adjust the view_offset to ensure the cursor is visible on screen. Scrolls the view up or down as needed
static void adjust_view(void) {
    size_t cr, cc, vr, vc;                // Cursor and view row/column

    calc_cursor_pos(edit_cursor, &cr, &cc);    // Get cursor position
    calc_cursor_pos(view_offset, &vr, &vc);    // Get view start position

    size_t visible = VGA_HEIGHT - 3;      // Number of visible text rows (3 rows used for header and prompt)

    // Scroll down if cursor is below the visible area
    while (cr >= vr + visible && view_offset < edit_len) {
        if (edit_buf[view_offset] == '\n')    // Newline advances view row
            vr++;
        view_offset++;                    // Advance view offset
    }

    // Scroll up if cursor is above the visible area
    while (cr < vr && view_offset > 0) {
        view_offset--;                    // Move view offset back
        if (edit_buf[view_offset] == '\n')    // Newline moves view row back
            vr--;
    }
}

/* ============================================================================
   CURSOR MOVEMENT FUNCTIONS
   ============================================================================ */

/* Left/Right Movement */

// Move cursor one position to the left
static void move_left(void) {
    if (edit_cursor > 0)                  // Check if not at beginning
        edit_cursor--;                    // Move cursor back one position
}

// Move cursor one position to the right
static void move_right(void) {
    if (edit_cursor < edit_len)           // Check if not at end
        edit_cursor++;                    // Move cursor forward one position
}

/* Line Navigation Helpers */

// Find the beginning of the line containing the given position
static size_t line_start(size_t pos) {
    while (pos > 0 && edit_buf[pos-1] != '\n')  // Move backwards
        pos--;                                   // Until newline or start
        return pos;                                  // Return start of line
}

// Compute the column number (horizontal position) at the given position
static size_t column_at(size_t pos) {
    size_t start = line_start(pos);       // Find start of current line
    return pos - start;                   // Column is offset from line start
}

// Find the start of the next line after the given position
static size_t next_line_start(size_t pos) {
    // Walk forward to the newline character
    while (pos < edit_len && edit_buf[pos] != '\n')
        pos++;

    if (pos < edit_len)                   // If newline found
        return pos + 1;                   // Next line starts after newline
        return edit_len;                      // Otherwise, return end of buffer
}

/* Up/Down Movement */

// Move cursor up one line (maintaining horizontal column position)
static void move_up(void) {
    size_t col = column_at(edit_cursor);  // Remember current column
    size_t start = line_start(edit_cursor); // Find start of current line

    if (start == 0) return;               // Already on first line, can't go up

    size_t prev = line_start(start - 1);  // Find start of previous line
    size_t offset = prev + col;           // Try to maintain same column

    // Clamp to valid range
    while (offset > edit_len)
        offset--;

    // Don't land on the newline character
    while (offset > prev && edit_buf[offset-1] == '\n')
        offset--;

    edit_cursor = offset;                 // Update cursor position
}

// Move cursor down one line (maintaining horizontal column position)
static void move_down(void) {
    size_t col = column_at(edit_cursor);  // Remember current column
    size_t next = next_line_start(edit_cursor); // Find start of next line

    if (next >= edit_len) return;         // Already on last line, can't go down

    size_t target = next + col;           // Try to maintain same column

    // Clamp to valid range
    while (target > edit_len)
        target--;

    // Don't land on the newline character
    while (target > next && edit_buf[target-1] == '\n')
        target--;

    edit_cursor = target;                 // Update cursor position
}

/* ============================================================================
   SCREEN DRAWING
   ============================================================================ */

static void editor_redraw(void) {
    callbacks.clear_screen();             // Clear the entire screen


    /* Draw title banner */
    const char *title = "=== Editor ===";

    // Calculate title length
    size_t title_len = 0;
    while (title[title_len]) title_len++;

    // Center the title on screen
    size_t start_col = (VGA_WIDTH - title_len) / 2;

    // Draw each character of title in bright white (0x0F)
    for (size_t i = 0; i < title_len; i++) {
        callbacks.draw_char(0, start_col + i, title[i], 0x0F);
    }

    /* Draw instructions */
    const char *instr = "Type text. Ctrl+S save, Ctrl+O open, Ctrl+Q quit, "
    "Ctrl+Z undo, Ctrl+Y redo.";

    // Calculate instruction length
    size_t instr_len = 0;
    while (instr[instr_len]) instr_len++;

    // Center the instructions on screen
    start_col = (VGA_WIDTH - instr_len) / 2;

    // Draw each character in light gray (0x07)
    for (size_t i = 0; i < instr_len; i++) {
        callbacks.draw_char(1, start_col + i, instr[i], 0x07);
    }

    /* Draw Separator Line */
    for (size_t i=0; i<VGA_WIDTH; i++)
        callbacks.draw_char(2, i, '-', VGA_ATTR); // Draw dashes on row 2 to separate header from content

    size_t row = 3, col = 0;              // Draw text content at row 3, column 0

    // Draw visible portion of buffer
    for (size_t i=view_offset; i<edit_len && row<VGA_HEIGHT-1; i++) {
        char ch = edit_buf[i];            // Get character from buffer

        if (ch == '\n') {                 // Newline character
            row++;                        // Move to next row
            col = 0;                      // Reset to column 0
            continue;                     // Don't draw the newline itself
        }

        callbacks.draw_char(row, col, ch, VGA_ATTR);  // Draw character
        col++;                            // Advance to next column

        if (col >= VGA_WIDTH) {           // Reached end of line (wrapping)
            col = 0;                      // Reset to column 0
            row++;                        // Move to next row
        }
    }

    size_t cr, cc, vr, vc;                // Cursor and view positions
    calc_cursor_pos(edit_cursor, &cr, &cc);      // Get cursor position
    calc_cursor_pos(view_offset, &vr, &vc);      // Get view position

    size_t screen_row = cr - vr + 3;      // Convert to screen coordinates (3 accounts for header rows)

    // Draw cursor if it's in visible area
    if (screen_row >= 3 && screen_row < VGA_HEIGHT-1 && cc < VGA_WIDTH)
        callbacks.draw_char(screen_row, cc, '_', VGA_ATTR);

    /* Draw File Prompt (if active) */
    if (prompt_mode != PROMPT_NONE) {
        const char *label = (prompt_mode == PROMPT_SAVE) ? // Select appropriate label based on prompt type
        "Save as: " : "Open file: ";

        size_t p = 0;                     // Current column position

        // Draw prompt label on bottom row
        while (label[p] && p < VGA_WIDTH) {
            callbacks.draw_char(VGA_HEIGHT-1, p, label[p], VGA_ATTR);
            p++;
        }

        // Draw filename text entered so far
        for (size_t q=0; q<prompt_len && p<VGA_WIDTH; q++, p++)
            callbacks.draw_char(VGA_HEIGHT-1, p, prompt_buf[q], VGA_ATTR);

        // Draw cursor at end of prompt
        if (p < VGA_WIDTH)
            callbacks.draw_char(VGA_HEIGHT-1, p, '_', VGA_ATTR);
    }
}

/* ============================================================================
   TEXT EDITING OPERATIONS (WITH UNDO SUPPORT)
   ============================================================================ */

// Insert a character at the current cursor position
static void editor_insert_char(char c) {
    if (edit_len + 1 >= EDIT_BUF_SIZE)    // Check if buffer is full
        return;                           // Can't insert, buffer full

        for (size_t i=edit_len; i>edit_cursor; i--)  // Shift all characters after cursor one position to the right
            edit_buf[i] = edit_buf[i-1];

    edit_buf[edit_cursor] = c;            // Insert new character at cursor

    undo_push((action_t){ACT_INSERT, edit_cursor, c}); // Record action for undo

    redo_clear();                         // New edit invalidates redo history

    edit_cursor++;                        // Move cursor forward
    edit_len++;                           // Increase buffer length
}

// Delete character before cursor (backspace operation)
static void editor_backspace(void) {
    if (edit_cursor == 0)                 // Check if at beginning
        return;                           // Nothing to delete

        char c = edit_buf[edit_cursor-1];     // Remember deleted character

        for (size_t i=edit_cursor-1; i<edit_len-1; i++) // Shift all characters after cursor one position to the left
            edit_buf[i] = edit_buf[i+1];

    undo_push((action_t){ACT_DELETE, edit_cursor-1, c}); // Record action for undo

    redo_clear();                         // New edit invalidates redo history

    edit_cursor--;                        // Move cursor back
    edit_len--;                           // Decrease buffer length
}

/* ============================================================================
   UNDO/REDO OPERATIONS
   ============================================================================ */

// Undo the last editing action
static void do_undo(void) {
    action_t a;                           // Action to undo

    if (!undo_pop(&a))                    // Pop from undo stack
        return;                           // Nothing to undo

        if (a.type == ACT_INSERT) {           // Undoing an insertion
            // Remove the character that was inserted
            for (size_t i=a.pos; i<edit_len-1; i++)
                edit_buf[i] = edit_buf[i+1];

            char c = a.ch;                    // Remember the character

            redo_push((action_t){ACT_INSERT, a.pos, c}); // Push inverse action to redo stack

            if (edit_cursor > a.pos)          // Adjust cursor if needed
                edit_cursor--;

            edit_len--;                       // Decrease buffer length

        } else {                              // Undoing a deletion (ACT_DELETE)
            // Re-insert the deleted character
            for (size_t i=edit_len; i>a.pos; i--)
                edit_buf[i] = edit_buf[i-1];

            edit_buf[a.pos] = a.ch;           // Restore deleted character

            redo_push((action_t){ACT_DELETE, a.pos, a.ch}); // Push inverse action to redo stack

            if (edit_cursor >= a.pos)         // Adjust cursor if needed
                edit_cursor++;

            edit_len++;                       // Increase buffer length
        }
}

// Redo a previously undone action
static void do_redo(void) {
    action_t a;                           // Action to redo

    if (!redo_pop(&a))                    // Pop from redo stack
        return;                           // Nothing to redo

        if (a.type == ACT_INSERT) {           // Redoing an insertion
            // Re-insert the character
            for (size_t i=edit_len; i>a.pos; i--)
                edit_buf[i] = edit_buf[i-1];

            edit_buf[a.pos] = a.ch;           // Insert character

            undo_push((action_t){ACT_INSERT, a.pos, a.ch}); // Push action back to undo stack

            if (edit_cursor >= a.pos)         // Adjust cursor if needed
                edit_cursor++;

            edit_len++;                       // Increase buffer length

        } else {                              // Redoing a deletion (ACT_DELETE)
            char c = edit_buf[a.pos];         // Remember deleted character

            // Remove the character
            for (size_t i=a.pos; i<edit_len-1; i++)
                edit_buf[i] = edit_buf[i+1];

            // Push action back to undo stack
            undo_push((action_t){ACT_DELETE, a.pos, c});

            if (edit_cursor > a.pos)          // Adjust cursor if needed
                edit_cursor--;

            edit_len--;                       // Decrease buffer length
        }
}

/* ============================================================================
   FILE PROMPT HANDLING
   ============================================================================ */

// Start a file operation prompt (save or open)
static void start_prompt(prompt_mode_t m) {
    prompt_mode = m;                      // Set prompt type
    prompt_len = 0;                       // Clear prompt buffer

    // Zero out the prompt buffer
    for (size_t i=0; i<sizeof(prompt_buf); i++)
        prompt_buf[i] = 0;

    editor_redraw();                      // Redraw to show prompt
}

// Complete the file operation prompt and perform the action
static void finish_prompt(void) {
    // Ensure null-termination of filename
    prompt_buf[prompt_len < sizeof(prompt_buf)-1 ?
    prompt_len : sizeof(prompt_buf)-1] = 0;

    if (prompt_mode == PROMPT_SAVE) {     // Save operation
        // Write buffer to file using callback
        int r = callbacks.fat_write(prompt_buf,
                                    (const uint8_t*)edit_buf,
                                    edit_len);

        // Display result message
        callbacks.print_message(r == 0 ? "File saved.\n" : "Save failed.\n");

    } else if (prompt_mode == PROMPT_OPEN) {  // Open operation
        // Read file into buffer using callback
        int r = callbacks.fat_read(prompt_buf,
                                   (uint8_t*)edit_buf,
                                   EDIT_BUF_SIZE);

        if (r >= 0) {                     // Read successful
            edit_len = r;                 // Update buffer length
            edit_cursor = r;              // Move cursor to end
            view_offset = 0;              // Reset view to top
            callbacks.print_message("File loaded.\n");
        } else {                          // Read failed
            callbacks.print_message("Load failed.\n");
        }
    }

    prompt_mode = PROMPT_NONE;            // Close prompt
    editor_redraw();                      // Redraw editor
}

/* ============================================================================
   CONTROL KEY COMMAND HANDLING
   ============================================================================ */

// Handle Ctrl+Key commands
static void editor_handle_control(char c) {
    if (c == 'q' || c == 'Q') {           // Ctrl+Q: Quit editor
        editor_active = 0;                // Deactivate editor
        prompt_mode = 0;                  // Clear any prompt
        callbacks.clear_screen();         // Clear screen
    }
    else if (c == 's' || c == 'S')        // Ctrl+S: Save file
        start_prompt(PROMPT_SAVE);        // Show save prompt

        else if (c == 'o' || c == 'O')        // Ctrl+O: Open file
            start_prompt(PROMPT_OPEN);        // Show open prompt

            else if (c == 'z' || c == 'Z') {      // Ctrl+Z: Undo
                do_undo();                        // Perform undo operation
                adjust_view();                    // Adjust scroll to show cursor
                editor_redraw();                  // Redraw screen
            }
            else if (c == 'y' || c == 'Y') {      // Ctrl+Y: Redo
                do_redo();                        // Perform redo operation
                adjust_view();                    // Adjust scroll to show cursor
                editor_redraw();                  // Redraw screen
            }
}

/* ============================================================================
   PUBLIC API FUNCTIONS
   ============================================================================ */

// Initialise the editor subsystem
void editor_init(void) {
    scancode_map_init();                  // Set up keyboard mapping
    editor_active = 0;                    // Editor not running initially
    edit_len = 0;                         // Buffer is empty
    edit_cursor = 0;                      // Cursor at start
    view_offset = 0;                      // View at top
    shift_down = 0;                       // Shift not pressed
    ctrl_down = 0;                        // Ctrl not pressed
    prompt_mode = PROMPT_NONE;            // No prompt active
    undo_top = redo_top = 0;              // Clear undo/redo stacks
}

// Set callback functions for screen/file operations
void editor_set_callbacks(const editor_callbacks_t *cb) {
    callbacks = *cb;                      // Copy callback structure
}

// Start the editor with a clean buffer
void editor_start(void) {
    editor_active = 1;                    // Activate editor
    edit_len = 0;                         // Clear buffer
    edit_cursor = 0;                      // Reset cursor
    view_offset = 0;                      // Reset view
    prompt_mode = PROMPT_NONE;            // Clear prompt
    undo_top = redo_top = 0;              // Clear undo/redo history
    editor_redraw();                      // Draw initial screen
}

// Check if the editor is currently active
int editor_is_active(void) {
    return editor_active;                 // Return activation state
}

/* ============================================================================
   KEYBOARD SCANCODE PROCESSING
   ============================================================================ */

// Process a keyboard scancode. Returns 1 if scancode was handled, 0 if editor is not active
int editor_handle_scancode(uint8_t s) {
    if (!editor_active)                   // If editor not running
        return 0;                         // Don't handle input

        /* Shift Key Detection */
        if (s == 0x2A || s == 0x36) {         // Left/Right Shift key pressed
            shift_down = 1;                   // Mark shift as pressed
            return 1;                         // Handled
        }
        if (s == 0xAA || s == 0xB6) {         // Left/Right Shift key released
            shift_down = 0;                   // Mark shift as not pressed
            return 1;                         // Handled
        }

        /* Ctrl Key Detection */
        if (s == 0x1D) {                      // Ctrl key pressed
            ctrl_down = 1;                    // Mark ctrl as pressed
            return 1;                         // Handled
        }
        if (s == 0x9D) {                      // Ctrl key released
            ctrl_down = 0;                    // Mark ctrl as not pressed
            return 1;                         // Handled
        }

        /* Ignore Key Release Events */
        if (s & 0x80)                         // High bit set = key release
            return 1;                         // Ignore releases (except shift/ctrl)

            /* Arrow Key Handling */
            if (s == 0x4B) {                      // Left arrow key
                move_left();                      // Move cursor left
                adjust_view();                    // Ensure cursor visible
                editor_redraw();                  // Redraw screen
                return 1;                         // Handled
            }
            if (s == 0x4D) {                      // Right arrow key
                move_right();                     // Move cursor right
                adjust_view();                    // Ensure cursor visible
                editor_redraw();                  // Redraw screen
                return 1;                         // Handled
            }
            if (s == 0x48) {                      // Up arrow key
                move_up();                        // Move cursor up
                adjust_view();                    // Ensure cursor visible
                editor_redraw();                  // Redraw screen
                return 1;                         // Handled
            }
            if (s == 0x50) {                      // Down arrow key
                move_down();                      // Move cursor down
                adjust_view();                    // Ensure cursor visible
                editor_redraw();                  // Redraw screen
                return 1;                         // Handled
            }

            /* Tab Key: Insert 4 Spaces */
            if (s == 0x0F) {                      // Tab key pressed
                for (int i=0; i<4; i++)           // Loop 4 times
                    editor_insert_char(' ');      // Insert a space character
                    adjust_view();                    // Ensure cursor visible
                    editor_redraw();                  // Redraw screen
                    return 1;                         // Handled
            }

            /* Convert Scancode to Character */
            char c = shift_down ? shift_map[s] : normal_map[s]; // Look up character based on shift state

            /* Handle Input During File Prompt */
            if (prompt_mode != PROMPT_NONE) {     // If file prompt active
                if (s == 0x0E && prompt_len > 0)  // If backspace in prompt
                    prompt_buf[--prompt_len] = 0; // Delete last character

                    else if (c == '\n')               // Enter key in prompt
                        finish_prompt();              // Complete file operation

                        else if (c && prompt_len+1 < sizeof(prompt_buf))  // Regular character
                            prompt_buf[prompt_len++] = c; // Add to filename buffer

                            editor_redraw();                  // Redraw with updated prompt
                            return 1;
            }

            /* Handle Ctrl+Key Commands */
            if (ctrl_down && c) {                 // If ctrl is pressed and key mapped
                editor_handle_control(c);         // Process control command
                if (editor_active)                // If still active after command
                    editor_redraw();              // Redraw screen
                    return editor_active;         // Return activation state
            }

            /* Handle Backspace */
            if (c == '\b') {                      // Backspace character
                editor_backspace();               // Delete character before cursor
                adjust_view();                    // Ensure cursor visible
                editor_redraw();                  // Redraw screen
                return 1;                         // Handled
            }

            /* Handle Regular Character Input */
            if (c) {                              // Valid character to insert
                editor_insert_char(c);            // Insert character at cursor
                adjust_view();                    // Ensure cursor visible
                editor_redraw();                  // Redraw screen
                return 1;                         // Handled
            }

            return 1;                             // Scancode handled (even if unmapped)
}

