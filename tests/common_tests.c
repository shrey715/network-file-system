// Unit tests for visual_strlen in src/common/utils.c
#include <stdio.h>
#include <stdlib.h>
#include "../include/common.h"

static int failures = 0;

static void expect_int(const char* name, int got, int want) {
    if (got != want) {
        printf("FAIL: %s - got %d want %d\n", name, got, want);
        failures++;
    } else {
        printf("ok:   %s - %d\n", name, got);
    }
}

int main(void) {
    // Basic cases
    expect_int("NULL input", visual_strlen(NULL), 0);
    expect_int("empty string", visual_strlen(""), 0);
    expect_int("plain ascii", visual_strlen("hello"), 5);

    // ANSI colored string: \033[31mRed\033[0m -> should count only 'Red' (3)
    expect_int("ansi color around text", visual_strlen("\033[31mRed\033[0m"), 3);

    // Multiple ANSI sequences before and after
    expect_int("multiple ansi sequences", visual_strlen("\033[1m\033[32mHi\033[0m"), 2);

    // Malformed ANSI sequence (no terminating 'm') should cause the remainder
    // of the string after ESC to be skipped. E.g. "A\033[31X" -> only 'A' counted
    expect_int("malformed ansi sequence", visual_strlen("A\033[31X"), 1);

    // ANSI sequence in middle
    expect_int("ansi in middle", visual_strlen("Hello \033[31mWorld\033[0m!"), 12); // "Hello " (6) + "World" (5) + "!" (1)

    if (failures == 0) {
        printf("All visual_strlen tests passed.\n");
        return 0;
    } else {
        printf("%d visual_strlen test(s) failed.\n", failures);
        return 1;
    }
}
