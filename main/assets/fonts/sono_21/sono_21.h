// Header (.h) for font: b'Sono' b'ExtraLight' 25

#include <stdint.h>

extern const int TALLEST_CHAR_PIXELS;

extern const uint8_t b_sono_21_font_pixels[];

struct font_char
{
    int offset;
    int w;
    int h;
    int left;
    int top;
    int advance;
};

extern const struct font_char b_sono_21_font_lookup[];
