#ifndef NOVA_GRAPHICS_H
#define NOVA_GRAPHICS_H

#include "types.h"

#define GFX_MAX_WIDTH 1024
#define GFX_MAX_HEIGHT 768
#define GFX_FONT_WIDTH 8
#define GFX_FONT_HEIGHT 12
#define GFX_FONT_ADVANCE 8

typedef struct {
    int x;
    int y;
    int width;
    int height;
} Rect;

bool gfx_init(void);
int gfx_width(void);
int gfx_height(void);
void gfx_generate_wallpaper(uint32_t accent, bool dark_mode);
void gfx_begin_frame(void);
void gfx_save_base(void);
void gfx_save_base_rect(int x, int y, int width, int height);
void gfx_restore_base_rect(int x, int y, int width, int height);
void gfx_present(void);
void gfx_present_rect(int x, int y, int width, int height);
void gfx_pixel(int x, int y, uint32_t color);
uint32_t gfx_get_pixel(int x, int y);
void gfx_fill_rect(int x, int y, int width, int height, uint32_t color);
void gfx_fill_rect_alpha(int x, int y, int width, int height, uint32_t color, uint8_t alpha);
void gfx_round_rect(int x, int y, int width, int height, int radius, uint32_t color);
void gfx_round_rect_alpha(int x, int y, int width, int height, int radius, uint32_t color, uint8_t alpha);
void gfx_shadow(int x, int y, int width, int height, int radius);
void gfx_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_circle(int center_x, int center_y, int radius, uint32_t color);
void gfx_text(int x, int y, const char *text, uint32_t color, int scale);
void gfx_text_clipped(int x, int y, const char *text, uint32_t color, int scale, Rect clip);
int gfx_text_width(const char *text, int scale);
uint32_t gfx_mix(uint32_t background, uint32_t foreground, uint8_t alpha);

#endif
