/* GPL-3.0-only: test of the GPL-3.0-only Nova-derived color component. */
#include <stdio.h>
#include <string.h>
#include "../ntk_color.h"

static int failures;

#define CHECK(name, actual, expected) do { \
    NtkColor got_ = (actual); \
    NtkColor want_ = (expected); \
    if (got_ != want_) { \
        fprintf(stderr, "%s: got 0x%08x, expected 0x%08x\n", \
                (name), (unsigned)got_, (unsigned)want_); \
        failures++; \
    } \
} while (0)

int main(void)
{
    uint8_t r, g, b, a;
    CHECK("rgb packing", ntk_color_from_rgb(0x12, 0x34, 0x56), 0xff123456u);
    CHECK("rgba packing", ntk_color_from_rgba(0x12, 0x34, 0x56, 0x78), 0x78123456u);
    CHECK("hex rgb", ntk_color_from_hex("#12ABef"), 0xff12abefu);
    CHECK("hex short", ntk_color_from_hex("#f06"), 0xffff0066u);
    CHECK("hex rgba", ntk_color_from_hex("80112233"), 0x80112233u);
    CHECK("invalid hex", ntk_color_from_hex("nope"), 0xff000000u);
    CHECK("lerp start", ntk_color_lerp_u8(0xff000000u, 0xffffffffu, 0), 0xff000000u);
    CHECK("lerp end", ntk_color_lerp_u8(0xff000000u, 0xffffffffu, 255), 0xffffffffu);
    CHECK("lerp midpoint", ntk_color_lerp_u8(0xff000000u, 0xffffffffu, 128), 0xff808080u);
    CHECK("alpha lerp", ntk_color_lerp_u8(0x00000000u, 0xff000000u, 128), 0x80000000u);
    CHECK("set alpha", ntk_color_with_alpha(0x00123456u, 0xabu), 0xab123456u);
    ntk_color_to_rgba(0x78123456u, &r, &g, &b, &a);
    if (r != 0x12 || g != 0x34 || b != 0x56 || a != 0x78) {
        fprintf(stderr, "rgba extraction failed\n");
        failures++;
    }
    if (strcmp(ntk_color_to_hex(0x80112233u), "#80112233") != 0) {
        fprintf(stderr, "hex formatting failed: %s\n", ntk_color_to_hex(0x80112233u));
        failures++;
    }
    if (failures) return 1;
    puts("Nova color component: 13 checks passed");
    return 0;
}
