/* screentest's touch: scripted taps, see screentest.c */
#pragma once
#include <stdbool.h>
bool touch_get(int *x, int *y);
void touch_swallow(void);
