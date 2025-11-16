#ifndef TABLE_H
#define TABLE_H

#include <stddef.h>

#define MAX_TABLE_COLS 10
#define MAX_TABLE_ROWS 1000
#define MAX_CELL_LENGTH 256

typedef enum {
    ALIGN_LEFT,
    ALIGN_RIGHT,
    ALIGN_CENTER
} ColumnAlignment;

typedef struct {
    char data[MAX_CELL_LENGTH];
} TableCell;

typedef struct {
    char header[MAX_CELL_LENGTH];
    ColumnAlignment align;
    int min_width;
    int max_width;
    int actual_width;
} TableColumn;

typedef struct {
    TableColumn columns[MAX_TABLE_COLS];
    TableCell rows[MAX_TABLE_ROWS][MAX_TABLE_COLS];
    int num_columns;
    int num_rows;
    int use_colors;
} Table;

// Table operations
void table_init(Table* table);
void table_add_column(Table* table, const char* header, ColumnAlignment align);
void table_add_row(Table* table);
void table_set_cell(Table* table, int row, int col, const char* value);
void table_set_cell_int(Table* table, int row, int col, int value);
void table_set_cell_long(Table* table, int row, int col, long value);
void table_print(Table* table);
void table_free(Table* table);

#endif
