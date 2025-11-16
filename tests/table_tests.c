// Test and demonstration for table printing and width calculation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/table.h"
#include "../include/common.h"

int main(void) {
    Table t;
    table_init(&t);

    // Ensure colors off for deterministic spacing checks
    enable_colors = 0;

    table_add_column(&t, "Name", ALIGN_LEFT);
    table_add_column(&t, "Age", ALIGN_RIGHT);
    table_add_column(&t, "Note", ALIGN_RIGHT);

    table_add_row(&t);
    table_add_row(&t);

    table_set_cell(&t, 0, 0, "Alice");
    table_set_cell_int(&t, 0, 1, 30);
    table_set_cell(&t, 0, 2, "\033[32mActive\033[0m"); // contains ANSI codes

    table_set_cell(&t, 1, 0, "Bob");
    table_set_cell_int(&t, 1, 1, 7);
    table_set_cell(&t, 1, 2, "On leave");

    // Programmatic checks: each column actual_width must equal the max visual_strlen
    int ok = 1;
    for (int c = 0; c < t.num_columns; c++) {
        int maxlen = visual_strlen(t.columns[c].header);
        for (int r = 0; r < t.num_rows; r++) {
            int l = visual_strlen(t.rows[r][c].data);
            if (l > maxlen) maxlen = l;
        }
        if (t.columns[c].actual_width != maxlen) {
            printf("Width mismatch column %d (header='%s'): actual=%d expected=%d\n",
                   c, t.columns[c].header, t.columns[c].actual_width, maxlen);
            ok = 0;
        } else {
            printf("Column %d width OK: %d\n", c, maxlen);
        }
    }

    printf("\nPrinted table (colors disabled):\n");
    table_print(&t);

    // Now enable colors and print again for visual inspection
    enable_colors = 1;
    printf("\nPrinted table (colors enabled):\n");
    table_print(&t);

    return ok ? 0 : 1;
}
