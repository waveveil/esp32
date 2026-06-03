#pragma once

#include <stdint.h>

void display_init(void);
void display_clear(void);

// line: 0-7, 8x8 font → 16 chars/line
void display_set_line(int line, const char *text);

// update volume bar + status
void display_set_volume(int vol);
void display_set_status(const char *status);
void display_refresh(void);
