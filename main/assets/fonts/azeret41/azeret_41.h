// Header (.h) for font: b'Azeret Mono' b'SemiBold' 41

#include <stdint.h>

extern const int TALLEST_CHAR_PIXELS;

extern const uint8_t b_azeret_41_font_pixels[];

struct font_char
{
    int offset;
    int w;
    int h;
    int left;
    int top;
    int advance;
};

extern const struct font_char b_azeret_41_font_lookup[];