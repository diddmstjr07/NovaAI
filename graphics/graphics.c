#include "graphics.h"
#include "runtime.h"

extern const uint8_t nova_font_alpha_1[96][GFX_FONT_WIDTH * GFX_FONT_HEIGHT];
extern const uint8_t nova_font_alpha_2[96][GFX_FONT_WIDTH * GFX_FONT_HEIGHT * 4];
extern const uint8_t nova_font_alpha_3[96][GFX_FONT_WIDTH * GFX_FONT_HEIGHT * 9];

typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t window_a;
    uint8_t window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t window_function;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t character_width;
    uint8_t character_height;
    uint8_t planes;
    uint8_t bits_per_pixel;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t reserved_mask;
    uint8_t reserved_position;
    uint8_t direct_color_attributes;
    uint32_t framebuffer;
} VbeModeInfo;

static volatile VbeModeInfo *const mode = (volatile VbeModeInfo *)0x6000;
static uint32_t frame[GFX_MAX_WIDTH * GFX_MAX_HEIGHT];
static uint32_t wallpaper[GFX_MAX_WIDTH * GFX_MAX_HEIGHT];
static uint32_t base_frame[GFX_MAX_WIDTH * GFX_MAX_HEIGHT];
static int screen_width;
static int screen_height;
static volatile uint8_t *video_memory;

static void copy_pixels(uint32_t *destination, const uint32_t *source, size_t count) {
    while (count--) {
        *destination++ = *source++;
    }
}

static void copy_pixels_to_video(volatile uint32_t *destination,
                                 const uint32_t *source, uint32_t count) {
    __asm__ volatile ("cld; rep movsl"
                      : "+D"(destination), "+S"(source), "+c"(count)
                      :
                      : "memory");
}

static int absolute(int value) {
    return value < 0 ? -value : value;
}

static uint32_t scale_channel(uint32_t value, uint8_t bits) {
    if (bits >= 8) {
        return value << (bits - 8);
    }
    return value >> (8 - bits);
}

static uint32_t native_color(uint32_t color) {
    uint32_t red = (color >> 16) & 0xFF;
    uint32_t green = (color >> 8) & 0xFF;
    uint32_t blue = color & 0xFF;
    return (scale_channel(red, mode->red_mask) << mode->red_position) |
           (scale_channel(green, mode->green_mask) << mode->green_position) |
           (scale_channel(blue, mode->blue_mask) << mode->blue_position);
}

bool gfx_init(void) {
    screen_width = mode->width;
    screen_height = mode->height;
    video_memory = (volatile uint8_t *)(uintptr_t)mode->framebuffer;
    if (!(mode->attributes & 0x80) || !video_memory) {
        return false;
    }
    if (screen_width < 640 || screen_height < 480 ||
        screen_width > GFX_MAX_WIDTH || screen_height > GFX_MAX_HEIGHT) {
        return false;
    }
    return mode->bits_per_pixel == 24 || mode->bits_per_pixel == 32;
}

int gfx_width(void) {
    return screen_width;
}

int gfx_height(void) {
    return screen_height;
}

uint32_t gfx_mix(uint32_t background, uint32_t foreground, uint8_t alpha) {
    uint32_t inverse = 255 - alpha;
    uint32_t red = ((((background >> 16) & 0xFF) * inverse) +
                    (((foreground >> 16) & 0xFF) * alpha)) / 255;
    uint32_t green = ((((background >> 8) & 0xFF) * inverse) +
                      (((foreground >> 8) & 0xFF) * alpha)) / 255;
    uint32_t blue = (((background & 0xFF) * inverse) +
                     ((foreground & 0xFF) * alpha)) / 255;
    return (red << 16) | (green << 8) | blue;
}

void gfx_generate_wallpaper(uint32_t accent, bool dark_mode) {
    int width = screen_width;
    int height = screen_height;
    uint32_t top = dark_mode ? 0x071426 : 0xDDEEFF;
    uint32_t bottom = dark_mode ? 0x102F55 : 0x77BCEE;
    int cx1 = (width * 66) / 100;
    int cy1 = (height * 38) / 100;
    int cx2 = (width * 38) / 100;
    int cy2 = (height * 70) / 100;

    for (int y = 0; y < height; ++y) {
        uint32_t vertical = gfx_mix(top, bottom, (uint8_t)((y * 170) / height));
        for (int x = 0; x < width; ++x) {
            int dx1 = x - cx1;
            int dy1 = y - cy1;
            int dx2 = x - cx2;
            int dy2 = y - cy2;
            int distance1 = (dx1 * dx1) / 5 + (dy1 * dy1);
            int distance2 = (dx2 * dx2) / 3 + (dy2 * dy2);
            uint8_t glow1 = distance1 < 90000 ? (uint8_t)(150 - (distance1 * 150) / 90000) : 0;
            uint8_t glow2 = distance2 < 65000 ? (uint8_t)(100 - (distance2 * 100) / 65000) : 0;
            uint32_t color = gfx_mix(vertical, accent, glow1);
            color = gfx_mix(color, dark_mode ? 0x0C86FF : 0xFFFFFF, glow2);
            wallpaper[y * width + x] = color;
        }
    }
}

void gfx_begin_frame(void) {
    copy_pixels(frame, wallpaper, (size_t)(screen_width * screen_height));
}

void gfx_save_base(void) {
    copy_pixels(base_frame, frame, (size_t)(screen_width * screen_height));
}

void gfx_save_base_rect(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0 || x >= screen_width || y >= screen_height ||
        x + width <= 0 || y + height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > screen_width ? screen_width : x + width;
    int y1 = y + height > screen_height ? screen_height : y + height;
    for (int row = y0; row < y1; ++row) {
        copy_pixels(base_frame + row * screen_width + x0,
                    frame + row * screen_width + x0,
                    (size_t)(x1 - x0));
    }
}

void gfx_restore_base_rect(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0 || x >= screen_width || y >= screen_height ||
        x + width <= 0 || y + height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > screen_width ? screen_width : x + width;
    int y1 = y + height > screen_height ? screen_height : y + height;
    for (int row = y0; row < y1; ++row) {
        copy_pixels(frame + row * screen_width + x0,
                    base_frame + row * screen_width + x0,
                    (size_t)(x1 - x0));
    }
}

void gfx_present_rect(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0 || x >= screen_width || y >= screen_height ||
        x + width <= 0 || y + height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > screen_width ? screen_width : x + width;
    int y1 = y + height > screen_height ? screen_height : y + height;
    int bytes_per_pixel = mode->bits_per_pixel / 8;
    bool native_bgr = mode->bits_per_pixel == 32 && mode->red_mask == 8 &&
                      mode->green_mask == 8 && mode->blue_mask == 8 &&
                      mode->red_position == 16 && mode->green_position == 8 &&
                      mode->blue_position == 0;
    for (int row = y0; row < y1; ++row) {
        volatile uint8_t *destination = video_memory + row * mode->pitch;
        const uint32_t *source = frame + row * screen_width;
        if (native_bgr) {
            copy_pixels_to_video((volatile uint32_t *)destination + x0,
                                 source + x0, (uint32_t)(x1 - x0));
        } else {
            for (int column = x0; column < x1; ++column) {
                uint32_t packed = native_color(source[column]);
                for (int byte = 0; byte < bytes_per_pixel; ++byte) {
                    destination[column * bytes_per_pixel + byte] = (uint8_t)(packed >> (byte * 8));
                }
            }
        }
    }
}

void gfx_present(void) {
    gfx_present_rect(0, 0, screen_width, screen_height);
}

void gfx_pixel(int x, int y, uint32_t color) {
    if ((uint32_t)x < (uint32_t)screen_width && (uint32_t)y < (uint32_t)screen_height) {
        frame[y * screen_width + x] = color;
    }
}

uint32_t gfx_get_pixel(int x, int y) {
    if ((uint32_t)x < (uint32_t)screen_width && (uint32_t)y < (uint32_t)screen_height) {
        return frame[y * screen_width + x];
    }
    return 0;
}

void gfx_fill_rect(int x, int y, int width, int height, uint32_t color) {
    if (width <= 0 || height <= 0 || x >= screen_width || y >= screen_height ||
        x + width <= 0 || y + height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > screen_width ? screen_width : x + width;
    int y1 = y + height > screen_height ? screen_height : y + height;
    for (int row = y0; row < y1; ++row) {
        uint32_t *pixel = frame + row * screen_width + x0;
        for (int column = x0; column < x1; ++column) {
            *pixel++ = color;
        }
    }
}

void gfx_fill_rect_alpha(int x, int y, int width, int height, uint32_t color, uint8_t alpha) {
    if (width <= 0 || height <= 0 || x >= screen_width || y >= screen_height ||
        x + width <= 0 || y + height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > screen_width ? screen_width : x + width;
    int y1 = y + height > screen_height ? screen_height : y + height;
    for (int row = y0; row < y1; ++row) {
        for (int column = x0; column < x1; ++column) {
            uint32_t *pixel = &frame[row * screen_width + column];
            *pixel = gfx_mix(*pixel, color, alpha);
        }
    }
}

static bool inside_round_rect(int px, int py, int width, int height, int radius) {
    int near_x = px < radius ? radius : (px >= width - radius ? width - radius - 1 : px);
    int near_y = py < radius ? radius : (py >= height - radius ? height - radius - 1 : py);
    int dx = px - near_x;
    int dy = py - near_y;
    return dx * dx + dy * dy <= radius * radius;
}

void gfx_round_rect(int x, int y, int width, int height, int radius, uint32_t color) {
    if (radius < 1) {
        gfx_fill_rect(x, y, width, height, color);
        return;
    }
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            if (inside_round_rect(px, py, width, height, radius)) {
                gfx_pixel(x + px, y + py, color);
            }
        }
    }
}

void gfx_round_rect_alpha(int x, int y, int width, int height, int radius, uint32_t color, uint8_t alpha) {
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            if (inside_round_rect(px, py, width, height, radius)) {
                int screen_x = x + px;
                int screen_y = y + py;
                gfx_pixel(screen_x, screen_y, gfx_mix(gfx_get_pixel(screen_x, screen_y), color, alpha));
            }
        }
    }
}

void gfx_shadow(int x, int y, int width, int height, int radius) {
    for (int spread = 16; spread >= 2; spread -= 2) {
        uint8_t alpha = (uint8_t)(5 + (16 - spread));
        gfx_round_rect_alpha(x - spread / 2, y + spread / 3,
                             width + spread, height + spread, radius + spread / 2,
                             0x000000, alpha);
    }
}

void gfx_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = absolute(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -absolute(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void gfx_circle(int center_x, int center_y, int radius, uint32_t color) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                gfx_pixel(center_x + x, center_y + y, color);
            }
        }
    }
}

static void draw_character(int x, int y, char character, uint32_t color, int scale, Rect clip) {
    uint8_t code = (uint8_t)character;
    if (code < 32 || code > 127) character = '?';
    if (scale < 1) scale = 1;
    if (scale > 3) scale = 3;
    int glyph = (uint8_t)character - 32;
    const uint8_t *pixels;
    if (scale == 1) pixels = nova_font_alpha_1[glyph];
    else if (scale == 2) pixels = nova_font_alpha_2[glyph];
    else pixels = nova_font_alpha_3[glyph];

    int width = GFX_FONT_WIDTH * scale;
    int height = GFX_FONT_HEIGHT * scale;
    for (int row = 0; row < height; ++row) {
        int py = y + row;
        if (py < clip.y || py >= clip.y + clip.height) continue;
        for (int column = 0; column < width; ++column) {
            int px = x + column;
            if (px < clip.x || px >= clip.x + clip.width) continue;
            uint8_t alpha = pixels[row * width + column];
            if (alpha) {
                gfx_pixel(px, py, alpha == 255 ? color :
                          gfx_mix(gfx_get_pixel(px, py), color, alpha));
            }
        }
    }
}

void gfx_text_clipped(int x, int y, const char *text, uint32_t color, int scale, Rect clip) {
    int cursor_x = x;
    while (*text) {
        if (*text == '\n') {
            cursor_x = x;
            y += (GFX_FONT_HEIGHT + 2) * scale;
        } else {
            draw_character(cursor_x, y, *text, color, scale, clip);
            cursor_x += GFX_FONT_ADVANCE * scale;
        }
        ++text;
    }
}

void gfx_text(int x, int y, const char *text, uint32_t color, int scale) {
    Rect screen = {0, 0, screen_width, screen_height};
    gfx_text_clipped(x, y, text, color, scale, screen);
}

int gfx_text_width(const char *text, int scale) {
    return (int)strlen(text) * GFX_FONT_ADVANCE * scale;
}
