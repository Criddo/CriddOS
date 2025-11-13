/* calc.c */
#include <stdint.h>
#include <stddef.h>
#include "calc.h"

#define MAX_EXPR_LEN 256
#define MAX_STACK 64

static char expr_buf[MAX_EXPR_LEN];
static size_t expr_len = 0;
static int calc_active = 0;

/* Callbacks from kernel */
static calc_callbacks_t callbacks;

/* Helper: check if character is digit */
static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* Helper: check if character is operator */
static int is_operator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/';
}

/* Helper: get operator precedence */
static int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    return 0;
}

/* Stack operations for evaluation */
typedef struct {
    double values[MAX_STACK];
    int top;
} ValueStack;

typedef struct {
    char ops[MAX_STACK];
    int top;
} OpStack;

static void value_push(ValueStack *s, double val) {
    if (s->top < MAX_STACK - 1) {
        s->values[++s->top] = val;
    }
}

static double value_pop(ValueStack *s) {
    if (s->top >= 0) {
        return s->values[s->top--];
    }
    return 0.0;
}

static void op_push(OpStack *s, char op) {
    if (s->top < MAX_STACK - 1) {
        s->ops[++s->top] = op;
    }
}

static char op_pop(OpStack *s) {
    if (s->top >= 0) {
        return s->ops[s->top--];
    }
    return 0;
}

static char op_peek(OpStack *s) {
    if (s->top >= 0) {
        return s->ops[s->top];
    }
    return 0;
}

/* Apply operator to two operands */
static double apply_op(char op, double b, double a) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/':
            if (b == 0.0) {
                return 0.0; /* Division by zero */
            }
            return a / b;
        default: return 0.0;
    }
}

/* Simple double to string conversion */
static void double_to_str(double val, char *buf, size_t buflen) {
    if (buflen < 2) return;

    /* Handle negative */
    int neg = 0;
    if (val < 0) {
        neg = 1;
        val = -val;
    }

    /* Get integer part */
    long long int_part = (long long)val;
    double frac_part = val - (double)int_part;

    /* Convert integer part */
    char temp[32];
    int idx = 0;
    if (int_part == 0) {
        temp[idx++] = '0';
    } else {
        while (int_part > 0 && idx < 31) {
            temp[idx++] = '0' + (int_part % 10);
            int_part /= 10;
        }
    }

    /* Add to buffer (reversed) */
    size_t pos = 0;
    if (neg && pos < buflen - 1) buf[pos++] = '-';
    for (int i = idx - 1; i >= 0 && pos < buflen - 1; --i) {
        buf[pos++] = temp[i];
    }

    /* Add decimal part if non-zero */
    if (frac_part > 0.0000001 && pos < buflen - 1) {
        buf[pos++] = '.';
        for (int i = 0; i < 6 && pos < buflen - 1; ++i) {
            frac_part *= 10.0;
            int digit = (int)frac_part;
            buf[pos++] = '0' + digit;
            frac_part -= digit;
        }
        /* Trim trailing zeros */
        while (pos > 0 && buf[pos - 1] == '0') pos--;
        if (pos > 0 && buf[pos - 1] == '.') pos--;
    }

    buf[pos] = '\0';
}

/* Evaluate expression using Dijkstra's shunting-yard algorithm */
static int evaluate_expr(const char *expr, double *result) {
    ValueStack values = { .top = -1 };
    OpStack ops = { .top = -1 };

    const char *ptr = expr;

    while (*ptr) {
        /* Skip whitespace */
        if (*ptr == ' ') {
            ptr++;
            continue;
        }

        /* Parse number */
        if (is_digit(*ptr)) {
            double num = 0;
            while (is_digit(*ptr)) {
                num = num * 10 + (*ptr - '0');
                ptr++;
            }
            /* Handle decimal point */
            if (*ptr == '.') {
                ptr++;
                double frac = 0.1;
                while (is_digit(*ptr)) {
                    num += (*ptr - '0') * frac;
                    frac *= 0.1;
                    ptr++;
                }
            }
            value_push(&values, num);
            continue;
        }

        /* Handle opening bracket */
        if (*ptr == '(') {
            op_push(&ops, '(');
            ptr++;
            continue;
        }

        /* Handle closing bracket */
        if (*ptr == ')') {
            while (ops.top >= 0 && op_peek(&ops) != '(') {
                char op = op_pop(&ops);
                double b = value_pop(&values);
                double a = value_pop(&values);
                value_push(&values, apply_op(op, b, a));
            }
            if (ops.top >= 0) op_pop(&ops); /* Remove '(' */
                ptr++;
            continue;
        }

        /* Handle operator */
        if (is_operator(*ptr)) {
            char current_op = *ptr;
            while (ops.top >= 0 && op_peek(&ops) != '(' &&
                precedence(op_peek(&ops)) >= precedence(current_op)) {
                char op = op_pop(&ops);
            double b = value_pop(&values);
            double a = value_pop(&values);
            value_push(&values, apply_op(op, b, a));
                }
                op_push(&ops, current_op);
                ptr++;
                continue;
        }

        /* Unknown character - skip */
        ptr++;
    }

    /* Apply remaining operators */
    while (ops.top >= 0) {
        char op = op_pop(&ops);
        if (op == '(' || op == ')') continue;
        double b = value_pop(&values);
        double a = value_pop(&values);
        value_push(&values, apply_op(op, b, a));
    }

    if (values.top == 0) {
        *result = values.values[0];
        return 0;
    }

    return -1; /* Error */
}

/* Redraw calculator screen */
static void calc_redraw(void) {
    if (!callbacks.clear_screen || !callbacks.draw_char) return;

    callbacks.clear_screen();

    /* Title */
    const char *title = "=== CALCULATOR ===";
    size_t col = 0;
    for (const char *p = title; *p; p++) {
        callbacks.draw_char(0, col++, *p, 0x0F);
    }

    /* Instructions */
    const char *instr = "Enter expression and press Enter. Ctrl+Q to quit.";
    col = 0;
    for (const char *p = instr; *p; p++) {
        callbacks.draw_char(1, col++, *p, 0x07);
    }

    /* Expression input line */
    const char *prompt = "> ";
    col = 0;
    for (const char *p = prompt; *p; p++) {
        callbacks.draw_char(3, col++, *p, 0x0A);
    }

    /* Draw expression */
    for (size_t i = 0; i < expr_len; ++i) {
        callbacks.draw_char(3, col++, expr_buf[i], 0x0F);
    }

    /* Draw cursor */
    callbacks.draw_char(3, col, '_', 0x0F);
}

void calc_init(void) {
    expr_len = 0;
    calc_active = 0;
}

void calc_set_callbacks(calc_callbacks_t *cb) {
    callbacks = *cb;
}

void calc_start(void) {
    calc_active = 1;
    expr_len = 0;
    calc_redraw();
}

int calc_is_active(void) {
    return calc_active;
}

int calc_handle_scancode(uint8_t scancode) {
    static int shift_down = 0;
    static int ctrl_down = 0;

    /* Track shift keys */
    if (scancode == 0x2A || scancode == 0x36) { shift_down = 1; return 1; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_down = 0; return 1; }

    /* Track ctrl key */
    if (scancode == 0x1D) { ctrl_down = 1; return 1; }
    if (scancode == 0x9D) { ctrl_down = 0; return 1; }

    /* Ignore key release events */
    if (scancode & 0x80) return 1;

    /* Handle Ctrl+Q to quit */
    if (ctrl_down && (scancode == 0x10)) { /* Q key */
        calc_active = 0;
        return 0;
    }

    /* Handle backspace */
    if (scancode == 0x0E) {
        if (expr_len > 0) {
            expr_len--;
            calc_redraw();
        }
        return 1;
    }

    /* Handle Enter - evaluate expression */
    if (scancode == 0x1C) {
        expr_buf[expr_len] = '\0';

        double result;
        if (evaluate_expr(expr_buf, &result) == 0) {
            char result_str[64];
            double_to_str(result, result_str, sizeof(result_str));

            /* Display result */
            const char *res_label = "Result: ";
            size_t col = 0;
            for (const char *p = res_label; *p; p++) {
                callbacks.draw_char(5, col++, *p, 0x0E);
            }
            for (const char *p = result_str; *p; p++) {
                callbacks.draw_char(5, col++, *p, 0x0F);
            }
        } else {
            const char *err = "Error: Invalid expression";
            size_t col = 0;
            for (const char *p = err; *p; p++) {
                callbacks.draw_char(5, col++, *p, 0x0C);
            }
        }

        /* Clear expression for next input */
        expr_len = 0;
        calc_redraw();
        return 1;
    }

    /* Map scancode to character */
    char c = 0;

    /* Numbers */
    if (scancode >= 0x02 && scancode <= 0x0B) {
        static const char nums[] = "1234567890";
        c = nums[scancode - 0x02];
    }
    /* Operators */
    else if (scancode == 0x0C) c = shift_down ? '_' : '-';
    else if (scancode == 0x0D) c = shift_down ? '+' : '=';
    else if (scancode == 0x35) c = '/';
    else if (scancode == 0x09) c = shift_down ? '*' : '8';
    else if (scancode == 0x34) c = '.';
    else if (scancode == 0x1A) c = '(';
    else if (scancode == 0x1B) c = ')';
    else if (scancode == 0x39) c = ' ';

    /* Add character to expression if valid */
    if (c && expr_len < MAX_EXPR_LEN - 1) {
        /* Only allow valid calculator characters */
        if (is_digit(c) || is_operator(c) || c == '(' || c == ')' || c == '.' || c == ' ') {
            expr_buf[expr_len++] = c;
            calc_redraw();
        }
    }

    return 1;
}
