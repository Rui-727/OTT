/* ott_ui.c - LV2 UI for the OTT plugin, built on pugl + Cairo.
 *
 * URI: https://github.com/Rui-727/OTT#ui
 *
 * The UI is a 560 x 360 fixed-size X11 window. It draws 12 knobs and a
 * bypass button with Cairo, and talks to the plugin exclusively through
 * the LV2 UI callback interface (write_function / port_event). The UI
 * and plugin never share memory beyond the host-mediated control port
 * buffers.
 *
 * Mouse:
 *   - Left-drag a knob vertically to change its value.
 *   - Shift+drag for fine control (5x slower).
 *   - Double-click a knob to reset it to its default.
 *   - Click the bypass button to toggle it.
 *
 * Build: see Makefile. Requires pugl-0, pugl-cairo-0, and cairo via
 * pkg-config. If pugl is missing the UI .so is skipped and the plugin
 * still builds with the host's generic parameter sheet.
 */

#include "lv2/ui.h"

#include <pugl/pugl.h>
#include <pugl/cairo.h>

#include <cairo.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OTT_URI "https://github.com/Rui-727/OTT"
#define OTT_UI_URI OTT_URI "#ui"

/* Port indices. Must match ott_lv2.c and ott.ttl. */
enum {
    OTT_PORT_DEPTH = 0,
    OTT_PORT_TIME,
    OTT_PORT_UPWARD,
    OTT_PORT_DOWNWARD,
    OTT_PORT_INPUT_GAIN,
    OTT_PORT_OUTPUT_GAIN,
    OTT_PORT_BAND1_THRESH,
    OTT_PORT_BAND2_THRESH,
    OTT_PORT_BAND3_THRESH,
    OTT_PORT_BAND1_GAIN,
    OTT_PORT_BAND2_GAIN,
    OTT_PORT_BAND3_GAIN,
    OTT_PORT_BYPASS,
    OTT_PORT_CONTROL_COUNT
};

/* ------------------------------------------------------------------ */
/* Widget model                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    W_KNOB,
    W_TOGGLE
} widget_type_t;

/* Range descriptor for a parameter. min/max are in raw port units; for
 * W_TOGGLE both are 0/1 and the value is treated as a boolean. */
typedef struct {
    float min;
    float max;
    float def;      /* default raw value (for double-click reset)        */
} param_range_t;

typedef struct {
    widget_type_t  type;
    uint32_t       port;       /* LV2 control port index                 */
    const char    *label;      /* short label drawn above the widget     */
    /* Geometry: knob uses (cx, cy, r); toggle uses (x, y, w, h).        */
    double         cx, cy;
    double         r;
    double         x, y, w, h;
    param_range_t  range;
    float          value;      /* cached raw port value (last port_event)*/
} ott_widget_t;

#define OTT_WIDGET_COUNT 13

/* ------------------------------------------------------------------ */
/* UI state                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    LV2UI_Write_Function write;
    LV2UI_Controller     controller;
    const LV2UI_Touch   *touch;

    PuglWorld  *world;
    PuglView   *view;

    ott_widget_t widgets[OTT_WIDGET_COUNT];

    /* Drag state. drag_port == -1 means no drag in progress. */
    int    drag_port;
    double drag_start_y;
    double drag_start_v;     /* normalized 0..1 at drag start            */
    bool   drag_fine;

    /* Double-click tracking. last_click_time is in seconds from pugl's
     * clock; last_click_port records which widget was last clicked so we
     * only treat two clicks on the same widget as a double-click. */
    double   last_click_time;
    uint32_t last_click_port;
} ott_ui_t;

/* Maximum gap (in seconds) between two clicks to count as a double-click. */
#define OTT_DOUBLE_CLICK_S 0.40

/* ------------------------------------------------------------------ */
/* Parameter helpers                                                   */
/* ------------------------------------------------------------------ */

/* Map raw port value to normalized 0..1 for drawing and dragging. */
static double
param_normalize(const ott_widget_t *w, float raw)
{
    if (w->type == W_TOGGLE) {
        return raw > 0.5f ? 1.0 : 0.0;
    }
    double span = (double)w->range.max - (double)w->range.min;
    if (span <= 0.0) return 0.0;
    double v = ((double)raw - (double)w->range.min) / span;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return v;
}

/* Map normalized 0..1 back to raw port value. */
static float
param_unnormalize(const ott_widget_t *w, double norm)
{
    if (w->type == W_TOGGLE) {
        return norm > 0.5 ? 1.0f : 0.0f;
    }
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    double span = (double)w->range.max - (double)w->range.min;
    return (float)((double)w->range.min + norm * span);
}

/* Format a raw port value for display below the widget. */
static void
format_value(char *buf, size_t n, const ott_widget_t *w, float raw)
{
    switch (w->port) {
        case OTT_PORT_DEPTH:
        case OTT_PORT_TIME:
        case OTT_PORT_UPWARD:
        case OTT_PORT_DOWNWARD:
            snprintf(buf, n, "%d%%", (int)((double)raw * 100.0 + 0.5));
            break;
        case OTT_PORT_INPUT_GAIN:
        case OTT_PORT_OUTPUT_GAIN:
        case OTT_PORT_BAND1_THRESH:
        case OTT_PORT_BAND2_THRESH:
        case OTT_PORT_BAND3_THRESH:
        case OTT_PORT_BAND1_GAIN:
        case OTT_PORT_BAND2_GAIN:
        case OTT_PORT_BAND3_GAIN:
            snprintf(buf, n, "%+.1f dB", (double)raw);
            break;
        case OTT_PORT_BYPASS:
            snprintf(buf, n, raw > 0.5f ? "BYPASS" : "ON");
            break;
        default:
            snprintf(buf, n, "%.3f", (double)raw);
            break;
    }
}

/* Push a raw value to the host's control port via write_function. */
static inline void
ui_write(ott_ui_t *ui, uint32_t port, float value)
{
    if (ui->write) {
        ui->write(ui->controller, port, sizeof(float), 0, &value);
    }
}

/* Notify the host that the user grabbed/released a control. */
static inline void
ui_touch(ott_ui_t *ui, uint32_t port, bool grabbed)
{
    if (ui->touch && ui->touch->touch) {
        ui->touch->touch(ui->touch->handle, port, grabbed);
    }
}

/* ------------------------------------------------------------------ */
/* Widget table and hit testing                                        */
/* ------------------------------------------------------------------ */

static void
init_widgets(ott_ui_t *ui)
{
    /* Helper macros for readable table. */
    #define KNOB(p, lbl, X, Y, R, MIN, MAX, DEF) \
        ui->widgets[ui_count++] = (ott_widget_t){ \
            W_KNOB, (p), (lbl), (X), (Y), (R), 0,0,0,0, \
            { (MIN), (MAX), (DEF) }, (DEF) }
    #define TOGGLE(p, lbl, X, Y, W, H, DEF) \
        ui->widgets[ui_count++] = (ott_widget_t){ \
            W_TOGGLE, (p), (lbl), 0,0,0, (X), (Y), (W), (H), \
            { 0.0f, 1.0f, (DEF) }, (DEF) }

    int ui_count = 0;

    /* Top row: Depth, Time, Input Gain, Output Gain. */
    KNOB(OTT_PORT_DEPTH,       "DEPTH",     80.0,  60.0, 26.0, 0.0f, 1.0f, 1.0f);
    KNOB(OTT_PORT_TIME,        "TIME",     200.0,  60.0, 26.0, 0.0f, 1.0f, 0.5f);
    KNOB(OTT_PORT_INPUT_GAIN,  "IN GAIN",  320.0,  60.0, 26.0, -24.0f, 24.0f, 5.2f);
    KNOB(OTT_PORT_OUTPUT_GAIN, "OUT GAIN", 440.0,  60.0, 26.0, -24.0f, 24.0f, 0.0f);

    /* Middle: 3 columns. Visual order is HIGH (left), MID (middle),
     * LOW (right) to match the Xfer OTT layout. Band indexing in the
     * DSP is band1=low, band2=mid, band3=high, so the port ordering
     * looks reversed compared to the visual order. */
    KNOB(OTT_PORT_BAND3_THRESH, "HIGH",    90.0, 140.0, 24.0, -60.0f, 0.0f, -30.0f);
    KNOB(OTT_PORT_BAND3_GAIN,   "GAIN",    90.0, 220.0, 24.0, -24.0f, 24.0f, 10.3f);
    KNOB(OTT_PORT_BAND2_THRESH, "MID",    280.0, 140.0, 24.0, -60.0f, 0.0f, -30.0f);
    KNOB(OTT_PORT_BAND2_GAIN,   "GAIN",   280.0, 220.0, 24.0, -24.0f, 24.0f,  5.7f);
    KNOB(OTT_PORT_BAND1_THRESH, "LOW",    470.0, 140.0, 24.0, -60.0f, 0.0f, -30.0f);
    KNOB(OTT_PORT_BAND1_GAIN,   "GAIN",   470.0, 220.0, 24.0, -24.0f, 24.0f, 10.3f);

    /* Bottom row: Upward, Downward, Bypass. */
    KNOB (OTT_PORT_UPWARD,   "UPWARD",   120.0, 320.0, 24.0, 0.0f, 1.0f, 1.0f);
    KNOB (OTT_PORT_DOWNWARD, "DOWNWARD", 280.0, 320.0, 24.0, 0.0f, 1.0f, 1.0f);
    TOGGLE(OTT_PORT_BYPASS,  "BYPASS",   420.0, 305.0, 110.0, 30.0, 0.0f);

    (void)ui_count;
    #undef KNOB
    #undef TOGGLE
}

/* Find the widget under (x, y). Returns NULL if none. */
static ott_widget_t *
widget_at(ott_ui_t *ui, double x, double y)
{
    for (int i = 0; i < OTT_WIDGET_COUNT; ++i) {
        ott_widget_t *w = &ui->widgets[i];
        if (w->type == W_KNOB) {
            double dx = x - w->cx;
            double dy = y - w->cy;
            if (dx * dx + dy * dy <= w->r * w->r) return w;
        } else { /* W_TOGGLE */
            if (x >= w->x && x <= w->x + w->w &&
                y >= w->y && y <= w->y + w->h) return w;
        }
    }
    return NULL;
}

static ott_widget_t *
widget_by_port(ott_ui_t *ui, uint32_t port)
{
    for (int i = 0; i < OTT_WIDGET_COUNT; ++i) {
        if (ui->widgets[i].port == port) return &ui->widgets[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Cairo drawing                                                       */
/* ------------------------------------------------------------------ */

/* OTT dark theme. Background #1a1a1a, text #cccccc, accent arc #FF6B6B. */
#define COLOR_BG_R   0.102
#define COLOR_BG_G   0.102
#define COLOR_BG_B   0.102
#define COLOR_FG_R   0.800
#define COLOR_FG_G   0.800
#define COLOR_FG_B   0.800
#define COLOR_DIM_R  0.500
#define COLOR_DIM_G  0.500
#define COLOR_DIM_B  0.500
#define COLOR_ACCENT_R 1.000
#define COLOR_ACCENT_G 0.420
#define COLOR_ACCENT_B 0.420
#define COLOR_TRACK_R  0.333
#define COLOR_TRACK_G  0.333
#define COLOR_TRACK_B  0.333
#define COLOR_POINTER_R 1.000
#define COLOR_POINTER_G 1.000
#define COLOR_POINTER_B 1.000
#define COLOR_BYPASS_ON_R  0.900
#define COLOR_BYPASS_ON_G  0.300
#define COLOR_BYPASS_ON_B  0.200
#define COLOR_BYPASS_OFF_R 0.200
#define COLOR_BYPASS_OFF_G 0.450
#define COLOR_BYPASS_OFF_B 0.200

static void
set_rgb(cairo_t *cr, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);
}

/* Centered text helper. Anchor y is the baseline. */
static void
show_text_centered(cairo_t *cr, double cx, double baseline, const char *s)
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, s, &ext);
    cairo_move_to(cr, cx - ext.width / 2.0, baseline);
    cairo_show_text(cr, s);
}

static void
draw_knob(cairo_t *cr, const ott_widget_t *w)
{
    const double cx = w->cx;
    const double cy = w->cy;
    const double r  = w->r;
    const double v  = param_normalize(w, w->value);

    /* Sweep 270 degrees, from 135 deg to 405 deg in standard math angles.
     * Cairo angles are radians, 0 = east, positive = clockwise (Y down).
     * Start = 135 deg = 3pi/4. End = 405 deg = 9pi/4. We express them
     * as -3pi/4 .. 3pi/4 by drawing clockwise from bottom-left around
     * the top to bottom-right. */
    const double start = 0.75 * M_PI;     /* 135 deg, bottom-left        */
    const double end   = 2.25 * M_PI;     /* 405 deg, bottom-right       */
    const double sweep = end - start;     /* 1.5pi = 270 deg             */
    const double angle = start + v * sweep;

    /* Background filled circle (knob body). */
    set_rgb(cr, 0.11, 0.11, 0.13);
    cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
    cairo_fill(cr);

    /* Track ring (the full arc, dim). */
    set_rgb(cr, COLOR_TRACK_R, COLOR_TRACK_G, COLOR_TRACK_B);
    cairo_set_line_width(cr, 2.5);
    cairo_arc(cr, cx, cy, r + 4.0, start, end);
    cairo_stroke(cr);

    /* Active arc (from start to current value). */
    set_rgb(cr, COLOR_ACCENT_R, COLOR_ACCENT_G, COLOR_ACCENT_B);
    cairo_set_line_width(cr, 3.0);
    if (v > 0.001) {
        cairo_arc(cr, cx, cy, r + 4.0, start, angle);
        cairo_stroke(cr);
    }

    /* Pointer line from center to the value position. */
    set_rgb(cr, COLOR_POINTER_R, COLOR_POINTER_G, COLOR_POINTER_B);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + (r - 2.0) * cos(angle),
                      cy + (r - 2.0) * sin(angle));
    cairo_stroke(cr);

    /* Label above the knob. */
    set_rgb(cr, COLOR_FG_R, COLOR_FG_G, COLOR_FG_B);
    cairo_set_font_size(cr, 10.0);
    show_text_centered(cr, cx, cy - r - 12.0, w->label);

    /* Value text below the knob, smaller and dimmer. */
    char buf[32];
    format_value(buf, sizeof(buf), w, w->value);
    set_rgb(cr, COLOR_DIM_R, COLOR_DIM_G, COLOR_DIM_B);
    cairo_set_font_size(cr, 9.0);
    show_text_centered(cr, cx, cy + r + 16.0, buf);
}

static void
draw_toggle(cairo_t *cr, const ott_widget_t *w)
{
    const bool on = w->value > 0.5f;

    /* Body. Red when bypassed (on), dim green when active (off). */
    if (on) {
        set_rgb(cr, COLOR_BYPASS_ON_R, COLOR_BYPASS_ON_G, COLOR_BYPASS_ON_B);
    } else {
        set_rgb(cr, COLOR_BYPASS_OFF_R, COLOR_BYPASS_OFF_G, COLOR_BYPASS_OFF_B);
    }
    cairo_rectangle(cr, w->x, w->y, w->w, w->h);
    cairo_fill(cr);

    /* Thin border. */
    set_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, w->x + 0.5, w->y + 0.5, w->w - 1.0, w->h - 1.0);
    cairo_stroke(cr);

    /* Label inside. */
    set_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_font_size(cr, 12.0);
    show_text_centered(cr, w->x + w->w / 2.0,
                       w->y + w->h / 2.0 + 4.0,
                       on ? "BYPASS" : "ACTIVE");

    /* Caption above. */
    set_rgb(cr, COLOR_FG_R, COLOR_FG_G, COLOR_FG_B);
    cairo_set_font_size(cr, 10.0);
    show_text_centered(cr, w->x + w->w / 2.0, w->y - 6.0, w->label);
}

static void
draw_column_label(cairo_t *cr, double cx, double y, const char *s)
{
    set_rgb(cr, COLOR_FG_R, COLOR_FG_G, COLOR_FG_B);
    cairo_set_font_size(cr, 11.0);
    show_text_centered(cr, cx, y, s);
}

static void
on_expose(ott_ui_t *ui, const PuglExposeEvent *event)
{
    (void)event;
    cairo_t *cr = (cairo_t *)puglGetContext(ui->view);
    if (!cr) return;

    /* Background. */
    set_rgb(cr, COLOR_BG_R, COLOR_BG_G, COLOR_BG_B);
    cairo_paint(cr);

    cairo_select_font_face(cr, "sans-serif",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);

    /* Band column captions: HIGH / MID / LOW above the threshold knobs. */
    draw_column_label(cr,  90.0, 100.0, "HIGH");
    draw_column_label(cr, 280.0, 100.0, "MID");
    draw_column_label(cr, 470.0, 100.0, "LOW");

    /* Widgets. */
    for (int i = 0; i < OTT_WIDGET_COUNT; ++i) {
        const ott_widget_t *w = &ui->widgets[i];
        if (w->type == W_KNOB) {
            draw_knob(cr, w);
        } else {
            draw_toggle(cr, w);
        }
    }

    /* Subtle separator lines between the rows. */
    set_rgb(cr, 0.18, 0.18, 0.18);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 16.0, 90.5);
    cairo_line_to(cr, 544.0, 90.5);
    cairo_stroke(cr);
    cairo_move_to(cr, 16.0, 270.5);
    cairo_line_to(cr, 544.0, 270.5);
    cairo_stroke(cr);

    /* Do NOT call cairo_destroy(cr) here. puglGetContext() returns a
     * context owned by pugl. Destroying it causes a double-free
     * assertion in cairo: "CAIRO_REFERENCE_COUNT_HAS_REFERENCE". */
}

/* ------------------------------------------------------------------ */
/* Event handling                                                      */
/* ------------------------------------------------------------------ */

static PuglStatus
on_event(PuglView *view, const PuglEvent *event)
{
    ott_ui_t *ui = (ott_ui_t *)puglGetHandle(view);
    if (!ui) return PUGL_SUCCESS;

    switch (event->type) {
        case PUGL_EXPOSE:
            on_expose(ui, &event->expose);
            break;

        case PUGL_BUTTON_PRESS: {
            const PuglButtonEvent *b = &event->button;
            if (b->button != 0) break;  /* left button only (pugl numbers from 0) */

            ott_widget_t *w = widget_at(ui, b->x, b->y);
            if (!w) break;

            /* Detect double-click: same widget, within OTT_DOUBLE_CLICK_S. */
            bool is_double = (w->port == ui->last_click_port &&
                              (b->time - ui->last_click_time) <= OTT_DOUBLE_CLICK_S);
            ui->last_click_time = b->time;
            ui->last_click_port = w->port;

            if (w->type == W_TOGGLE) {
                float newv = w->value > 0.5f ? 0.0f : 1.0f;
                w->value = newv;
                ui_write(ui, w->port, newv);
                puglObscureView(ui->view);
                break;
            }

            /* Knob press: begin drag. */
            ui->drag_port    = (int)w->port;
            ui->drag_start_y = b->y;
            ui->drag_start_v = param_normalize(w, w->value);
            ui->drag_fine    = (b->state & PUGL_MOD_SHIFT) != 0;
            ui_touch(ui, w->port, true);

            /* Double-click resets the knob to its default. */
            if (is_double) {
                w->value = w->range.def;
                ui_write(ui, w->port, w->range.def);
                puglObscureView(ui->view);
            }
            break;
        }

        case PUGL_MOTION: {
            if (ui->drag_port < 0) break;
            ott_widget_t *w = widget_by_port(ui, (uint32_t)ui->drag_port);
            if (!w) break;
            const PuglMotionEvent *m = &event->motion;

            /* Allow shift to switch fine mode mid-drag. */
            bool fine = (m->state & PUGL_MOD_SHIFT) != 0;
            double dy = ui->drag_start_y - m->y;
            double sens = (fine || ui->drag_fine) ? 0.001 : 0.005;
            double new_v = ui->drag_start_v + dy * sens;
            if (new_v < 0.0) new_v = 0.0;
            if (new_v > 1.0) new_v = 1.0;

            float raw = param_unnormalize(w, new_v);
            if (raw != w->value) {
                w->value = raw;
                ui_write(ui, w->port, raw);
                puglObscureView(ui->view);
            }
            break;
        }

        case PUGL_BUTTON_RELEASE: {
            if (ui->drag_port < 0) break;
            ui_touch(ui, (uint32_t)ui->drag_port, false);
            ui->drag_port = -1;
            break;
        }

        case PUGL_CLOSE:
            /* The host owns the lifecycle. We just stop accepting drags. */
            ui->drag_port = -1;
            break;

        default:
            break;
    }
    return PUGL_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* LV2 UI descriptor                                                   */
/* ------------------------------------------------------------------ */

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor,
            const char              *plugin_uri,
            const char              *bundle_path,
            LV2UI_Write_Function    write_function,
            LV2UI_Controller        controller,
            LV2UI_Widget           *widget,
            const LV2_Feature *const *features)
{
    (void)descriptor; (void)bundle_path;

    if (plugin_uri && strcmp(plugin_uri, OTT_URI) != 0) {
        return NULL;
    }

    ott_ui_t *ui = (ott_ui_t *)calloc(1, sizeof(*ui));
    if (!ui) return NULL;

    ui->write      = write_function;
    ui->controller = controller;
    ui->drag_port  = -1;

    init_widgets(ui);

    /* Walk the features array for the ones we care about. */
    void *parent = NULL;
    for (const LV2_Feature *const *f = features; f && *f; ++f) {
        const LV2_Feature *feat = *f;
        if (!feat) continue;
        if (strcmp(feat->URI, LV2_UI__parent) == 0) {
            parent = feat->data;
        } else if (strcmp(feat->URI, LV2_UI__touch) == 0) {
            ui->touch = (const LV2UI_Touch *)feat->data;
        }
    }

    /* pugl setup. */
    ui->world = puglNewWorld(PUGL_MODULE, 0U);
    if (!ui->world) {
        free(ui);
        return NULL;
    }
    puglSetWorldString(ui->world, PUGL_CLASS_NAME, "OTT");

    ui->view = puglNewView(ui->world);
    if (!ui->view) {
        puglFreeWorld(ui->world);
        free(ui);
        return NULL;
    }

    puglSetViewString(ui->view, PUGL_WINDOW_TITLE, "OTT");
    puglSetViewHint(ui->view, PUGL_RESIZABLE, PUGL_FALSE);
    puglSetSizeHint(ui->view, PUGL_DEFAULT_SIZE, 560, 360);
    puglSetSizeHint(ui->view, PUGL_MIN_SIZE, 560, 360);
    puglSetSizeHint(ui->view, PUGL_MAX_SIZE, 560, 360);
    puglSetHandle(ui->view, ui);
    puglSetEventFunc(ui->view, on_event);
    puglSetBackend(ui->view, puglCairoBackend());

    if (parent) {
        puglSetParent(ui->view, (PuglNativeView)(intptr_t)parent);
    }

    if (puglRealize(ui->view) != PUGL_SUCCESS) {
        puglFreeView(ui->view);
        puglFreeWorld(ui->world);
        free(ui);
        return NULL;
    }

    /* Only show the window ourselves when there's no parent (the host
     * embeds the widget itself when one was provided). */
    if (!parent) {
        puglShow(ui->view, PUGL_SHOW_RAISE);
    }

    if (widget) {
        *widget = (LV2UI_Widget)(intptr_t)puglGetNativeView(ui->view);
    }

    return (LV2UI_Handle)ui;
}

static void
cleanup(LV2UI_Handle handle)
{
    ott_ui_t *ui = (ott_ui_t *)handle;
    if (!ui) return;
    if (ui->view) {
        puglFreeView(ui->view);
    }
    if (ui->world) {
        puglFreeWorld(ui->world);
    }
    free(ui);
}

static void
port_event(LV2UI_Handle handle,
           uint32_t     port_index,
           uint32_t     buffer_size,
           uint32_t     format,
           const void  *buffer)
{
    ott_ui_t *ui = (ott_ui_t *)handle;
    if (!ui) return;
    if (format != 0 || buffer_size != sizeof(float)) return;
    if (port_index >= OTT_PORT_CONTROL_COUNT) return;

    ott_widget_t *w = widget_by_port(ui, port_index);
    if (!w) return;

    float v = *(const float *)buffer;
    if (v == w->value) return;  /* short-circuit redundant updates */
    w->value = v;
    if (ui->view) {
        puglObscureView(ui->view);
    }
}

static int
ui_idle(LV2UI_Handle handle)
{
    ott_ui_t *ui = (ott_ui_t *)handle;
    if (!ui || !ui->world) return 0;
    puglUpdate(ui->world, 0.0);
    return 0;
}

static int
ui_show(LV2UI_Handle handle)
{
    ott_ui_t *ui = (ott_ui_t *)handle;
    if (ui && ui->view) {
        puglShow(ui->view, PUGL_SHOW_RAISE);
    }
    return 0;
}

static int
ui_hide(LV2UI_Handle handle)
{
    ott_ui_t *ui = (ott_ui_t *)handle;
    if (ui && ui->view) {
        puglHide(ui->view);
    }
    return 0;
}

static const void *
extension_data(const char *uri)
{
    static const LV2UI_Idle_Interface idle = { ui_idle };
    static const LV2UI_Show_Interface show = { ui_show, ui_hide };
    if (!strcmp(uri, LV2_UI__idleInterface)) return &idle;
    if (!strcmp(uri, LV2_UI__showInterface)) return &show;
    return NULL;
}

static const LV2UI_Descriptor descriptor = {
    OTT_UI_URI,
    instantiate,
    cleanup,
    port_event,
    extension_data
};

LV2UI_SYMBOL_EXPORT const LV2UI_Descriptor *
lv2ui_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
