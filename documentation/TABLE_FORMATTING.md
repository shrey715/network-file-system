# Table Formatting System

## Overview

A POSIX-compliant table formatting library that provides professional, auto-sizing tabular output for the distributed file system client.

## Features

### Auto-sizing Columns
- Columns automatically adjust to content width
- Minimum width based on header text
- Dynamic calculation of actual required width

### Column Alignment
- **ALIGN_LEFT**: Left-aligned text (default for strings)
- **ALIGN_RIGHT**: Right-aligned text (default for numbers)
- **ALIGN_CENTER**: Center-aligned text

### Color Support
- Integrates with existing ANSI color system
- Bold cyan headers when colors enabled
- Dimmed separator lines

### Type-Safe Helpers
- `table_set_cell()` - String values
- `table_set_cell_int()` - Integer values
- `table_set_cell_long()` - Long integer values

## API Reference

### Initialization
```c
Table table;
table_init(&table);
```

### Building Tables
```c
// Add columns
table_add_column(&table, "Filename", ALIGN_LEFT);
table_add_column(&table, "Size", ALIGN_RIGHT);

// Add rows
table_add_row(&table);
table_set_cell(&table, 0, 0, "example.txt");
table_set_cell_int(&table, 0, 1, 1024);
```

### Display
```c
table_print(&table);
table_free(&table);  // Cleanup
```

## Example Output

### Before (Old Format)
```
Filename             Words Chars      Last Access Owner
------------------------------------------------------------
(null)> 
```

### After (New Table Format)
```
Filename      Words  Chars  Last Access       Owner
----------  -------  -----  ----------------  -------
1.txt             4     22  2025-11-17-03:41  shreyas
hello.txt         0      0  2025-11-17-03:41  shreyas
```

## Implementation Details

### Files Created
- `include/table.h` - Table API declarations
- `src/common/table.c` - Table implementation
- Updated `src/client/commands.c` - Use table for `file list -l`

### Memory Management
- Stack-allocated table structures (no dynamic allocation)
- Maximum 10 columns, 1000 rows per table
- Maximum 256 characters per cell

### POSIX Compliance
Uses only standard C library functions:
- `memset()`, `strlen()`, `strncpy()`, `snprintf()`
- `printf()` family for output
- No GNU extensions or external libraries

## Usage in Commands

### File List Command
```bash
> file list -l
```

Produces properly formatted table with:
- Left-aligned filenames
- Right-aligned numeric columns
- Consistent spacing
- Color-coded headers

## Benefits

1. **Professional appearance** - Clean, aligned output
2. **Maintainable** - Centralized formatting logic
3. **Reusable** - Easy to add tables to other commands
4. **Extensible** - Simple to add features like borders, sorting
5. **Efficient** - No dynamic allocation, fast rendering

## Future Enhancements

### Possible additions:
- Border styles (ASCII, Unicode)
- Column width constraints
- Cell truncation with ellipsis
- Multi-line cell content
- Custom cell formatters
- Sorting by column
- Export to CSV/TSV

## Integration Notes

The table system integrates seamlessly with:
- Existing `enable_colors` global flag
- ANSI color code definitions
- POSIX-only requirement
- Error-free compilation with `-Wall -Wextra`
