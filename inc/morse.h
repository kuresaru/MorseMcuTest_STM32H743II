#ifndef __MORSE_H
#define __MORSE_H

#include <stdint.h>

typedef uint8_t morse_t;

void morse_rst(morse_t *m);
void morse_dit(morse_t *m);
void morse_dam(morse_t *m);
char morse_get(morse_t *m);

#endif