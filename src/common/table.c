#include "table.h"
#include "common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void table_init(Table* table) {
    memset(table, 0, sizeof(Table));
    table->num_columns = 0;
    table->num_rows = 0;
    table->use_colors = enable_colors;
}

void table_add_column(Table* table, const char* header, ColumnAlignment align) {
    if (table->num_columns >= MAX_TABLE_COLS) {
        return;
    }
    
    int col = table->num_columns;
    strncpy(table->columns[col].header, header, MAX_CELL_LENGTH - 1);
    table->columns[col].header[MAX_CELL_LENGTH - 1] = '\0';
    table->columns[col].align = align;
    table->columns[col].min_width = visual_strlen(header);
    table->columns[col].max_width = 0;
    table->columns[col].actual_width = visual_strlen(header);
    
    table->num_columns++;
}

void table_add_row(Table* table) {
    if (table->num_rows >= MAX_TABLE_ROWS) {
        return;
    }
    table->num_rows++;
}

void table_set_cell(Table* table, int row, int col, const char* value) {
    if (row < 0 || row >= table->num_rows || 
        col < 0 || col >= table->num_columns) {
        return;
    }
    
    strncpy(table->rows[row][col].data, value, MAX_CELL_LENGTH - 1);
    table->rows[row][col].data[MAX_CELL_LENGTH - 1] = '\0';
    
    // Update column width using visual_strlen to exclude ANSI codes
    int len = visual_strlen(value);
    if (len > table->columns[col].actual_width) {
        table->columns[col].actual_width = len;
    }
}

void table_set_cell_int(Table* table, int row, int col, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    table_set_cell(table, row, col, buf);
}

void table_set_cell_long(Table* table, int row, int col, long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    table_set_cell(table, row, col, buf);
}

static void print_aligned(const char* text, int width, ColumnAlignment align, int is_header) {
    // Use visual length instead of strlen to handle ANSI codes
    int text_len = visual_strlen(text);
    int padding = width - text_len;
    
    if (padding < 0) padding = 0;
    
    // Apply color for headers
    if (is_header && enable_colors) {
        printf(ANSI_BOLD ANSI_CYAN);
    }
    
    switch (align) {
        case ALIGN_LEFT:
            printf("%s", text);
            for (int i = 0; i < padding; i++) printf(" ");
            break;
            
        case ALIGN_RIGHT:
            for (int i = 0; i < padding; i++) printf(" ");
            printf("%s", text);
            break;
            
        case ALIGN_CENTER: {
            int left_pad = padding / 2;
            int right_pad = padding - left_pad;
            for (int i = 0; i < left_pad; i++) printf(" ");
            printf("%s", text);
            for (int i = 0; i < right_pad; i++) printf(" ");
            break;
        }
    }
    
    if (is_header && enable_colors) {
        printf(ANSI_RESET);
    }
}

void table_print(Table* table) {
    if (table->num_columns == 0) {
        return;
    }
    
    // Print header row
    for (int col = 0; col < table->num_columns; col++) {
        if (col > 0) printf("  ");  // Column separator (2 spaces)
        print_aligned(table->columns[col].header, 
                     table->columns[col].actual_width,
                     table->columns[col].align,
                     1);
    }
    printf("\n");
    
    // Print separator line
    for (int col = 0; col < table->num_columns; col++) {
        if (col > 0) printf("  ");
        
        if (enable_colors) {
            printf(ANSI_BRIGHT_BLACK);
        }
        
        for (int i = 0; i < table->columns[col].actual_width; i++) {
            printf("-");
        }
        
        if (enable_colors) {
            printf(ANSI_RESET);
        }
    }
    printf("\n");
    
    // Print data rows
    for (int row = 0; row < table->num_rows; row++) {
        for (int col = 0; col < table->num_columns; col++) {
            if (col > 0) printf("  ");
            print_aligned(table->rows[row][col].data,
                         table->columns[col].actual_width,
                         table->columns[col].align,
                         0);
        }
        printf("\n");
    }
}

void table_free(Table* table) {
    // Nothing to free for stack-allocated data
    // This is here for API consistency in case we switch to dynamic allocation
    table->num_rows = 0;
    table->num_columns = 0;
}
