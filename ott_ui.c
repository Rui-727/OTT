/* ott_ui.c - LV2 UI for the OTT plugin, built on pugl + Cairo.
 *
 * URI: https://github.com/Rui-727/OTT#ui
 *
 * The UI is a 600 x 400 fixed-size X11 window drawn with Cairo. The visual
 * design echoes the Xfer Records OTT plugin: a dark gray panel with three
 * rows of knobs (top: global controls; middle: per-band THRESHOLD/GAIN for
 * HIGH/MID/LOW; bottom: UPWARD/DOWNWARD plus a bypass button). Each knob is
 * drawn as a modern rotary: outer ring with a colored value arc sweeping
 * 270 degrees, dark inner circle, and a white pointer. The window is
 * branded "OTT" / "by Zero:Archive" with credits to the original.
 *
 * The UI talks to the plugin exclusively through the LV2 UI callback
 * interface (write_function / port_event). It never shares memory with
 * the plugin beyond the host-mediated control port buffers.
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

/* Window geometry. Fixed. */
#define OTT_UI_WIDTH  600
#define OTT_UI_HEIGHT 400

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

/* Visual size class. Drives the knob radius and the label/value spacing. */
typedef enum {
    SIZE_LARGE,   /* top-row global knobs: 70px diameter (r=35)            */
    SIZE_SMALL    /* per-band and bottom-row knobs: 50px diameter (r=25)   */
} knob_size_t;

/* Range descriptor for a parameter. min/max are in raw port units; for
 * W_TOGGLE both are 0/1 and the value is treated as a boolean. */
typedef struct {
    float min;
    float max;
    float def;      /* default raw value (for double-click reset)        */
} param_range_t;

typedef struct {
    widget_type_t  type;
    knob_size_t    size;       /* W_KNOB only                                    */
    uint32_t       port;       /* LV2 control port index                 */
    const char    *label;      /* short label drawn below the widget     */
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
/* Theme                                                               */
/* ------------------------------------------------------------------ */

/* OTT "Xfer-style" dark theme, all colors as 0..1 doubles for Cairo. */
#define C_BG          0.165, 0.165, 0.165   /* #2A2A2A panel background   */
#define C_KNOB_OUTER  0.227, 0.227, 0.227   /* #3A3A3A knob body          */
#define C_KNOB_INNER  0.133, 0.133, 0.133   /* #222222 knob center        */
#define C_ARC_ACTIVE  1.000, 0.420, 0.208   /* #FF6B35 active arc         */
#define C_ARC_TRACK   0.267, 0.267, 0.267   /* #444444 inactive arc       */
#define C_POINTER     1.000, 1.000, 1.000   /* #FFFFFF value pointer      */
#define C_LABEL       0.867, 0.867, 0.867   /* #DDDDDD knob labels        */
#define C_VALUE       0.533, 0.533, 0.533   /* #888888 value text         */
#define C_BAND        0.667, 0.667, 0.667   /* #AAAAAA band labels        */
#define C_SEP         0.200, 0.200, 0.200   /* #333333 separator lines    */
#define C_TITLE       1.000, 1.000, 1.000   /* white title                */
#define C_SUBTITLE    0.533, 0.533, 0.533   /* dim subtitle               */
#define C_CREDITS     0.333, 0.333, 0.333   /* #555555 credits            */
#define C_BYPASS_ON   0.000, 0.667, 0.000   /* #00AA00 green (ACTIVE)     */
#define C_BYPASS_OFF  0.267, 0.000, 0.000   /* #440000 dark red (BYPASS)  */
#define C_BUTTON_TEXT 1.000, 1.000, 1.000   /* white button text          */

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
            snprintf(buf, n, raw > 0.5f ? "BYPASS" : "ACTIVE");
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

/* Knob radius by size class. SIZE_LARGE = 70px diameter (r=35),
 * SIZE_SMALL = 50px diameter (r=25). */
static double
knob_radius(knob_size_t s)
{
    return s == SIZE_LARGE ? 35.0 : 25.0;
}

static void
init_widgets(ott_ui_t *ui)
{
    /* Helper macros for readable table. Knob center is (X, Y); radius is
     * derived from the size class so all large knobs share one radius. */
    #define KNOB(SZ, p, lbl, X, Y, MIN, MAX, DEF) \
        ui->widgets[ui_count++] = (ott_widget_t){ \
            W_KNOB, (SZ), (p), (lbl), (X), (Y), knob_radius(SZ), 0,0,0,0, \
            { (MIN), (MAX), (DEF) }, (DEF) }
    #define TOGGLE(p, lbl, X, Y, W, H, DEF) \
        ui->widgets[ui_count++] = (ott_widget_t){ \
            W_TOGGLE, SIZE_SMALL, (p), (lbl), 0,0,0, (X), (Y), (W), (H), \
            { 0.0f, 1.0f, (DEF) }, (DEF) }

    int ui_count = 0;

    /* --- Top row: Depth, Time, Input Gain, Output Gain (4 large knobs) - */
    /* 600px wide / 4 knobs = 150px per slot, centers at x = 75,225,375,525.
     * cy = 70 so the 70px knob sits at y=35..105 with label/value below.   */
    KNOB(SIZE_LARGE, OTT_PORT_DEPTH,       "DEPTH",     75.0,  70.0, 0.0f, 1.0f, 1.0f);
    KNOB(SIZE_LARGE, OTT_PORT_TIME,        "TIME",     225.0,  70.0, 0.0f, 1.0f, 0.5f);
    KNOB(SIZE_LARGE, OTT_PORT_INPUT_GAIN,  "IN GAIN",  375.0,  70.0, -24.0f, 24.0f, 5.2f);
    KNOB(SIZE_LARGE, OTT_PORT_OUTPUT_GAIN, "OUT GAIN", 525.0,  70.0, -24.0f, 24.0f, 0.0f);

    /* --- Middle: 3 columns. Visual order HIGH (left), MID (middle),
     * LOW (right) to match the Xfer OTT layout. Band indexing in the DSP
     * is band1=low, band2=mid, band3=high, so the port ordering looks
     * reversed compared to the visual order. Each column has THRESHOLD
     * and GAIN side by side. */
    /* Column centers: 150, 300, 450. Knob offset +/- 35 from center.    */
    /* cy = 210, r = 25 (SIZE_SMALL).                                    */
    /* HIGH column (left): band3 ports. */
    KNOB(SIZE_SMALL, OTT_PORT_BAND3_THRESH, "THRESHOLD", 115.0, 210.0, -60.0f, 0.0f, -30.0f);
    KNOB(SIZE_SMALL, OTT_PORT_BAND3_GAIN,   "GAIN",      185.0, 210.0, -24.0f, 24.0f, 10.3f);
    /* MID column (center): band2 ports. */
    KNOB(SIZE_SMALL, OTT_PORT_BAND2_THRESH, "THRESHOLD", 265.0, 210.0, -60.0f, 0.0f, -30.0f);
    KNOB(SIZE_SMALL, OTT_PORT_BAND2_GAIN,   "GAIN",      335.0, 210.0, -24.0f, 24.0f,  5.7f);
    /* LOW column (right): band1 ports. */
    KNOB(SIZE_SMALL, OTT_PORT_BAND1_THRESH, "THRESHOLD", 415.0, 210.0, -60.0f, 0.0f, -30.0f);
    KNOB(SIZE_SMALL, OTT_PORT_BAND1_GAIN,   "GAIN",      485.0, 210.0, -24.0f, 24.0f, 10.3f);

    /* --- Bottom row: Upward, Downward, Bypass. -------------------------- */
    /* Knobs at x=150 and x=300, bypass button centered at x=470. cy=325.  */
    KNOB (SIZE_SMALL, OTT_PORT_UPWARD,   "UPWARD",   150.0, 325.0, 0.0f, 1.0f, 1.0f);
    KNOB (SIZE_SMALL, OTT_PORT_DOWNWARD, "DOWNWARD", 300.0, 325.0, 0.0f, 1.0f, 1.0f);
    TOGGLE(OTT_PORT_BYPASS,  "BYPASS",   430.0, 310.0, 100.0, 30.0, 0.0f);

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
/* Cairo drawing helpers                                               */
/* ------------------------------------------------------------------ */

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

/* Rounded rectangle path. Rounded corners with the given radius. */
static void
rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r > w / 2.0) r = w / 2.0;
    if (r > h / 2.0) r = h / 2.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0,         M_PI / 2.0);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI / 2.0,  M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI,        3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

/* Draw a modern rotary knob. The arc sweeps 270 degrees from 7 o'clock
 * (minimum) clockwise to 5 o'clock (maximum). The active portion is
 * warm orange, the inactive portion is dark gray. A dark inner circle
 * creates the ring look, and a white pointer indicates the value. */
static void
draw_knob(cairo_t *cr, const ott_widget_t *w)
{
    const double cx = w->cx;
    const double cy = w->cy;
    const double r  = w->r;
    const double v  = param_normalize(w, w->value);

    /* Arc sweep: 270 degrees, from 135 deg (bottom-left, "7 o'clock") to
     * 405 deg (bottom-right, "5 o'clock"), going clockwise over the top.
     * Cairo angles are radians with Y growing down, so positive angles
     * go clockwise on screen; start=3pi/4 places us at lower-left.    */
    const double start = 0.75 * M_PI;     /* 135 deg, bottom-left        */
    const double end   = 2.25 * M_PI;     /* 405 deg, bottom-right       */
    const double sweep = end - start;     /* 1.5pi = 270 deg             */
    const double angle = start + v * sweep;

    /* 1. Outer filled circle (knob body, #3A3A3A). */
    set_rgb(cr, C_KNOB_OUTER);
    cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
    cairo_fill(cr);

    /* 2. Value arc ring around the outer edge. We draw at radius r - 3
     * with a 4px line so the ring sits just inside the knob outline.
     * Inactive (full sweep) first, then active portion on top. */
    const double arc_r = r - 3.0;
    if (arc_r > 2.0) {
        /* Track (inactive portion: from current angle to end). */
        set_rgb(cr, C_ARC_TRACK);
        cairo_set_line_width(cr, 4.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_arc(cr, cx, cy, arc_r, start, end);
        cairo_stroke(cr);

        /* Active portion: from start to current angle. */
        if (v > 0.001) {
            set_rgb(cr, C_ARC_ACTIVE);
            cairo_set_line_width(cr, 4.0);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_arc(cr, cx, cy, arc_r, start, angle);
            cairo_stroke(cr);
        }
    }

    /* 3. Inner filled circle (60% of outer diameter -> radius 0.6 * r),
     * darker gray (#222) to create the "ring" look against the knob body. */
    const double inner_r = 0.6 * r;
    set_rgb(cr, C_KNOB_INNER);
    cairo_arc(cr, cx, cy, inner_r, 0.0, 2.0 * M_PI);
    cairo_fill(cr);

    /* 4. Pointer / indicator: white line from the inner circle's edge to
     * the inner edge of the arc ring, pointing at the current value. 2px. */
    set_rgb(cr, C_POINTER);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double p_inner = inner_r * 0.45;        /* start near center    */
    const double p_outer = arc_r - 2.0;            /* stop just inside arc */
    cairo_move_to(cr,
                  cx + p_inner * cos(angle),
                  cy + p_inner * sin(angle));
    cairo_line_to(cr,
                  cx + p_outer * cos(angle),
                  cy + p_outer * sin(angle));
    cairo_stroke(cr);

    /* Reset line cap for any subsequent draws. */
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

    /* 5. Label below the knob (#DDDDDD, 10pt). */
    set_rgb(cr, C_LABEL);
    cairo_set_font_size(cr, 10.0);
    show_text_centered(cr, cx, cy + r + 15.0, w->label);

    /* 6. Value text below the label (#888888, 9pt). */
    char buf[32];
    format_value(buf, sizeof(buf), w, w->value);
    set_rgb(cr, C_VALUE);
    cairo_set_font_size(cr, 9.0);
    show_text_centered(cr, cx, cy + r + 28.0, buf);
}

/* Draw the bypass button as a rounded rectangle. Green ("ACTIVE") when
 * the plugin is processing audio (bypass port == 0); dark red ("BYPASS")
 * when the signal is bypassed (bypass port == 1). */
static void
draw_toggle(cairo_t *cr, const ott_widget_t *w)
{
    /* In OTT's convention: bypass value 0 means the plugin is ACTIVE
     * (audio is processed), value 1 means BYPASS (audio is passed through).
     * The button text reflects the *current state*, not the action. */
    const bool bypassed = w->value > 0.5f;

    if (bypassed) {
        set_rgb(cr, C_BYPASS_OFF);
    } else {
        set_rgb(cr, C_BYPASS_ON);
    }
    rounded_rect(cr, w->x, w->y, w->w, w->h, 5.0);
    cairo_fill(cr);

    /* Thin darker border to lift the button off the panel. */
    set_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);
    rounded_rect(cr, w->x + 0.5, w->y + 0.5, w->w - 1.0, w->h - 1.0, 4.5);
    cairo_stroke(cr);

    /* Button label inside. */
    set_rgb(cr, C_BUTTON_TEXT);
    cairo_set_font_size(cr, 11.0);
    show_text_centered(cr, w->x + w->w / 2.0,
                       w->y + w->h / 2.0 + 4.0,
                       bypassed ? "BYPASS" : "ACTIVE");

    /* Caption above the button. */
    set_rgb(cr, C_LABEL);
    cairo_set_font_size(cr, 10.0);
    show_text_centered(cr, w->x + w->w / 2.0, w->y - 6.0, w->label);
}

static void
draw_band_label(cairo_t *cr, double cx, double y, const char *s)
{
    set_rgb(cr, C_BAND);
    cairo_set_font_size(cr, 11.0);
    show_text_centered(cr, cx, y, s);
}

/* Draw the top-left header: "OTT" in bold white, "by Zero:Archive" in a
 * smaller dim subtitle below. */
static void
draw_header(cairo_t *cr)
{
    /* "OTT" title, bold, 16pt. */
    set_rgb(cr, C_TITLE);
    cairo_select_font_face(cr, "sans-serif",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 16.0);
    cairo_move_to(cr, 16.0, 20.0);
    cairo_show_text(cr, "OTT");

    /* "by Zero:Archive" subtitle, normal weight, 9pt, dim. */
    set_rgb(cr, C_SUBTITLE);
    cairo_select_font_face(cr, "sans-serif",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9.0);
    cairo_move_to(cr, 16.0, 32.0);
    cairo_show_text(cr, "by Zero:Archive");
}

static void
on_expose(ott_ui_t *ui, const PuglExposeEvent *event)
{
    (void)event;
    cairo_t *cr = (cairo_t *)puglGetContext(ui->view);
    if (!cr) return;

    /* Background panel. */
    set_rgb(cr, C_BG);
    cairo_paint(cr);

    /* Header (uses bold then resets to normal for the rest). */
    draw_header(cr);
    cairo_select_font_face(cr, "sans-serif",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);

    /* Band column captions: HIGH / MID / LOW above the threshold knobs. */
    draw_band_label(cr, 150.0, 150.0, "HIGH");
    draw_band_label(cr, 300.0, 150.0, "MID");
    draw_band_label(cr, 450.0, 150.0, "LOW");

    /* Widgets. */
    for (int i = 0; i < OTT_WIDGET_COUNT; ++i) {
        const ott_widget_t *w = &ui->widgets[i];
        if (w->type == W_KNOB) {
            draw_knob(cr, w);
        } else {
            draw_toggle(cr, w);
        }
    }

    /* Subtle separator lines between the sections (1px, #333). The 0.5
     * offset keeps the line crisp under Cairo's anti-aliasing. */
    set_rgb(cr, C_SEP);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 16.0, 135.5);
    cairo_line_to(cr, (double)OTT_UI_WIDTH - 16.0, 135.5);
    cairo_stroke(cr);
    cairo_move_to(cr, 16.0, 280.5);
    cairo_line_to(cr, (double)OTT_UI_WIDTH - 16.0, 280.5);
    cairo_stroke(cr);

    /* Credits footer. */
    set_rgb(cr, C_CREDITS);
    cairo_select_font_face(cr, "sans-serif",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 8.0);
    show_text_centered(cr, (double)OTT_UI_WIDTH / 2.0,
                       (double)OTT_UI_HEIGHT - 8.0,
                       "Original OTT by Xfer Records. Linux port by Zero:Archive.");

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

    puglSetViewString(ui->view, PUGL_WINDOW_TITLE, "OTT - by Zero:Archive");
    puglSetViewHint(ui->view, PUGL_RESIZABLE, PUGL_FALSE);
    puglSetSizeHint(ui->view, PUGL_DEFAULT_SIZE, OTT_UI_WIDTH, OTT_UI_HEIGHT);
    puglSetSizeHint(ui->view, PUGL_MIN_SIZE, OTT_UI_WIDTH, OTT_UI_HEIGHT);
    puglSetSizeHint(ui->view, PUGL_MAX_SIZE, OTT_UI_WIDTH, OTT_UI_HEIGHT);
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
     * embeds the widget itself when one was provided). Hosts that drive
     * the window via showInterface will call ui_show() themselves; for
     * everyone else we have to make the window appear here. */
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

/* Show the UI window. Hosts call this when the user clicks "Show UI"
 * after previously hiding it (or after instantiate without a parent).
 *
 * We use PUGL_SHOW_FORCE_RAISE here instead of PUGL_SHOW_RAISE: the
 * latter only raises an already-mapped window, so the first click after
 * a hide sometimes just maps the window without bringing it to the
 * front, forcing the user to click "Show UI" a second time. FORCE_RAISE
 * always raises, which fixes the double-click issue. */
static int
ui_show(LV2UI_Handle handle)
{
    ott_ui_t *ui = (ott_ui_t *)handle;
    if (ui && ui->view) {
        puglShow(ui->view, PUGL_SHOW_FORCE_RAISE);
        /* Pump the event loop once so the MapWindow + raise requests
         * get flushed to the X server immediately, instead of waiting
         * for the next idle tick. */
        if (ui->world) {
            puglUpdate(ui->world, 0.0);
        }
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
