#include "suites.h"

#include <math.h>

#include "canvas.h"
#include "test_harness.h"

/* Canvas tests: 16-tone quantization (SPEC 1.1 / 8) and the half-open rectangle
 * edges the design rects rely on. */

static int is_tone(uint8_t v)
{
    return (int)v % 17 == 0;
}

static void test_quantize_boundaries(void)
{
    static const struct {
        int in;
        int out;
    } cases[] = {
        { 0, 0 },     { 8, 0 },     { 9, 17 },    { 16, 17 },
        { 17, 17 },   { 25, 17 },   { 26, 34 },   { 33, 34 },
        { 34, 34 },   { 42, 34 },   { 43, 51 },   { 246, 238 },
        { 247, 255 }, { 254, 255 }, { 255, 255 }
    };
    Canvas c;
    size_t i;

    KH_CHECK(canvas_init(&c, 1, 1));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        canvas_clear(&c, 255);
        canvas_fill_rect(&c, 0, 0, 1, 1, (uint8_t)cases[i].in);
        canvas_quantize16(&c);
        KH_CHECK_MSG(c.pixels[0] == cases[i].out,
                     "quantize %d: expected %d, got %d", cases[i].in,
                     cases[i].out, c.pixels[0]);
    }
    canvas_free(&c);
}

static void test_quantize_all_input_values(void)
{
    Canvas c;
    int v;

    KH_CHECK(canvas_init(&c, 256, 1));
    for (v = 0; v < 256; v++) {
        c.pixels[v] = (uint8_t)v;
    }
    canvas_quantize16(&c);
    for (v = 0; v < 256; v++) {
        int expected = ((v + 8) / 17) * 17;
        if (expected > 255) {
            expected = 255;
        }
        KH_CHECK_MSG(c.pixels[v] == expected,
                     "quantize %d: expected %d, got %d", v, expected,
                     c.pixels[v]);
        KH_CHECK_MSG(is_tone(c.pixels[v]),
                     "quantize %d produced non-16-tone value %d", v,
                     c.pixels[v]);
    }
    canvas_free(&c);
}

static void test_quantize_idempotent(void)
{
    Canvas c;
    int v;
    int t;

    KH_CHECK(canvas_init(&c, 16, 1));
    for (t = 0; t < 16; t++) {
        v = t * 17;
        c.pixels[t] = (uint8_t)v;
    }
    canvas_quantize16(&c);
    for (t = 0; t < 16; t++) {
        KH_CHECK_MSG(c.pixels[t] == t * 17,
                     "16-tone value %d changed to %d", t * 17, c.pixels[t]);
    }
    canvas_free(&c);
}

/* Adjacent half-open rectangles must tile exactly: no 255-coloured seam and no
 * later rectangle overwriting an earlier one's final column. */
static void test_adjacent_rect_edges(void)
{
    static const int edges[] = { 0, 10, 17, 32, 45, 64 };
    static const uint8_t values[] = { 0, 85, 170, 0, 85 };
    Canvas c;
    int i;
    int x;

    KH_CHECK(canvas_init(&c, 64, 8));
    canvas_clear(&c, 255);
    for (i = 0; i < 5; i++) {
        canvas_fill_rect(&c, edges[i], 0, edges[i + 1] - edges[i], 8,
                         values[i]);
    }
    for (i = 0; i < 5; i++) {
        for (x = edges[i]; x < edges[i + 1]; x++) {
            int y;
            for (y = 0; y < 8; y++) {
                uint8_t got = c.pixels[y * c.stride + x];
                KH_CHECK_MSG(got == values[i],
                             "rect %d at x=%d,y=%d: expected %d, got %d "
                             "(gap or overlap at a rectangle edge)",
                             i, x, y, values[i], got);
            }
        }
    }
    for (x = 64; x < c.w; x++) {
        KH_CHECK(c.pixels[x] == 255);
    }
    canvas_free(&c);
}

/* The 16 brightness steps are 40 px wide design rects starting at x=64.  At
 * device scale the rounded right edge of one step must equal the rounded left
 * edge of the next, or the panel gets a one-pixel white seam. */
static void test_device_scale_rect_edges(void)
{
    const double scale = 758.0 / 1072.0;
    static const uint8_t values[] = { 0, 85, 170 };
    Canvas c;
    int previous_right = -1;
    int first_left = 0;
    int i;

    KH_CHECK(canvas_init(&c, 758, 16));
    canvas_clear(&c, 255);
    for (i = 0; i < 16; i++) {
        int left = (int)lround((64.0 + 40.0 * i) * scale);
        int right = (int)lround((64.0 + 40.0 * (i + 1)) * scale);
        int x;
        if (i == 0) {
            first_left = left;
        }
        KH_CHECK_MSG(right > left, "brightness step %d: bad device edges %d..%d",
                     i, left, right);
        if (previous_right >= 0) {
            KH_CHECK_MSG(left == previous_right,
                         "brightness step %d: left edge %d does not meet the "
                         "previous right edge %d", i, left, previous_right);
        }
        canvas_fill_rect(&c, left, 0, right - left, 16, values[i % 3]);
        for (x = left; x < right; x++) {
            KH_CHECK_MSG(c.pixels[x] == values[i % 3],
                         "brightness step %d at x=%d: expected %d, got %d",
                         i, x, values[i % 3], c.pixels[x]);
        }
        previous_right = right;
    }
    KH_CHECK_MSG(previous_right == (int)lround(704.0 * scale),
                 "brightness bar right edge: expected %d, got %d",
                 (int)lround(704.0 * scale), previous_right);
    KH_CHECK_MSG(c.pixels[first_left] == 0,
                 "brightness bar left edge did not start at %d", first_left);
    canvas_free(&c);
}

void kh_run_canvas_tests(void)
{
    printf("\n[canvas]\n");
    kh_run_test("canvas.quantize_boundaries", test_quantize_boundaries);
    kh_run_test("canvas.quantize_all_input_values",
                test_quantize_all_input_values);
    kh_run_test("canvas.quantize_idempotent", test_quantize_idempotent);
    kh_run_test("canvas.adjacent_rect_edges", test_adjacent_rect_edges);
    kh_run_test("canvas.device_scale_rect_edges",
                test_device_scale_rect_edges);
}
