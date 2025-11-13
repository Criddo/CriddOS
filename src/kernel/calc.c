/* ============================================================================
   CALCULATOR
   ============================================================================ */
#include <stdint.h>
#include <stddef.h>
#include "calc.h"

/* ============================================================================
   CALCULATOR CONFIGURATION
   ============================================================================ */

#define CALC_WIDTH 80          // Screen width in characters
#define CALC_HEIGHT 25         // Screen height in characters
#define INPUT_MAX 256          // Maximum input buffer size
#define DECIMAL_PLACES 6       // Number of decimal places to support
#define SCALE_FACTOR 1000000   // 10^6 - multiplier for fixed-point arithmetic

/* ============================================================================
   GLOBAL STATE VARIABLES
   ============================================================================ */

static int active = 0;                      // Whether calculator currently active
static char input_buffer[INPUT_MAX];        // User input buffer
static size_t input_pos = 0;                // Current position in input buffer
static int shift_down = 0;                  // Whether shift key currently pressed
static int ctrl_down = 0;                   // Whether control key currently pressed
static calc_callbacks_t callbacks;          // Function pointers to kernel services

/* ============================================================================
   KEYBOARD SCANCODE MAPPING
   ============================================================================ */

static char normal_map[256];  // Maps scancodes to characters (without shift)
static char shift_map[256];   // Maps scancodes to characters (with shift)

/* Initialise keyboard scancode-to-character mapping tables */
static void init_scancode_map(void) {
    // Initialise all mappings to zero (no character output)
    for (int i = 0; i < 256; ++i) {
        normal_map[i] = 0;
        shift_map[i] = 0;
    }

    /* Number row keys */
    normal_map[0x02] = '1'; shift_map[0x02] = '!';  // Scancode 0x02: 1 or !
    normal_map[0x03] = '2'; shift_map[0x03] = '@';  // Scancode 0x03: 2 or @
    normal_map[0x04] = '3'; shift_map[0x04] = '#';  // Scancode 0x04: 3 or #
    normal_map[0x05] = '4'; shift_map[0x05] = '$';  // Scancode 0x05: 4 or $
    normal_map[0x06] = '5'; shift_map[0x06] = '%';  // Scancode 0x06: 5 or %
    normal_map[0x07] = '6'; shift_map[0x07] = '^';  // Scancode 0x07: 6 or ^
    normal_map[0x08] = '7'; shift_map[0x08] = '&';  // Scancode 0x08: 7 or &
    normal_map[0x09] = '8'; shift_map[0x09] = '*';  // Scancode 0x09: 8 or *
    normal_map[0x0A] = '9'; shift_map[0x0A] = '(';  // Scancode 0x0A: 9 or (
        normal_map[0x0B] = '0'; shift_map[0x0B] = ')';  // Scancode 0x0B: 0 or )

        /* Special operation keys */
        normal_map[0x0C] = '-'; shift_map[0x0C] = '_';  // Minus/underscore key
        normal_map[0x0D] = '='; shift_map[0x0D] = '+';  // Equals/plus key
        normal_map[0x1C] = '\n';                        // Enter key
        normal_map[0x39] = ' ';                         // Spacebar
        normal_map[0x0E] = '\b';                        // Backspace key

        /* Division and decimal point */
        normal_map[0x35] = '/'; shift_map[0x35] = '?';  // Forward slash/question mark
        normal_map[0x34] = '.'; shift_map[0x34] = '>';  // Period/greater-than

        /* Letter keys (for Ctrl+Q quit command) */
        normal_map[0x10] = 'q'; shift_map[0x10] = 'Q';  // Q key
}

/* ============================================================================
   FIXED-POINT ARITHMETIC TYPE (store numbers as integers multiplied by SCALE_FACTOR (1,000,000))
   ============================================================================ */

typedef int64_t fixed_t;  // Fixed-point number: value * SCALE_FACTOR

/* ============================================================================
   FIXED-POINT PARSING
   ============================================================================*/

static fixed_t parse_fixed(const char *str, int len, int *error) {
    *error = 0;

    // Empty string is an error
    if (len == 0) {
        *error = 1;
        return 0;
    }

    int64_t integer_part = 0;    // Will store digits before decimal point
    int64_t decimal_part = 0;    // Will store digits after decimal point
    int decimal_digits = 0;      // Count of decimal digits parsed
    int is_negative = 0;         // Track if number is negative
    int i = 0;                   // Current position in string
    int found_dot = 0;           // Whether decimal is found

    if (str[0] == '-') { // If negative sign found at start
        is_negative = 1;  // Mark as negative
        i = 1;            // Skip the minus sign
    }

    /* Parse integer part (all digits before decimal point) */
    while (i < len && str[i] != '.') {
        char c = str[i];  // Get current character

        if (c >= '0' && c <= '9') {
            // Shift existing digits left and add new digit
            integer_part = integer_part * 10 + (c - '0');
        } else if (c != ' ') {
            *error = 1;
            return 0; // Return error for non-digit, non-space character
        }
        i++;  // Move to next character
    }

    /* Parse decimal part if decimal point exists */
    if (i < len && str[i] == '.') {
        found_dot = 1;  // Mark that we found the decimal point
        i++;            // Skip the decimal point itself

        // Parse up to DECIMAL_PLACES digits after the decimal point
        while (i < len && decimal_digits < DECIMAL_PLACES) {
            char c = str[i];  // Get current character

            if (c >= '0' && c <= '9') {
                decimal_part = decimal_part * 10 + (c - '0'); // Add this digit to decimal part
                decimal_digits++;  // Track how many decimal digits processed
            } else if (c != ' ') {
                *error = 1; // Non-digit, non-space character is an error
                return 0;
            }
            i++;  // Move to next character
        }
    }

    /* Scale decimal part to SCALE_FACTOR (pad with zeros if needed) */
    while (decimal_digits < DECIMAL_PLACES) {
        decimal_part *= 10;      // Multiply by 10 (adds a zero)
        decimal_digits++;        // Track that a digit has been added
    }

    /* Combine integer and decimal parts into final fixed-point value */
    fixed_t result = integer_part * SCALE_FACTOR + decimal_part;

    return is_negative ? -result : result; // Apply negative sign if needed
}

/* ============================================================================
   FIXED-POINT TO STRING CONVERSION (Convert a fixed-point number back to a readable decimal string)
   ============================================================================ */

static void fixed_to_str(fixed_t val, char *buf, size_t bufsize) {
    // Safety check: need at least 2 characters for output
    if (bufsize < 2) return;

    int idx = 0;  // Current position in output buffer

    /* Handle negative numbers */
    if (val < 0) {
        buf[idx++] = '-';  // Add minus sign
        val = -val;        // Make value positive for easier processing
    }

    /* Extract integer and decimal parts */
    int64_t integer = val / SCALE_FACTOR;
    int64_t decimal = val % SCALE_FACTOR;

    /* Convert integer part to string */
    if (integer == 0) {
        buf[idx++] = '0'; // If integer part is 0, just write "0"
    } else {
        // Build integer digits in reverse order
        char temp[32];      // Temporary buffer for reversed digits
        int temp_idx = 0;   // Position in temporary buffer

        // Extract digits one by one (from right to left)
        while (integer > 0 && temp_idx < 32) {
            temp[temp_idx++] = '0' + (integer % 10);  // Get last digit
            integer /= 10;                             // Remove last digit
        }

        // Copy digits in correct order
        for (int i = temp_idx - 1; i >= 0 && idx < (int)bufsize - 1; i--) {
            buf[idx++] = temp[i];
        }
    }

    /* Add decimal part if non-zero */
    if (decimal > 0 && idx < (int)bufsize - 1) {
        buf[idx++] = '.';  // Add decimal point

        /* Convert decimal part to digits */
        char dec_temp[DECIMAL_PLACES];  // Array to hold decimal digits

        // Extract each decimal digit (from right to left)
        for (int i = DECIMAL_PLACES - 1; i >= 0; i--) {
            dec_temp[i] = '0' + (decimal % 10);  // Get last digit
            decimal /= 10;                        // Remove last digit
        }

        /* Remove trailing zeros where not needed */
        int last_nonzero = DECIMAL_PLACES - 1;  // Start from rightmost digit
        while (last_nonzero >= 0 && dec_temp[last_nonzero] == '0') {
            last_nonzero--;  // Move left until there is a non-zero digit
        }

        /* Copy decimal digits to output (excluding trailing zeros) */
        for (int i = 0; i <= last_nonzero && idx < (int)bufsize - 1; i++) {
            buf[idx++] = dec_temp[i];
        }
    }

    buf[idx] = '\0';  // Null-terminate the string
}

/* ============================================================================
   EXPRESSION PARSER - GLOBAL STATE
   ============================================================================ */

static const char *expr_ptr;  // Current position in expression being parsed
static int expr_error;        // Check whether an error occurred during parsing

/* Skip over whitespace characters in the expression */
static void skip_whitespace(void) {
    while (*expr_ptr == ' ') expr_ptr++; // Keep advancing while there are space characters
}

/* ============================================================================
   EXPRESSION PARSER - NUMBER PARSING
   ============================================================================ */

static fixed_t parse_number(void) {
    skip_whitespace();  // Skip any leading spaces

    const char *start = expr_ptr;  // Remember where number starts
    int is_negative = 0;           // Track if number is negative

    /* Check for negative sign */
    if (*expr_ptr == '-') {
        is_negative = 1;  // Mark as negative
        expr_ptr++;       // Move past the minus sign
    }

    /* Scan through digits and decimal point */
    int has_digit = 0;  // Track if has at least one digit

    // Keep advancing through valid number characters
    while ((*expr_ptr >= '0' && *expr_ptr <= '9') || *expr_ptr == '.') {
        if (*expr_ptr >= '0' && *expr_ptr <= '9') {
            has_digit = 1;
        }
        expr_ptr++;  // Move to next character
    }

    if (!has_digit) { // No digits found is an error
        expr_error = 1;
        return 0;
    }

    /* Parse the number just scanned */
    int len = expr_ptr - start;        // Calculate length of number string
    int error = 0;                     // Error flag for parse_fixed
    fixed_t result = parse_fixed(start, len, &error);  // Convert to fixed-point

    // Check if parsing failed
    if (error) {
        expr_error = 1;  // Mark parser as having an error
        return 0;
    }

    return result;  // Return the parsed number
}

/* Forward declaration - needed because parse_factor calls parse_expression */
static fixed_t parse_expression(void);

/* ============================================================================
   EXPRESSION PARSER - FACTOR PARSING (either a number or a parenthesized expression)
   ============================================================================ */

static fixed_t parse_factor(void) {
    skip_whitespace();  // Skip leading spaces

    /* Check for parenthesised expression */
    if (*expr_ptr == '(') {
        expr_ptr++;  // Skip opening parenthesis

        // Recursively parse the expression inside parentheses
        fixed_t result = parse_expression();

        skip_whitespace();  // Skip spaces before closing parenthesis

        /* Expect closing parenthesis */
        if (*expr_ptr == ')') {
            expr_ptr++;  // Skip closing parenthesis
        } else {
            expr_error = 1; // Missing closing parenthesis is an error
        }

        return result;
    }

    return parse_number(); // Not a parenthesised expression, must be a number
}

/* ============================================================================
   EXPRESSION PARSER - TERM PARSING (factors connected by * or / )
   ============================================================================ */

static fixed_t parse_term(void) {
    fixed_t result = parse_factor();  // Get first factor

    /* Process multiplication and division operators */
    while (!expr_error) {
        skip_whitespace();     // Skip spaces around operator
        char op = *expr_ptr;   // Look at current character

        if (op == '*') {
            /* Multiplication */
            expr_ptr++;  // Skip the '*' operator
            fixed_t right = parse_factor();  // Get next factor

            // Multiply and scale back to maintain fixed-point precision
            result = (result * right) / SCALE_FACTOR;

        } else if (op == '/') {
            /* Division */
            expr_ptr++;  // Skip the '/' operator
            fixed_t right = parse_factor();  // Get next factor

            // Check for division by zero
            if (right == 0) {
                expr_error = 1;  // Set error flag
                return 0;
            }

            // Scale up before division to maintain precision
            result = (result * SCALE_FACTOR) / right;

        } else {
            break; // If no more * or / operators, exit loop
        }
    }

    return result;
}

/* ============================================================================
   EXPRESSION PARSER - EXPRESSION PARSING (terms connected by + or - operators)
   ============================================================================ */

static fixed_t parse_expression(void) {
    fixed_t result = parse_term();  // Get first term

    /* Process addition and subtraction operators */
    while (!expr_error) {
        skip_whitespace();     // Skip spaces around operator
        char op = *expr_ptr;   // Look at current character

        if (op == '+') {
            /* Addition */
            expr_ptr++;  // Skip the '+' operator
            fixed_t right = parse_term();  // Get next term

            // Add values (both values are already scaled by SCALE_FACTOR)
            result = result + right;

        } else if (op == '-') {
            /* Subtraction */
            expr_ptr++;  // Skip the '-' operator
            fixed_t right = parse_term();  // Get next term

            // Subtract values
            result = result - right;

        } else {
            break; // Exit loop if no more + or - operators
        }
    }

    return result;
}

/* ============================================================================
   EXPRESSION EVALUATOR (evaluate a mathematial expression string)
   ============================================================================ */
static fixed_t evaluate(const char *expr, int *error) {
    expr_ptr = expr;     // Initialise parser to start of expression
    expr_error = 0;      // Clear any previous error state
    *error = 0;          // Initialise output error flag

    /* Check for empty expression */
    if (!expr || *expr == '\0') {
        *error = 1;  // Empty expression is an error
        return 0;
    }

    /* Parse the expression */
    fixed_t result = parse_expression();  // Start parsing from top level

    /* Verify entire expression is consumed */
    skip_whitespace();  // Skip any trailing spaces

    if (*expr_ptr != '\0') {
        expr_error = 1; // Error if there are leftover characters
    }

    *error = expr_error;  // Copy error state to output parameter
    return result;        // Return computed result
}

/* ============================================================================
   SCREEN DRAWING (Redraw the calculator interface on screen)
   ============================================================================ */

static void calc_redraw(void) {
    callbacks.clear_screen();  // Clear entire screen first

    /* Draw title banner */
    const char *title = "=== Calculator ===";

    // Calculate title length
    size_t title_len = 0;
    while (title[title_len]) title_len++;

    // Center the title on screen
    size_t start_col = (CALC_WIDTH - title_len) / 2;

    // Draw each character of title in bright white (0x0F)
    for (size_t i = 0; i < title_len; i++) {
        callbacks.draw_char(0, start_col + i, title[i], 0x0F);
    }

    /* Draw instructions for user */
    const char *instr = "Type expression and press Enter. Ctrl+Q to quit.";

    // Calculate instruction length
    size_t instr_len = 0;
    while (instr[instr_len]) instr_len++;

    // Center the instructions on screen
    start_col = (CALC_WIDTH - instr_len) / 2;

    // Draw each character in light gray (0x07)
    for (size_t i = 0; i < instr_len; i++) {
        callbacks.draw_char(1, start_col + i, instr[i], 0x07);
    }

    /* --- Draw Separator Line --- */
    // Draw dashes on row 2 to separate header from content
    for (size_t i=0; i<CALC_WIDTH; i++)
        callbacks.draw_char(2, i, '-', 0x07);

    /* Draw input prompt */
    callbacks.draw_char(3, 0, '>', 0x0A);   // Green '>' prompt
    callbacks.draw_char(3, 1, ' ', 0x07);   // Space after prompt

    /* Draw current input buffer contents */
    for (size_t i = 0; i < input_pos && i < CALC_WIDTH - 2; i++) {
        // Draw each character in bright white (0x0F)
        callbacks.draw_char(3, 2 + i, input_buffer[i], 0x0F);
    }

    /* Draw cursor */
    if (input_pos < CALC_WIDTH - 2) {
        // Draw yellow underscore cursor at current position
        callbacks.draw_char(3, 2 + input_pos, '_', 0x0E);
    }
}

/* ============================================================================
   PUBLIC API FUNCTIONS (functions called by the kernel to control the calculator)
   ============================================================================ */

/* Initialise calculator subsystem */
void calc_init(void) {
    init_scancode_map();  // Set up keyboard mappings
    active = 0;           // Calculator starts inactive
}

/* Set callback functions for screen operations */
void calc_set_callbacks(calc_callbacks_t *cb) {
    if (cb) {
        // Copy function pointers from provided structure
        callbacks.clear_screen = cb->clear_screen;
        callbacks.draw_char = cb->draw_char;
    }
}

/* Start the calculator (called when user presses Ctrl+C) */
void calc_start(void) {
    active = 1;      // Mark calculator as active
    input_pos = 0;   // Clear input position
    shift_down = 0;  // Clear shift state
    ctrl_down = 0;   // Clear control state

    // Clear input buffer
    for (size_t i = 0; i < INPUT_MAX; i++) {
        input_buffer[i] = '\0';
    }

    calc_redraw();  // Draw initial calculator screen
}

/* Check if calculator is currently active */
int calc_is_active(void) {
    return active;  // Return 1 if active, 0 if not
}

/* ============================================================================
   KEYBOARD INPUT HANDLING
   ============================================================================ */

int calc_handle_scancode(uint8_t scancode) {

    /* Handle Shift key press (left shift or right shift) */
    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = 1;  // Mark shift as pressed
        return 1;        // Stay active
    }

    /* Handle Shift key release */
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_down = 0;  // Mark shift as released
        return 1;        // Stay active
    }

    /* Handle Control key press */
    if (scancode == 0x1D) {
        ctrl_down = 1;  // Mark control as pressed
        return 1;       // Stay active
    }

    /* Handle Control key release */
    if (scancode == 0x9D) {
        ctrl_down = 0;  // Mark control as released
        return 1;       // Stay active
    }

    /* Ignore key release events */
    if (scancode & 0x80) return 1;

    /* Convert scancode to character based on shift state */
    char c = shift_down ? shift_map[scancode] : normal_map[scancode];

    /* Check for Ctrl+Q (quit calculator) */
    if (ctrl_down && (c == 'q' || c == 'Q')) {
        callbacks.clear_screen();  // Clear screen before exiting
        active = 0;                // Mark calculator as inactive
        return 0;                  // Signal that calculator has quit
    }

    /* Ignore other control key combinations */
    if (ctrl_down) return 1;

    /* Handle Backspace */
    if (c == '\b') {
        if (input_pos > 0) {
            input_pos--;                      // Move cursor back
            input_buffer[input_pos] = '\0';   // Clear character
            calc_redraw();                    // Update display
        }
        return 1;  // Stay active
    }

    /* Handle Enter key - evaluate expression */
    if (c == '\n') {
        input_buffer[input_pos] = '\0';  // Null-terminate input

        /* Clear result line (row 5) */
        for (int i = 0; i < CALC_WIDTH; i++) {
            callbacks.draw_char(5, i, ' ', 0x07);
        }

        /* Evaluate the expression */
        int error = 0;
        fixed_t result = evaluate(input_buffer, &error);

        /* Display result or error */
        if (error || input_pos == 0) {
            /* Error case - display error message */
            const char *err_msg = "Error!";

            callbacks.draw_char(5, 0, '!', 0x0C);   // Red exclamation mark
            callbacks.draw_char(5, 1, ' ', 0x07);   // Space

            // Draw error message in red (0x0C)
            for (int i = 0; err_msg[i]; i++) {
                callbacks.draw_char(5, 2 + i, err_msg[i], 0x0C);
            }

        } else {
            /* Success case - display result */
            char result_str[64];  // Buffer for result string

            // Initialise result string buffer
            for (int i = 0; i < 64; i++) result_str[i] = '\0';

            // Convert fixed-point result to string
            fixed_to_str(result, result_str, sizeof(result_str));

            callbacks.draw_char(5, 0, '=', 0x0A);   // Green equals sign
            callbacks.draw_char(5, 1, ' ', 0x07);   // Space

            // Draw result string in bright white (0x0F)
            int i = 0;
            while (result_str[i] != '\0' && i < 60) {
                callbacks.draw_char(5, 2 + i, result_str[i], 0x0F);
                i++;
            }
        }

        /* Clear input buffer for next calculation */
        input_pos = 0;
        for (size_t i = 0; i < INPUT_MAX; i++) {
            input_buffer[i] = '\0';
        }

        /* Redraw input line (clear old input, show fresh prompt) */
        for (size_t i = 2; i < CALC_WIDTH; i++) {
            callbacks.draw_char(3, i, ' ', 0x07);  // Clear line
        }

        callbacks.draw_char(3, 0, '>', 0x0A);     // Green prompt
        callbacks.draw_char(3, 1, ' ', 0x07);     // Space
        callbacks.draw_char(3, 2, '_', 0x0E);     // Yellow cursor

        return 1;  // Stay active
    }

    /* Add character to input buffer */
    if (c && input_pos < INPUT_MAX - 1) {
        /* Only allow numbers, operators, parentheses, space and decimal point */
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' ||
            c == '/' || c == '(' || c == ')' || c == ' ' || c == '.') {

            input_buffer[input_pos++] = c;  // Add character to buffer
            calc_redraw();                  // Update display
            }
    }

    return 1;  // Stay active
}
