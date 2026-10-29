/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_widgets.c
 * Description: Caller-owned desktop widgets.
 * Added: 2026-10-27
 */
/* Caller-owned desktop widgets. */

#include "desktop.h"

static void ui_copy(char *dst, const char *src, int cap)
{
    int i = 0;

    if (cap <= 0)
        return;
    while (src && src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int ui_max(int a, int b) { return a > b ? a : b; }

/* --- button ------------------------------------------------------------ */

void ui_button_init(UiButton *b, SurfaceRect rect, const char *label)
{
    if (!b)
        return;
    b->rect = rect;
    ui_copy(b->label, label, UI_LABEL_MAX);
    b->hovered = 0;
    b->active = 0;
    b->enabled = 1;
}

void ui_button_set_rect(UiButton *b, SurfaceRect rect)
{
    if (b)
        b->rect = rect;
}

void ui_button_set_state(UiButton *b, int hovered, int active)
{
    if (!b)
        return;
    b->hovered = hovered != 0;
    b->active = active != 0;
}

int ui_button_hit(const UiButton *b, int x, int y)
{
    if (!b || !b->enabled)
        return 0;
    return x >= b->rect.x && x < b->rect.x + b->rect.width &&
           y >= b->rect.y && y < b->rect.y + b->rect.height;
}

void ui_button_draw(const UiButton *b)
{
    u32 bg, fg;
    int radius;

    if (!b || b->rect.width <= 0 || b->rect.height <= 0)
        return;
    if (!b->enabled) {
        bg = TH.card2;
        fg = TH.muted;
    } else if (b->active || b->hovered) {
        bg = TH.card3;
        fg = TH.text;
    } else {
        bg = TH.card2;
        fg = TH.dim;
    }
    radius = b->rect.height / 2;
    if (radius > 4)
        radius = 4;
    surface_set_color(&g_surface, bg);
    surface_fill_round(&g_surface, b->rect, radius);
    surface_set_color(&g_surface, TH.border);
    surface_stroke_round(&g_surface, b->rect, radius);
    text8_center(b->label, b->rect.x + b->rect.width / 2,
                 b->rect.y + (b->rect.height - 8) / 2, 1, fg);
}

/* --- progress ---------------------------------------------------------- */

void ui_progress_init(UiProgress *p, SurfaceRect rect)
{
    if (!p)
        return;
    p->rect = rect;
    p->value = 0;
    p->text[0] = '\0';
}

void ui_progress_set_value(UiProgress *p, int value)
{
    if (!p)
        return;
    if (value < 0)
        value = 0;
    if (value > 100)
        value = 100;
    p->value = value;
}

void ui_progress_set_text(UiProgress *p, const char *text)
{
    if (p)
        ui_copy(p->text, text, (int)sizeof(p->text));
}

void ui_progress_draw(const UiProgress *p)
{
    int radius, fill;

    if (!p || p->rect.width <= 0 || p->rect.height <= 0)
        return;
    radius = p->rect.height / 2;
    if (radius > 4)
        radius = 4;
    surface_set_color(&g_surface, TH.card2);
    surface_fill_round(&g_surface, p->rect, radius);
    fill = p->rect.width * p->value / 100;
    if (fill > 0) {
        surface_set_color(&g_surface, TH.accent);
        surface_fill_round(&g_surface,
                           (SurfaceRect){p->rect.x, p->rect.y, fill,
                                         p->rect.height}, radius);
    }
    if (p->text[0])
        text8_center(p->text, p->rect.x + p->rect.width / 2,
                     p->rect.y + (p->rect.height - 8) / 2, 1, TH.text);
}

/* --- scrollbar --------------------------------------------------------- */

void ui_scrollbar_init(UiScrollbar *s, SurfaceRect rect, int vertical)
{
    if (!s)
        return;
    s->rect = rect;
    s->total = 0;
    s->visible = 0;
    s->first = 0;
    s->vertical = vertical != 0;
}

void ui_scrollbar_set_range(UiScrollbar *s, int total, int visible, int first)
{
    if (!s)
        return;
    s->total = ui_max(0, total);
    s->visible = ui_max(0, visible);
    s->first = ui_max(0, first);
    if (s->visible >= s->total)
        s->first = 0;
    else if (s->first > s->total - s->visible)
        s->first = s->total - s->visible;
}

int ui_scrollbar_thumb(const UiScrollbar *s, SurfaceRect *thumb)
{
    int length, span, size, pos;

    if (!s || !thumb)
        return 0;
    if (s->total <= 0 || s->visible <= 0 || s->total <= s->visible) {
        *thumb = (SurfaceRect){0, 0, 0, 0};
        return 0;
    }
    length = s->vertical ? s->rect.height : s->rect.width;
    span = s->total - s->visible;
    size = length * s->visible / s->total;
    if (size < 12)
        size = 12;
    if (size > length)
        size = length;
    pos = span > 0 ? (length - size) * s->first / span : 0;
    if (s->vertical)
        *thumb = (SurfaceRect){s->rect.x, s->rect.y + pos, s->rect.width,
                               size};
    else
        *thumb = (SurfaceRect){s->rect.x + pos, s->rect.y, size,
                               s->rect.height};
    return 1;
}

int ui_scrollbar_hit(const UiScrollbar *s, int x, int y)
{
    if (!s)
        return 0;
    return x >= s->rect.x && x < s->rect.x + s->rect.width &&
           y >= s->rect.y && y < s->rect.y + s->rect.height;
}

void ui_scrollbar_draw(const UiScrollbar *s)
{
    SurfaceRect thumb;
    u32 light;

    if (!s || s->rect.width <= 0 || s->rect.height <= 0)
        return;

    surface_set_color(&g_surface, TH.card2);
    surface_fill(&g_surface, s->rect);
    surface_set_color(&g_surface, TH.border);
    if (s->vertical) {
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x, s->rect.y, 1, s->rect.height});
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x + s->rect.width - 1, s->rect.y,
                                   1, s->rect.height});
    } else {
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x, s->rect.y, s->rect.width, 1});
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x, s->rect.y + s->rect.height - 1,
                                   s->rect.width, 1});
    }

    if (!ui_scrollbar_thumb(s, &thumb))
        return;
    light = rgb_lerp(TH.accent2, 0xffffffu, 72);
    surface_set_color(&g_surface, TH.accent2);
    surface_fill(&g_surface, thumb);
    surface_set_color(&g_surface, light);
    if (s->vertical)
        surface_fill(&g_surface,
                     (SurfaceRect){thumb.x, thumb.y, thumb.width, 1});
    else
        surface_fill(&g_surface,
                     (SurfaceRect){thumb.x, thumb.y, 1, thumb.height});
}

/* --- separator --------------------------------------------------------- */

void ui_separator_init(UiSeparator *s, SurfaceRect rect, int vertical)
{
    if (!s)
        return;
    s->rect = rect;
    s->vertical = vertical != 0;
}

void ui_separator_draw(const UiSeparator *s)
{
    if (!s || s->rect.width <= 0 || s->rect.height <= 0)
        return;
    surface_set_color(&g_surface, TH.border);
    if (s->vertical) {
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x, s->rect.y, 1, s->rect.height});
        surface_set_color(&g_surface, TH.panel0);
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x + 1, s->rect.y, 1,
                                   s->rect.height});
    } else {
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x, s->rect.y, s->rect.width, 1});
        surface_set_color(&g_surface, TH.panel0);
        surface_fill(&g_surface,
                     (SurfaceRect){s->rect.x, s->rect.y + 1, s->rect.width,
                                   1});
    }
}
