#pragma once
#include <stddef.h>

// The caller supplies rounded tenths of a dBFS in [-900, 0].
void ui_format_dbfs(char *buffer, size_t size, int tenths);
