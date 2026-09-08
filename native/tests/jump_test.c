#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

/* The console spelling must reserve a complete host jump context, including
 * glibc's saved signal mask, and retain adjacent state across longjmp. */
int main(void) {
    struct { __jmp_buf jump; unsigned marker; } context;
    context.marker = 0x12345678;
    int result = __setjmp(&context.jump);
    if (result == 0) longjmp(&context.jump, 7);
    if (result != 7 || context.marker != 0x12345678) abort();
    puts("PASS: host jump context and adjacent state");
}
