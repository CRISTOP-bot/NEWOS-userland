/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_widgets.h
 * Description: Caller-owned desktop widgets (button, progress, scrollbar).
 * Added: 2026-10-27
 */
/* Caller-owned desktop widgets.
 *
 * Immediate-mode controls: geometry and state live in the caller's struct,
 * drawing goes straight to the desktop surface, nothing is allocated and no
 * event callbacks or widget tree exist. The desktop keeps its Win95
 * look by reading the active theme (TH) at draw time.
 */

#ifndef NEWOS_DESKTOP_WIDGETS_H
#define NEWOS_DESKTOP_WIDGETS_H

#include "desktop_surface.h"

#define UI_LABEL_MAX 32

typedef struct {
    SurfaceRect rect;
    char        label[UI_LABEL_MAX];
    int         hovered;
    int         active;
    int         enabled;
} UiButton;

void ui_button_init(UiButton *b, SurfaceRect rect, const char *label);
void ui_button_set_rect(UiButton *b, SurfaceRect rect);
void ui_button_set_state(UiButton *b, int hovered, int active);
int  ui_button_hit(const UiButton *b, int x, int y);
void ui_button_draw(const UiButton *b);

typedef struct {
    SurfaceRect rect;
    int         value;        /* 0..100 */
    char        text[32];
} UiProgress;

void ui_progress_init(UiProgress *p, SurfaceRect rect);
void ui_progress_set_value(UiProgress *p, int value);
void ui_progress_set_text(UiProgress *p, const char *text);
void ui_progress_draw(const UiProgress *p);

typedef struct {
    SurfaceRect rect;
    int         total;        /* total item count */
    int         visible;      /* visible item count */
    int         first;        /* first visible item */
    int         vertical;
} UiScrollbar;

void ui_scrollbar_init(UiScrollbar *s, SurfaceRect rect, int vertical);
void ui_scrollbar_set_range(UiScrollbar *s, int total, int visible, int first);
int  ui_scrollbar_thumb(const UiScrollbar *s, SurfaceRect *thumb);
int  ui_scrollbar_hit(const UiScrollbar *s, int x, int y);
void ui_scrollbar_draw(const UiScrollbar *s);

typedef struct {
    SurfaceRect rect;
    int         vertical;
} UiSeparator;

void ui_separator_init(UiSeparator *s, SurfaceRect rect, int vertical);
void ui_separator_draw(const UiSeparator *s);

#endif /* NEWOS_DESKTOP_WIDGETS_H */
