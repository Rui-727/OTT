# OTT LV2 UI Research

Research notes for adding a graphical UI to the OTT LV2 plugin. Covers the LV2
UI extension API, the available C/C++ toolkit options, drawing knobs and
sliders with Cairo, the layout of the original Xfer OTT UI, the build and
install integration, and the answers to specific design questions for our
plugin.

The existing plugin ships without a UI and relies on the host's generic
parameter sheet (Ardour, Carla, Jalv, Qtractor, Zrythm all work). The goal of
this document is to determine the smallest, most portable way to ship a real
knob-and-bypass UI that looks like the Xfer OTT plugin.

Sources are inline. The reference LV2 UI examples (eg-sampler, eg-scope) are
copied directly from the lv2/lv2-examples repository and quoted at length
because they are the canonical pugl + Cairo LV2 UI code.

## 1. The LV2 UI extension

### 1.1 What a UI is, in one paragraph

An LV2 UI is a separate shared library that the host loads on demand when the
user opens the plugin's GUI. The host calls `lv2ui_descriptor()` in that
library, gets back an `LV2UI_Descriptor`, and calls its `instantiate()` to
create a UI instance. The UI never talks to the plugin directly: it talks to
the host through two callbacks, `write_function` (UI to plugin, via the host)
and `port_event` (plugin to UI, via the host). This separation is deliberate
and is enforced by the spec: the host may load the UI in a different process
from the plugin, may load the UI on a different machine, may not load the UI
at all. The only contract is the port-based callback interface [lv2-ui-spec].

### 1.2 The `lv2ui:UI` type and the UI classes

`lv2ui:UI` is the abstract type for all UIs. Concrete UIs use one of the
platform-specific subclasses [lv2-ui-spec]:

- `ui:X11UI` - the widget is an X11 `Window` ID. Native on Linux. This is the
  one we want.
- `ui:CocoaUI` - the widget is an `NSView *`. Native on macOS.
- `ui:WindowsUI` - the widget is an `HWND`. Native on Windows.
- `ui:GtkUI` / `ui:Gtk3UI` - the widget is a `GtkWidget *`. The host must
  have already initialized GTK and be running the GLib main loop.
- `ui:Qt4UI` / `ui:Qt5UI` - the widget is a `QWidget *`. Same caveat as GTK:
  the host must have initialized Qt and be running its event loop.

For a plugin that ships a single UI binary that works in any host on Linux,
`ui:X11UI` is the right choice. The host (Ardour, Carla, Jalv, Qtractor,
Zrythm, REAPER) embeds the X11 window into its own plugin panel. A pugl-based
UI declares itself as `ui:X11UI` regardless of which drawing backend it uses
internally [lv2-examples-eg-sampler-manifest].

### 1.3 The `LV2UI_Descriptor` and `lv2ui_descriptor()`

The entry point mirrors `lv2_descriptor()` for plugins [ui-h]:

```c
typedef struct _LV2UI_Descriptor {
    const char* URI;
    LV2UI_Handle (*instantiate)(const struct _LV2UI_Descriptor* descriptor,
                                const char* plugin_uri,
                                const char* bundle_path,
                                LV2UI_Write_Function write_function,
                                LV2UI_Controller controller,
                                LV2UI_Widget* widget,
                                const LV2_Feature* const* features);
    void (*cleanup)(LV2UI_Handle ui);
    void (*port_event)(LV2UI_Handle ui,
                       uint32_t port_index,
                       uint32_t buffer_size,
                       uint32_t format,
                       const void* buffer);
    const void* (*extension_data)(const char* uri);
} LV2UI_Descriptor;

LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index);
```

`LV2UI_Handle` is an opaque pointer to the UI instance (analogous to
`LV2_Handle` for the plugin). `LV2UI_Controller` is an opaque host pointer
that the UI passes back as the first argument to `write_function`. The UI
must not interpret either pointer. `LV2UI_Widget` is `void *` and is what the
UI writes through the `widget` output parameter to hand its native window
back to the host. For an `ui:X11UI`, the UI writes the X11 `Window` ID
directly (cast through `LV2UI_Widget`, not through a pointer) [ui-h, lv2-ui-spec].

### 1.4 `instantiate()` in detail

The host calls `instantiate()` once per UI instance. The UI:

1. Allocates its private struct and stashes `write_function` and
   `controller` for later use.
2. Walks the `features` array to find host features it cares about (parent
   window, idle interface support, port map, touch, resize, options, log,
   URID map). The LV2 helper `lv2_features_query()` does this in one call,
   see eg-sampler [lv2-examples-eg-sampler-ui].
3. Creates its window with the host's parent (if `ui:parent` was passed)
   embedded.
4. Realizes and shows the window.
5. Writes the native window ID through the `widget` out parameter.

The canonical pattern from eg-sampler [lv2-examples-eg-sampler-ui]:

```c
static LV2UI_Handle
instantiate(const LV2UI_Descriptor*   descriptor,
            const char*               plugin_uri,
            const char*               bundle_path,
            LV2UI_Write_Function      write_function,
            LV2UI_Controller          controller,
            LV2UI_Widget*             widget,
            const LV2_Feature* const* features)
{
    SamplerUI* ui = (SamplerUI*)calloc(1, sizeof(SamplerUI));
    ui->write      = write_function;
    ui->controller = controller;
    *widget        = NULL;

    void*       parent  = NULL;
    const char* missing = lv2_features_query(
        features,
        LV2_URID__map,        &ui->map,           true,
        LV2_UI__parent,       &parent,            false,
        NULL);

    if (missing) { free(ui); return NULL; }

    ui->world = puglNewWorld(PUGL_MODULE, 0U);
    ui->view  = puglNewView(ui->world);
    puglSetWorldString(ui->world, PUGL_CLASS_NAME, "EgSampler");
    puglSetViewString(ui->view, PUGL_WINDOW_TITLE, "Example Sampler");
    puglSetViewHint(ui->view, PUGL_RESIZABLE, PUGL_TRUE);
    puglSetSizeHint(ui->view, PUGL_MIN_SIZE, MIN_CANVAS_W, MIN_CANVAS_H);
    puglSetSizeHint(ui->view, PUGL_DEFAULT_SIZE, DEF_CANVAS_W, DEF_CANVAS_H);
    puglSetHandle(ui->view, ui);
    puglSetEventFunc(ui->view, on_event);
    puglSetBackend(ui->view, puglCairoBackend());

    if (parent) {
        puglSetParent(ui->view, (PuglNativeView)parent);
    }

    puglRealize(ui->view);
    puglShow(ui->view, PUGL_SHOW_RAISE);

    *widget = (LV2UI_Widget)puglGetNativeView(ui->view);
    return ui;
}
```

### 1.5 Sending values to the plugin: `write_function`

`LV2UI_Write_Function` is the only sanctioned channel for UI to plugin
traffic [ui-h]:

```c
typedef void (*LV2UI_Write_Function)(LV2UI_Controller controller,
                                     uint32_t port_index,
                                     uint32_t buffer_size,
                                     uint32_t port_protocol,
                                     const void* buffer);
```

For a normal `lv2:ControlPort` input, `port_protocol` is `0` (which means
`ui:floatProtocol`), `buffer` points at a single `float`, and `buffer_size`
is `sizeof(float)`. The host writes that float to the port's buffer; the
plugin's `run()` reads it on the next block. There are no timing guarantees:
the value reaches the audio thread "as soon as possible" [lv2-ui-spec].

So when the user drags a knob, the UI computes the new float value and calls:

```c
float v = new_value;
ui->write(ui->controller, port_index, sizeof(float), 0, &v);
```

That is the whole API on the UI side. The OTT plugin has 13 control input
ports (depth, time, upward, downward, input_gain, output_gain, band1..3_thresh,
band1..3_gain, bypass), and the UI writes them by index. Indices match the
`lv2:index` in `ott.ttl` (0..12) [ott-ttl].

### 1.6 Receiving values from the plugin: `port_event`

`port_event` is the host to UI direction [ui-h]:

```c
void (*port_event)(LV2UI_Handle ui,
                   uint32_t port_index,
                   uint32_t buffer_size,
                   uint32_t format,
                   const void* buffer);
```

The host calls this when a control port value changes for any reason:
automation, preset load, host-side parameter edit, MIDI learn, or a write
back from another UI instance. By default the host only forwards changes on
`lv2:ControlPort` inputs in `ui:floatProtocol` (format `0`, `buffer` points
at a single `float`) [lv2-ui-spec]. The UI must not retain the `buffer`
pointer after the call returns.

A normal OTT handler is a one-line switch on `port_index` that updates the
matching cached value in the UI struct and triggers a redraw:

```c
static void port_event(LV2UI_Handle handle, uint32_t port_index,
                       uint32_t buffer_size, uint32_t format,
                       const void* buffer) {
    if (format != 0 || buffer_size != sizeof(float)) return;
    OttUI* ui = (OttUI*)handle;
    float v = *(const float*)buffer;
    if (port_index < OTT_PORT_COUNT) {
        ui->values[port_index] = v;
        puglObscureView(ui->view);  /* request redraw */
    }
}
```

For control ports that the UI does not need updates on (because it sent the
change itself and already knows the value), the host is smart enough to skip
the round-trip in most cases. Even if it does call back, the comparison
`if (v == ui->values[i]) return;` short-circuits the redraw.

### 1.7 Showing and hiding: `ui:showInterface`

Some hosts cannot embed the UI widget (no parent window, headless mode,
remote plugin). The `ui:showInterface` extension data lets the UI provide
a `show()` and `hide()` that opens the widget in its own top-level window
as a fallback [lv2-ui-spec, ui-h]:

```c
typedef struct _LV2UI_Show_Interface {
    int (*show)(LV2UI_Handle ui);
    int (*hide)(LV2UI_Handle ui);
} LV2UI_Show_Interface;
```

The UI returns this struct from `extension_data()` when asked for
`LV2_UI__showInterface`. If a UI supports show/hide it MUST also support
`idleInterface` (the host uses idle to drive the show/hide window) and MUST
return non-zero from `idle()` after the user closes the window [ui-h].

For OTT we want embedding in normal hosts, so we accept `ui:parent` and
embed. We can still implement `showInterface` as a fallback for hosts that
do not give us a parent (then `show()` calls `puglShow()` on a top-level
pugl view we already created without a parent). The cost is low and the
robustness is worth it.

### 1.8 Driving the UI loop: `ui:idleInterface`

The host owns the event loop. It calls the UI's `idle()` function from the
UI thread at a high rate (spec recommends at least 30 Hz) [ui-h]:

```c
typedef struct _LV2UI_Idle_Interface {
    int (*idle)(LV2UI_Handle ui);
} LV2UI_Idle_Interface;
```

For a pugl UI, `idle()` is one line:

```c
static int ui_idle(LV2UI_Handle handle) {
    const OttUI* ui = (const OttUI*)handle;
    if (ui->view) puglUpdate(ui->world, 0);
    return 0;
}
```

Returning non-zero signals the host that the UI window was closed by the
user; the host then stops calling `idle()` and may call `hide()` or destroy
the UI [ui-h]. We return 0 as long as the view is alive.

The UI advertises `idleInterface` both as `lv2:optionalFeature` and as
`lv2:extensionData` in the TTL, and returns the idle struct from
`extension_data()` [lv2-examples-eg-sampler-ui]:

```c
static const void* extension_data(const char* uri) {
    static const LV2UI_Idle_Interface idle = {ui_idle};
    if (!strcmp(uri, LV2_UI__idleInterface)) return &idle;
    return NULL;
}
```

### 1.9 Resizing: `ui:resize`

`ui:resize` is provided by the host as a feature and optionally by the UI as
extension data [ui-h]:

```c
typedef struct _LV2UI_Resize {
    LV2UI_Feature_Handle handle;
    int (*ui_resize)(LV2UI_Feature_Handle handle, int width, int height);
} LV2UI_Resize;
```

When the host passes the feature, the UI calls `ui_resize()` to inform the
host that it changed its own size. When the UI provides it via
`extension_data`, the host calls it to ask the UI to change size. The
extension is marked `owl:deprecated` in the spec in favour of the more
general options mechanism, but it is still universally supported and is the
practical way to handle resize today [lv2-ui-spec].

For OTT we will declare a fixed default size (say 560 x 360) and use
`ui:fixedSize` to tell the host we will not resize ourselves. If we later
add a resizable variant, we implement `ui:resize` as extension data so the
host can tell us when the user drags the corner.

### 1.10 Touch: `ui:touch`

`ui:touch` notifies the host that the user has grabbed or released a control
[ui-h]:

```c
typedef struct _LV2UI_Touch {
    LV2UI_Feature_Handle handle;
    void (*touch)(LV2UI_Feature_Handle handle, uint32_t port_index, bool grabbed);
} LV2UI_Touch;
```

The host uses this to suspend automation on the port while the user is
dragging the knob (like a motorised fader) and to support MIDI learn. The
UI calls `touch(handle, port_index, true)` on `BUTTON_PRESS` and
`touch(handle, port_index, false)` on `BUTTON_RELEASE`. The feature is
optional; if the host does not provide it, the UI simply skips the call.
For OTT we should implement touch because every major host supports it and
it is the difference between a usable and an annoying UI under automation
[lv2-ui-spec].

### 1.11 Other features worth knowing

- `ui:parent` (feature, data is a native window ID). Required for embedding.
  Always accept it as optional, even if the UI works fine without it.
- `ui:portMap` (feature, `LV2UI_Port_Map` struct). Maps port symbol to
  index. Lets a UI be distributed without knowing the plugin's port indices.
  Not needed for OTT since we ship UI and plugin together.
- `ui:portSubscribe` (feature, `LV2UI_Port_Subscribe` struct). Dynamically
  subscribe to a port at runtime. For OTT the static
  `ui:portNotification` declarations in the TTL are enough.
- `ui:floatProtocol`, `ui:peakProtocol`. The two standard port protocols.
  We use only `floatProtocol` for the 13 control ports. `peakProtocol` is
  for metering audio levels (we could use it later for a per-band gain
  reduction meter).
- `ui:fixedSize`, `ui:noUserResize`. Hints that the UI is not resizable.
- `ui:scaleFactor`, `ui:backgroundColor`, `ui:foregroundColor`,
  `ui:windowTitle`, `ui:updateRate`. Host-provided options for HiDPI and
  visual integration. The UI reads them through the `options` feature.
- `ui:portNotification` (data property). Declares which ports the host
  must forward to the UI. For `lv2:ControlPort` inputs the host does this
  implicitly, but declaring it explicitly is recommended and is what
  lv2lint checks for [lv2-ui-spec].

### 1.12 The forbidden features: `instance-access` and `data-access`

`instance-access` lets the host hand the UI a direct pointer to the
plugin's `LV2_Handle`. `data-access` lets the UI call the plugin's
`extension_data()`. Both exist, both are documented, and both are
"highly discouraged" in the spec text [lv2-instance-access, lv2-data-access]:

> Note that the use of this extension by UIs violates the important
> principle of UI/plugin separation, and is potentially a source of many
> problems. Accordingly, use of this extension is highly discouraged, and
> plugins should not expect hosts to support it, since it is often
> impossible to do so.

The reason is network transparency: a host that runs the plugin out of
process (Carla's plugin bridge, MOD, a future remote-host setup) cannot
hand the UI a valid pointer. We will not use either feature. The OTT UI
talks to the plugin purely through `write_function` and `port_event`.

## 2. Toolkit options

### 2.1 Raw X11 + Cairo

You open the X11 display with `XOpenDisplay(NULL)`, create a window with
`XCreateWindow`, get a Cairo surface with
`cairo_xlib_surface_create(display, window, visual, width, height)`, draw
on it with a `cairo_t *`, and pump `XNextEvent` yourself in `idle()`. Mouse
events come in as `XButtonEvent` / `XMotionEvent`, expose events as
`XExposeEvent`. No toolkit dependency beyond `libx11` and `libcairo`.

Pros:

- Maximum control. No abstraction in the way.
- Smallest dependency surface: `libx11` and `libcairo` are preinstalled on
  every Linux desktop.
- Easy to read, easy to debug, easy to step through.

Cons:

- You reimplement a lot of what pugl already does: parent window embedding,
  HiDPI handling, event loop integration with the host's idle calls,
  cross-platform (no Mac, no Windows).
- The host hands you an X11 `Window` ID via `ui:parent`. Reparenting your
  window into that parent and managing resize events from the host is
  fiddly to get right. Get it wrong and Carla hangs, Ardour shows a blank
  panel, or Jalv crashes.
- Future port to macOS or Windows means rewriting the whole window layer.

This is the right choice only if you want zero new dependencies and you are
willing to live with Linux-only. The Dragonfly plugins used to ship a
custom X11+Cairo layer; they have since moved to pugl.

### 2.2 pugl (recommended)

pugl is the LV2 project's own minimal portable GUI library. It is a C API
with optional C++ bindings, designed specifically for plugins, with no
implicit context and no mutable static data so it can be statically linked
into any plugin without polluting the host process [pugl-docs, pugl-repo].

Architecture [pugl-docs]:

- A `PuglWorld` is the top-level state (one per plugin UI).
- A `PuglView` is a drawable window. You create it, set its size hints,
  set its backend (Cairo, OpenGL, Vulkan, or stub), set an event handler,
  set a parent (for embedding), realize it, show it.
- All events go through one `PuglEvent` union dispatched to a single
  `onEvent(PuglView*, const PuglEvent*)` callback. Event types include
  `PUGL_CONFIGURE` (resize), `PUGL_EXPOSE` (redraw),
  `PUGL_BUTTON_PRESS` / `PUGL_BUTTON_RELEASE` / `PUGL_MOTION` / `PUGL_SCROLL`
  (mouse), `PUGL_KEY_PRESS` / `PUGL_KEY_RELEASE` (keyboard),
  `PUGL_POINTER_IN` / `PUGL_POINTER_OUT`, `PUGL_CLOSE`, `PUGL_REALIZE` /
  `PUGL_UNREALIZE`.
- The Cairo context is fetched with `puglGetContext(view)` during an
  expose event. The context is only valid for the duration of that event.
- The native window ID is fetched with `puglGetNativeView(view)` and
  handed to the host as the `LV2UI_Widget`.
- The host's `idle()` call maps to `puglUpdate(world, 0.0)`.

Backends [pugl-docs]:

- Cairo: easiest. 2D vector drawing. Antialiased. No GLSL required. This
  is what we want for OTT.
- OpenGL: hardware accelerated, good for shaders, scopes, 3D.
- Vulkan: maximum performance, maximum complexity. Not justified for a
  knob UI.
- Stub: no drawing, for tests.

The pugl API surface for a Cairo LV2 UI is tiny. From
eg-sampler/sampler_ui.c and eg-scope/examploscope_ui.c the full set of
calls we need is:

```c
puglNewWorld(PUGL_MODULE, 0U);
puglSetWorldString(world, PUGL_CLASS_NAME, "OTT");
puglNewView(world);
puglSetViewString(view, PUGL_WINDOW_TITLE, "OTT");
puglSetViewHint(view, PUGL_RESIZABLE, PUGL_FALSE);
puglSetSizeHint(view, PUGL_DEFAULT_SIZE, w, h);
puglSetHandle(view, ui);
puglSetEventFunc(view, on_event);
puglSetBackend(view, puglCairoBackend());
puglSetParent(view, (PuglNativeView)parent);
puglRealize(view);
puglShow(view, PUGL_SHOW_RAISE);
puglGetNativeView(view);          /* returns the X11 Window ID */
puglObscureView(view);            /* request full redraw */
puglGetContext(view);             /* returns cairo_t* during expose */
puglSetCursor(view, PUGL_CURSOR_HAND);
puglUpdate(world, 0);             /* in idle() */
puglFreeView(view);
puglFreeWorld(world);
```

That is the entire API needed for OTT.

Pros:

- Cross-platform (X11, macOS, Windows). Same UI source compiles for all
  three, the manifest.ttl just declares `ui:X11UI`, `ui:CocoaUI`, or
  `ui:WindowsUI` per platform.
- Built for embedding. `puglSetParent()` handles reparenting into the
  host's window correctly across all hosts.
- Designed for plugins. No global state, no statics, no main loop takeover.
- Used by the reference LV2 UI examples (eg-sampler, eg-scope), by
  setBfree (OpenGL), by x42, and historically by Dragonfly.
- Can be statically linked (the meson build produces static libs by
  default) so the installed plugin does not need a system pugl.

Cons:

- Adds a build dependency on pugl. On Debian/Ubuntu:
  `apt install libpugl-dev`. On Arch: `pacman -S pugl`. On Fedora:
  `dnf install pugl-devel`. Or vendor it as a meson subproject, or copy
  the headers and ~10 source files into the repo.
- The pugl 0.x API has churned between versions (0.5, 0.6, 0.7, 0.8,
  0.9, 0.10). Pin to a specific version (`pugl-0 >= 0.5.7` is what
  lv2-examples requires) and use pkg-config to find it.

This is the recommended approach for OTT. The rest of the document
assumes pugl + Cairo.

### 2.3 `ui:GtkUI` and `ui:Qt5UI`

GTK and Qt UIs get full widget sets: buttons, sliders, spinners, file
dialogs, theming. They are heavy. The spec explicitly warns that they are
"not suitable for binary distribution since multiple versions of Gtk/Qt can
not be used in the same process" [lv2-ui-spec]. The host must have
initialised the matching toolkit version before instantiating the UI, and
you can only have one major GTK version loaded per process. If the host
uses GTK3 and your UI uses GTK4, the UI will not load. Calf, Guitarix, and
some older Ardour-bundled plugins use GtkUI; modern LV2 plugins largely
avoid it.

Pros:

- Fastest path to a "looks like a normal desktop app" UI.
- Native file dialogs, tooltips, keyboard navigation for free.

Cons:

- Binary incompatibility across GTK/Qt versions.
- Heavy shared library dependencies.
- Hosts like Carla that already link a different toolkit version may not
  load the UI at all.
- The widget set actively fights you if you want a custom look (round
  knobs, custom slider thumbs, the OTT dark theme).

Not recommended for OTT. We want one UI binary that loads in every host.

### 2.4 suil

suil is the LV2 project's UI hosting library. It is for hosts, not for
plugin authors. A host that uses suil can load any UI type (X11, Gtk, Qt,
Cocoa, Windows) without linking every toolkit itself, because suil dlopens
the UI in a child process when needed. Plugin authors do not need to think
about suil: if you write a `ui:X11UI`, suil-based hosts will load it
correctly [lv2-ui-spec, suil-mention-in-zrythm]. Mentioned here only to
clear up the common confusion.

### 2.5 Custom Cairo-on-X11 (the old Dragonfly approach)

Before pugl matured, several plugin authors wrote their own small Cairo-on-X11
window wrapper. The pattern is roughly: open `XOpenDisplay`, create a child
`XCreateWindow` of the host's parent window, create a
`cairo_xlib_surface_create`, handle `Expose` by fetching a `cairo_t` and
drawing, handle `ButtonPress`/`MotionNotify`/`ButtonRelease` for input.
This is the same thing pugl does, but you write and maintain it yourself.

Today there is no good reason to do this. pugl gives you the same minimal
dependency footprint (Cairo + X11), handles the corner cases (HiDPI,
parent embedding, modifier keys, scroll wheels, the differences between
X11 and Wayland via XWayland), and is maintained by the LV2 project. If
you ever want macOS or Windows, you get them for free.

### 2.6 Other options considered and rejected

- **JUCE**: cross-platform C++ framework, exports LV2 via a wrapper. Heavy
  (tens of MB of source), pulls in its own drawing, its own event loop,
  its own parameter model. Overkill for a 13-knob UI. The Reddit
  linuxaudio thread on LV2 UI toolkits flags that JUCE's LV2 wrapper has
  limitations (parameter changes are block-rate, not sample-rate, which
  loses one of LV2's advantages) [reddit-toolkit].
- **DPF (DISTRHO Plugin Framework)**: a nice C++ framework that bundles
  pugl and exports LV2, VST2, VST3, CLAP, standalone. Worth considering
  for a future rewrite but adds C++ and a non-trivial build dependency
  for what is otherwise a pure-C project.
- **FLTK**: lightweight C++ toolkit. Not commonly used for LV2 UIs, no
  real ecosystem support, no advantage over pugl+Cairo.
- **BWidgets** [bwidgets]: a Cairo+pugl widget toolkit by the
  BLayout/BWidgets author. Provides ready-made knob/slider/button
  widgets drawn in Cairo. Tempting, but adds a dependency on a
  less-widely-deployed library. For 13 knobs and a bypass button, hand
  drawing in Cairo is faster than learning a new widget API.
- **XUiDesigner** [xuidesigner]: a WYSIWYG LV2 GUI creator that emits
  Cairo+pugl C code. Good for prototyping but produces code you then
  have to maintain. Not appropriate for a hand-tuned OTT clone.

### 2.7 Recommendation

pugl 0.5+ with the Cairo backend, declared as `ui:X11UI`, linked
statically if possible (or via pkg-config `pugl-0` and `pugl-cairo-0`).
C only, no C++. This is what the LV2 reference examples do, what x42 does,
what Dragonfly now does, and what setBfree does (with the OpenGL backend).

## 3. Drawing knobs and sliders in Cairo

### 3.1 Cairo basics

Cairo is a 2D vector drawing library with a C API. The state machine is:
set the source colour, set the line width, build a path with `move_to` /
`line_to` / `arc` / `curve_to` / `rectangle`, then either `stroke` (draw
the outline) or `fill` (fill the interior) [cairo-tutorial].

Useful calls for a knob UI:

- `cairo_set_source_rgb(cr, r, g, b)` / `cairo_set_source_rgba(cr, r, g, b, a)`.
  Components are 0.0 to 1.0.
- `cairo_set_line_width(cr, w)`.
- `cairo_arc(cr, cx, cy, r, start_angle, end_angle)`. Angles in radians,
  0 is east, positive is clockwise (Y axis points down in Cairo).
- `cairo_move_to`, `cairo_line_to`, `cairo_rectangle`, `cairo_curve_to`.
- `cairo_stroke`, `cairo_fill`, `cairo_stroke_preserve`,
  `cairo_fill_preserve` (preserve keeps the path for the next call).
- `cairo_save` / `cairo_restore` for scoping transforms.
- `cairo_translate`, `cairo_scale`, `cairo_rotate`.
- `cairo_set_font_size`, `cairo_select_font_face`,
  `cairo_text_extents`, `cairo_show_text`.
- `cairo_clip` to restrict drawing to a region.

Cairo uses antialiased rendering by default. Crisp 1-pixel lines need a
0.5 pixel offset (a 1.0-wide stroke from (0,0) to (10,0) covers pixels
-0.5..0.5 and 9.5..10.5, i.e. two half-coverage rows). The eg-sampler code
shifts by `cairo_translate(cr, 0.5, 0.5)` before drawing 1-pixel grid
lines [lv2-examples-eg-sampler-ui].

### 3.2 A rotary knob

A rotary knob is a circle with a pointer line. Parameters: center `(cx,
cy)`, radius `r`, value `v` normalised 0..1, sweep angle (typically 270
degrees, from -135 to +135 degrees measured from straight up).

```c
static void draw_knob(cairo_t* cr, double cx, double cy, double r,
                      double v, const char* label) {
    const double sweep = 270.0 * M_PI / 180.0;        /* total arc */
    const double start = -M_PI / 2 - sweep / 2;        /* -135 deg */
    const double end   = -M_PI / 2 + sweep / 2;        /* +135 deg */
    const double angle = start + v * sweep;            /* pointer angle */

    /* background circle */
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.18);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_fill(cr);

    /* arc track (unfilled portion) */
    cairo_set_source_rgb(cr, 0.30, 0.30, 0.34);
    cairo_set_line_width(cr, 2.0);
    cairo_arc(cr, cx, cy, r + 4, start, end);
    cairo_stroke(cr);

    /* arc fill (from start to current value) */
    cairo_set_source_rgb(cr, 0.90, 0.55, 0.13);   /* OTT orange */
    cairo_set_line_width(cr, 3.0);
    cairo_arc(cr, cx, cy, r + 4, start, angle);
    cairo_stroke(cr);

    /* pointer line from center to edge */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + r * cos(angle), cy + r * sin(angle));
    cairo_stroke(cr);

    /* label below */
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_font_size(cr, 11.0);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);
    cairo_move_to(cr, cx - ext.width / 2, cy + r + 18);
    cairo_show_text(cr, label);
}
```

### 3.3 Knob mouse interaction

The standard knob gesture is vertical drag: press the left button on the
knob, drag up to increase, drag down to decrease. The drag distance maps
to the value range with a sensitivity constant (typically 200 pixels for
the full range, so 1 pixel = 0.5%). Holding Shift makes the drag finer
(typically 5x slower, so 1000 pixels for the full range). Holding Ctrl
snaps to the default. Double-click resets to the default.

The eg-scope example shows the pattern. On `BUTTON_PRESS` it records the
start position and the start value [lv2-examples-eg-scope-ui]:

```c
case PUGL_BUTTON_PRESS:
    ui->start_mouse_x = ui->mouse_x = event->button.x;
    ui->start_mouse_y = ui->mouse_y = event->button.y;
    ui->mouse_down    = true;
    ui->start_amp     = ui->amp;
    ui->start_speed   = ui->speed;
    break;
```

On `MOTION` while `mouse_down` it computes the new value from the Y
delta and a sensitivity constant [lv2-examples-eg-scope-ui]:

```c
static void on_motion(EgScopeUI* ui, const PuglMotionEvent* event) {
    ui->mouse_x = event->x;
    ui->mouse_y = event->y;
    static const float y_accel = 0.025f;
    ui->amp = MAX(0.1f, MIN(6.0f,
                y_accel * (ui->start_amp + (ui->start_mouse_y - ui->mouse_y))));
}
```

For OTT, the same pattern with a port-specific sensitivity, a Shift
modifier check via `event->motion.state`, and a `write_function` call to
push the value to the plugin:

```c
case PUGL_BUTTON_PRESS:
    if (event->button.button == 0) {            /* left button */
        OttKnob* k = find_knob_at(ui, event->button.x, event->button.y);
        if (k) {
            ui->drag_knob     = k;
            ui->drag_start_y  = event->button.y;
            ui->drag_start_v  = k->value;
            ui->drag_fine     = event->button.state & PUGL_MOD_SHIFT;
            if (ui->touch)
                ui->touch->touch(ui->touch->handle, k->port_index, true);
        }
    }
    break;

case PUGL_MOTION:
    if (ui->drag_knob) {
        double dy = ui->drag_start_y - event->motion.y;
        double sens = ui->drag_fine ? 0.001 : 0.005;
        double new_v = ui->drag_start_v + dy * sens;
        if (new_v < 0) new_v = 0;
        if (new_v > 1) new_v = 1;
        ui->drag_knob->value = new_v;
        float raw = unmap_knob(ui->drag_knob, new_v);
        ui->write(ui->controller, ui->drag_knob->port_index,
                  sizeof(float), 0, &raw);
        puglObscureView(ui->view);
    }
    break;

case PUGL_BUTTON_RELEASE:
    if (ui->drag_knob) {
        if (ui->touch)
            ui->touch->touch(ui->touch->handle, ui->drag_knob->port_index, false);
        ui->drag_knob = NULL;
    }
    break;
```

`unmap_knob` takes the normalised 0..1 value and maps it to the port's
real range (e.g. for input_gain: `-24 + 48 * v` to get -24..+24 dB; for
depth: just `v`; for bypass: `(int)(v > 0.5)`).

The OTT plugin's ports use these ranges [ott-ttl]:

| Port | Range | Map from 0..1 |
|------|-------|----------------|
| depth | 0..1 | `v` |
| time | 0..1 | `v` |
| upward | 0..1 | `v` |
| downward | 0..1 | `v` |
| input_gain | -24..+24 dB | `-24 + 48 * v` |
| output_gain | -24..+24 dB | `-24 + 48 * v` |
| band1..3_thresh | -60..0 dB | `-60 + 60 * v` |
| band1..3_gain | -24..+24 dB | `-24 + 48 * v` |
| bypass | 0..1 toggled | `(v > 0.5) ? 1.0f : 0.0f` |

### 3.4 A vertical slider (for the band thresholds)

The real Xfer OTT uses vertical black sliders for the band thresholds in
the middle of the UI. A vertical slider is a rectangle with a draggable
thumb:

```c
static void draw_vslider(cairo_t* cr, double x, double y, double w, double h,
                         double v, const char* label) {
    /* track */
    cairo_set_source_rgb(cr, 0.10, 0.10, 0.12);
    cairo_rectangle(cr, x + w/2 - 1, y, 2, h);
    cairo_fill(cr);

    /* thumb */
    double thumb_y = y + (1.0 - v) * h;
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.88);
    cairo_rectangle(cr, x, thumb_y - 4, w, 8);
    cairo_fill(cr);

    /* label */
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_font_size(cr, 11.0);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);
    cairo_move_to(cr, x + w/2 - ext.width/2, y + h + 14);
    cairo_show_text(cr, label);
}
```

Mouse handling for a slider is the same drag pattern as the knob but
along the Y axis within the slider's rectangle, with the value computed
as `(1 - (y - top) / h)` clamped to 0..1.

For OTT we will likely use knobs for everything (matching the existing
parameter table and simplifying the code). The slider is documented here
for completeness and as a fallback if we want a closer visual match to
the original Xfer UI.

### 3.5 A bypass toggle button

A bypass button is a rectangle that fills differently when active:

```c
static void draw_bypass(cairo_t* cr, double x, double y, double w, double h,
                        bool on) {
    if (on) {
        cairo_set_source_rgb(cr, 0.90, 0.30, 0.20);  /* red when bypassed */
    } else {
        cairo_set_source_rgb(cr, 0.20, 0.45, 0.20);  /* dim green when active */
    }
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_font_size(cr, 12.0);
    const char* label = on ? "BYPASS" : "ACTIVE";
    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);
    cairo_move_to(cr, x + w/2 - ext.width/2, y + h/2 + ext.height/2);
    cairo_show_text(cr, label);
}
```

A click anywhere inside the rectangle toggles the bypass port between 0.0
and 1.0 and calls `write_function`.

### 3.6 Value text

Showing the current value below each knob is a `cairo_show_text` call with
the formatted string. The format depends on the port:

```c
static void format_value(char* buf, size_t n, uint32_t port, float v) {
    switch (port) {
        case OTT_PORT_DEPTH:
        case OTT_PORT_TIME:
        case OTT_PORT_UPWARD:
        case OTT_PORT_DOWNWARD:
            snprintf(buf, n, "%d%%", (int)(v * 100 + 0.5));
            break;
        case OTT_PORT_INPUT_GAIN:
        case OTT_PORT_OUTPUT_GAIN:
        case OTT_PORT_BAND1_THRESH:
        case OTT_PORT_BAND2_THRESH:
        case OTT_PORT_BAND3_THRESH:
        case OTT_PORT_BAND1_GAIN:
        case OTT_PORT_BAND2_GAIN:
        case OTT_PORT_BAND3_GAIN:
            snprintf(buf, n, "%+.1f dB", v);
            break;
        case OTT_PORT_BYPASS:
            snprintf(buf, n, v > 0.5f ? "BYPASS" : "ON");
            break;
    }
}
```

Cairo's `cairo_select_font_face(cr, "sans-serif",
CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD)` is a reasonable default.
The exact font used depends on what `fontconfig` resolves "sans-serif" to
on the user's system, which is fine for a portable plugin.

### 3.7 Redrawing

Pugl is event-driven: you only draw during a `PUGL_EXPOSE` event
[pugl-docs]. To request a redraw, call `puglObscureView(view)` (full
redraw) or `puglObscureRegion(view, x, y, w, h)` (partial). The host then
sends an expose event on the next idle cycle. The eg-sampler UI calls
`puglObscureView(ui->view)` after receiving a new peak buffer; eg-scope
uses `puglObscureRegion` for incremental updates of just the changed
columns [lv2-examples-eg-sampler-ui, lv2-examples-eg-scope-ui].

For OTT with 14 widgets, just call `puglObscureView` after any state
change. The expose handler then redraws everything; Cairo on a 560x360
window with 14 widgets is well under a millisecond on any modern machine.

## 4. The OTT UI layout

### 4.1 What the real Xfer OTT UI looks like

Multiple sources describe the standalone Xfer OTT UI consistently
[edmprod-ott, production-expert-ott, virtualplaying-ott]. The 2022 facelift
version (the one everyone uses now) is laid out as:

```
+--------------------------------------------------+
|   DEPTH     TIME    IN GAIN   OUT GAIN           |   <- 4 knobs across the top
|                                                  |
|   +--------+   +--------+   +--------+           |
|   |   H    |   |   M    |   |   L    |           |   <- 3 band columns
|   | (slider|   | (slider|   | (slider|           |      each with a vertical
|   |  thresh|   |  thresh|   |  thresh|           |      threshold slider
|   |  + knob|   |  + knob|   |  + knob|           |      + an output gain knob)
|   |  gain) |   |  gain) |   |  gain) |           |
|   +--------+   +--------+   +--------+           |
|                                                  |
|   UPWARD                       DOWNWARD          |   <- 2 knobs across the bottom
+--------------------------------------------------+
```

Specifics from the sources:

- **Top row, 4 knobs**: Depth (dry/wet), Time (attack/release scaling),
  In Gain (input gain), Out Gain (output gain) [edmprod-ott,
  production-expert-ott].
- **Middle, 3 columns** labelled H, M, L (High, Mid, Low):
  - Each column has a vertical black threshold slider in the center.
  - Below the slider: a yellow/green meter showing the per-band
    compression (white line = original level, yellow line = compressed
    level, green = downward zone, yellow = upward zone).
  - Each column has an output gain knob (drawn as a small knob at the
    bottom of the column).
  - The crossover frequencies are fixed at 88.3 Hz and 2.5 kHz and are
    not shown as user-editable [edmprod-ott].
- **Bottom row, 2 knobs**: Upward (scales the upward compression ratio
  from 0 to the full 4:1) and Downward (scales the downward compression
  ratio from 0 to the full preset ratios) [edmprod-ott].
- **Hidden feature**: Ctrl+click on any parameter resets it. Ctrl+click
  on a band's H/M/L label disables that band's upward or downward
  compression [edmprod-ott]. We will not replicate this in v1 of the UI.

The original plugin's background is dark grey (approximately #1f1f1f),
knob pointers are white, knob fill arcs are an orange/amber colour
(approximately #e68c0d), and text is light grey. The overall feel is
compact and dense, not flashy.

### 4.2 Our layout

Our port set [ott-ttl] is close to but not identical to the Xfer plugin.
We have 13 control ports:

- 4 top-row knobs (depth, time, upward, downward) - matches Xfer's 4 top
  knobs except Xfer splits upward/downward to the bottom. We will follow
  Xfer's visual layout (Upward/Downward at the bottom) even though our
  ports 2 and 3 are upward and downward, because users expect the Xfer
  layout. The top row will be Depth, Time, In Gain, Out Gain (ports 0,
  1, 4, 5).
- 6 band knobs (band1..3_thresh, band1..3_gain) - matches Xfer's 3
  threshold sliders + 3 output gain knobs. We will draw the thresholds
  as vertical sliders to match Xfer, and the band gains as small knobs
  below each slider.
- 1 bypass button.

Proposed OTT UI layout, 560 x 360 px:

```
+----------------------------------------------------+
|  DEPTH    TIME     IN GAIN    OUT GAIN             |   y = 0..80
|   [knob]  [knob]   [knob]     [knob]               |
|                                                    |
|  +--------+    +--------+    +--------+            |   y = 90..250
|  | HIGH   |    |  MID   |    |  LOW   |            |
|  |  ||    |    |  ||    |    |  ||    |            |   threshold
|  |  ||    |    |  ||    |    |  ||    |            |   sliders (vertical)
|  |  ||    |    |  ||    |    |  ||    |            |
|  | (knob) |    | (knob) |    | (knob) |            |   band output
|  |  GAIN  |    |  GAIN  |    |  GAIN  |            |   gain knobs
|  +--------+    +--------+    +--------+            |
|                                                    |
|    UPWARD                DOWNWARD      [BYPASS]    |   y = 260..340
|    [knob]                [knob]        [toggle]    |
+----------------------------------------------------+
```

Coordinates (approximate):

- Top row knobs at y=20, x=80, 200, 320, 440. Radius 28.
- Band columns at x=80, 280, 480, width 80 each, y=90..250.
  - Slider rectangle: x=column+30, y=90..200.
  - Band gain knob below slider: cx=column+40, cy=235, r=20.
- Bottom row knobs at x=140, 340, y=290. Radius 28.
- Bypass button at x=460, y=275, 80x30.

This layout matches the Xfer OTT visual arrangement, fits in a 560x360
window (a reasonable embedded size that hosts like Ardour, Carla, and
Jalv handle well), and exposes all 13 control ports plus the bypass
button.

### 4.3 Port to widget mapping

| Port index | Port symbol | Widget | Widget rect / center |
|-----------:|-------------|--------|----------------------|
| 0 | depth | knob | (80, 20) r=28 |
| 1 | time | knob | (200, 20) r=28 |
| 4 | input_gain | knob | (320, 20) r=28 |
| 5 | output_gain | knob | (440, 20) r=28 |
| 6 | band1_thresh (high) | vslider | column x=480, y=90..200 |
| 7 | band2_thresh (mid) | vslider | column x=280, y=90..200 |
| 8 | band3_thresh (low) | vslider | column x=80, y=90..200 |
| 9 | band1_gain (high) | knob | (520, 235) r=20 |
| 10 | band2_gain (mid) | knob | (320, 235) r=20 |
| 11 | band3_gain (low) | knob | (120, 235) r=20 |
| 2 | upward | knob | (140, 290) r=28 |
| 3 | downward | knob | (340, 290) r=28 |
| 12 | bypass | toggle | (460, 275) 80x30 |

Note that the band ordering in the UI is reversed (high on the right,
low on the left) to match the original Xfer OTT visual, where the high
band is on the left and the low band is on the right. Actually looking
again at the screenshots described in the sources, Xfer has H on the
left, M in the middle, L on the right [edmprod-ott]. We will match that:
H on the left (column x=80), M in the middle (x=280), L on the right
(x=480). Port indices 6, 7, 8 are band1, band2, band3 which the DSP
treats as low, mid, high (band1 = low, crossover 88.3 Hz below it). So
the column at x=80 (leftmost, H in the Xfer layout) is port 8 (band3 =
high), the middle column is port 7 (band2 = mid), and the rightmost
column is port 6 (band1 = low). This needs careful attention in the
widget table.

## 5. Build integration

### 5.1 Bundle layout

An LV2 bundle is a directory ending in `.lv2` containing at minimum a
`manifest.ttl`. Our current bundle is `ott.lv2/` with `ott.so`,
`manifest.ttl`, `ott.ttl`. Adding a UI means adding `ott_ui.so` and
declaring it in `manifest.ttl` (and `ott.ttl`) [lv2-examples-eg-sampler-manifest]:

```
ott.lv2/
    manifest.ttl
    ott.ttl
    ott.so            <- plugin binary (existing)
    ott_ui.so         <- UI binary (new)
```

The spec recommends keeping the UI in a separate .so so hosts can skip
loading it when running headless [lv2-ui-spec]. The cost is a few KB
of disk; the benefit is that the plugin .so stays small and pure-C with
no Cairo or pugl dependency.

### 5.2 The `manifest.ttl`

Current `manifest.ttl` [ott-manifest]:

```turtle
@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

<https://github.com/Rui-727/OTT>
    a lv2:Plugin ;
    lv2:binary <ott.so> ;
    rdfs:seeAlso <ott.ttl> .
```

After adding the UI it becomes:

```turtle
@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .

<https://github.com/Rui-727/OTT>
    a lv2:Plugin ;
    lv2:binary <ott.so> ;
    rdfs:seeAlso <ott.ttl> .

<https://github.com/Rui-727/OTT#ui>
    a ui:X11UI ;
    lv2:binary <ott_ui.so> ;
    rdfs:seeAlso <ott.ttl> .
```

The `ui:X11UI` type tells the host the UI's `LV2UI_Widget` is an X11
`Window` ID. The `lv2:binary` points to the UI shared object. The
`rdfs:seeAlso` points to `ott.ttl` where the rest of the UI description
lives.

### 5.3 The `ott.ttl` additions

The UI is associated with the plugin via a `ui:ui` triple on the plugin
subject [lv2-ui-spec]. Add to the existing plugin description in
`ott.ttl`:

```turtle
<https://github.com/Rui-727/OTT>
    a lv2:Plugin , lv2:CompressorPlugin ;
    /* ... existing ports ... */
    ui:ui <https://github.com/Rui-727/OTT#ui> .
```

And add a separate subject for the UI:

```turtle
<https://github.com/Rui-727/OTT#ui>
    a ui:X11UI ;
    lv2:optionalFeature ui:idleInterface ,
                        ui:resize ,
                        ui:touch ;
    lv2:extensionData ui:idleInterface ,
                      ui:showInterface ;
    ui:fixedSize true .
```

This declares [lv2-ui-spec]:

- The UI is an `ui:X11UI`.
- It would like (but does not require) the `ui:idleInterface` feature so
  the host drives its event loop, the `ui:resize` feature so it can
  request resizes, and the `ui:touch` feature so it can notify the host
  of knob grabs for automation.
- It provides `ui:idleInterface` and `ui:showInterface` as extension data
  (returned from `extension_data()` in the C code).
- It is a fixed-size UI (will not resize itself).

The `ui:portNotification` declaration is optional for `lv2:ControlPort`
inputs because the host forwards those by default. We will skip it; if
lv2lint complains we add the explicit declarations.

### 5.4 The Makefile

The current `Makefile` builds `ott.so` from `ott_dsp.o` and `ott_lv2.o`
[ott-makefile]. We add a target for `ott_ui.so`. Use pkg-config to find
pugl and Cairo so the build works across distros:

```makefile
CC ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g -fPIC
PREFIX ?= /usr/local
LV2_DIR ?= $(HOME)/.lv2

PLUGIN = ott.lv2

UI_CFLAGS  := $(shell pkg-config --cflags pugl-0 pugl-cairo-0 cairo 2>/dev/null)
UI_LIBS    := $(shell pkg-config --libs   pugl-0 pugl-cairo-0 cairo 2>/dev/null)

all: $(PLUGIN)/ott.so $(PLUGIN)/ott_ui.so

$(PLUGIN)/ott.so: ott_dsp.o ott_lv2.o
	mkdir -p $(PLUGIN)
	$(CC) $(CFLAGS) -shared -o $@ $^ -lm

$(PLUGIN)/ott_ui.so: ott_ui.o lv2/ui.h
	mkdir -p $(PLUGIN)
	$(CC) $(CFLAGS) $(UI_CFLAGS) -shared -o $@ ott_ui.o \
	    $(UI_LIBS) -lm -Wl,-z,nodelete

ott_ui.o: ott_ui.c lv2/ui.h
	$(CC) $(CFLAGS) $(UI_CFLAGS) -c -o $@ ott_ui.c

# ... existing ott_dsp.o, ott_lv2.o, test, install targets ...

install: all
	mkdir -p $(LV2_DIR)/$(PLUGIN)
	cp $(PLUGIN)/ott.so $(LV2_DIR)/$(PLUGIN)/
	cp $(PLUGIN)/ott_ui.so $(LV2_DIR)/$(PLUGIN)/
	cp manifest.ttl $(LV2_DIR)/$(PLUGIN)/
	cp ott.ttl $(LV2_DIR)/$(PLUGIN)/
```

Two important linker flags:

- `-Wl,-z,nodelete` keeps the UI .so from being unloaded when the host
  destroys the UI instance. pugl and Cairo register cleanup handlers and
  caches that get corrupted if the library is unloaded and reloaded. The
  LV2 spec calls this out as the modern replacement for the deprecated
  `ui:makeSONameResident` feature [lv2-ui-spec].
- `-shared` and `name_prefix=''` (in meson terms, no `lib` prefix). The
  .so must be named `ott_ui.so` (not `libott_ui.so`) because the
  `lv2:binary` URI in the TTL points at `ott_ui.so` as a relative path.

### 5.5 The bundled `lv2/ui.h`

The current repo ships a minimal `lv2/lv2.h` so the build does not need
the `lv2-dev` package [ott-readme, ott-lv2-h]. We do the same for the UI
by shipping `lv2/ui.h` with the definitions we need:
`LV2UI_Descriptor`, `LV2UI_Handle`, `LV2UI_Controller`, `LV2UI_Widget`,
`LV2UI_Write_Function`, `LV2UI_Idle_Interface`, `LV2UI_Show_Interface`,
`LV2UI_Touch`, `LV2UI_Resize`, the `LV2_UI__*` URI macros, and the
`lv2ui_descriptor()` prototype. About 200 lines, modelled on the
upstream `ui.h` from the LV2 repo [ui-h].

The pugl and Cairo headers still need to come from the system (or a
vendored copy of pugl). Those are larger and not worth re-implementing.

### 5.6 How hosts discover and load the UI

LV2 hosts discover plugins by walking the LV2 path (typically
`~/.lv2`, `/usr/lib/lv2`, `/usr/local/lib/lv2`, set by `LV2_PATH` env
var), reading each `manifest.ttl`, and following `rdfs:seeAlso` links
[lv2-ui-spec]. When a host finds a `lv2:Plugin` it loads the plugin
binary. When it finds a `ui:UI` (or a `ui:ui` triple on the plugin), it
notes the UI URI and binary path but does not load the UI binary until
the user opens the plugin's GUI.

When the user opens the GUI:

1. The host checks which UI classes it supports. For an X11 host this is
   `ui:X11UI`. For a macOS host this is `ui:CocoaUI`. If the plugin has
   multiple UIs of different classes, the host picks the one it supports.
2. The host dlopens the UI binary at the path given by `lv2:binary`.
3. The host looks up `lv2ui_descriptor` and calls it with index 0.
4. The host calls `instantiate()` with the plugin URI, the bundle path,
   a `write_function` callback, a `controller` handle, an out-param for
   the widget, and the features array. The features array includes
   `ui:parent` (with the host's parent window ID), `ui:touch` (with the
   host's touch callback), `ui:portMap`, `ui:options` (with background
   color, scale factor, etc.), and any others the host supports.
5. The UI creates its window, embeds it in the parent, writes the
   X11 `Window` ID into `*widget`, and returns its `LV2UI_Handle`.
6. The host calls `extension_data(LV2_UI__idleInterface)` to get the
   idle callback, and calls `idle()` from the UI thread at high rate
   (30+ Hz).
7. The host calls `port_event()` whenever a control port value changes.
8. When the user closes the GUI, the host calls `cleanup()` and the UI
   destroys its window.

If the host cannot embed the UI (no parent, headless), it asks for
`ui:showInterface` via `extension_data()` and calls `show()` to open the
UI in its own top-level window [ui-h].

For OTT we need to support both: embed when given a parent, fall back to
a top-level window when not. The pugl pattern in eg-sampler handles this
naturally: only call `puglSetParent` if `parent` is non-NULL, otherwise
leave the view as a top-level window [lv2-examples-eg-sampler-ui].

## 6. Specific questions answered

### 6.1 Can we use pugl + Cairo for a C-only UI?

Yes. The eg-sampler.lv2/sampler_ui.c example is exactly this: pure C,
pugl, Cairo, no C++ [lv2-examples-eg-sampler-ui]. pugl ships C++ bindings
but the C API is the primary one and is fully featured. The eg-scope UI
is also pure C with pugl + Cairo [lv2-examples-eg-scope-ui]. Our OTT UI
will be the same: one `ott_ui.c` file, compiled with `-std=gnu11`, linked
against `pugl-0`, `pugl-cairo-0`, and `cairo`.

### 6.2 What's the minimal set of headers and libraries needed?

Headers:

- `lv2/ui.h` - the LV2 UI extension header. We will bundle a minimal
  version in `lv2/ui.h` next to the existing `lv2/lv2.h`.
- `pugl/pugl.h` - the core pugl API.
- `pugl/cairo.h` - the Cairo backend accessor (`puglCairoBackend()`).
- `cairo.h` - the Cairo drawing API.

Libraries (via pkg-config):

- `pugl-0` - the core pugl platform library (X11 backend).
- `pugl-cairo-0` - the pugl Cairo backend.
- `cairo` - Cairo itself.
- `m` - math library (for `cos`, `sin`, `M_PI`).

On Debian/Ubuntu: `apt install libpugl-dev libcairo2-dev`. On Arch:
`pacman -S pugl cairo`. On Fedora: `dnf install pugl-devel cairo-devel`.

If we want to avoid the pugl system dependency entirely we can vendor
pugl into the repo (copy the headers and the ~10 source files for the
X11 + Cairo backend, ~5000 lines total). This is what the meson
`subprojects/pugl.wrap` mechanism does in lv2-examples. For a first
version, requiring `libpugl-dev` is fine.

### 6.3 How does the host pass the plugin instance to the UI?

It does not, by design. The LV2 spec is explicit that the UI and the
plugin are separate and communicate only through the port-based callback
interface [lv2-ui-spec]:

> The process that loads the shared object file containing the UI code
> and the process that loads the shared object file containing the
> actual plugin implementation are not necessarily the same process
> (and not even necessarily on the same machine). This means that plugin
> and UI code MUST NOT use singletons and global variables and expect
> them to refer to the same objects in the UI and the actual plugin.
> The function callback interface defined in this header is the only
> method of communication between UIs and plugin instances.

At `instantiate()` the host hands the UI four things:

1. The plugin URI (a string, so the UI can verify it is the right plugin).
2. The bundle path (a directory path, so the UI can load resources like
   fonts or images shipped alongside the .so).
3. A `write_function` callback and a `controller` handle. The UI calls
   `write_function(controller, port_index, sizeof(float), 0, &value)` to
   push a value to the plugin's input port. The host is responsible for
   routing that write to the plugin's port buffer.
4. The features array, which may include `ui:parent` (parent window for
   embedding), `ui:touch`, `ui:portMap`, `ui:options`, `urid:map`,
   `urid:unmap`, `log:log`, and others.

The `instance-access` and `data-access` features exist but are
"highly discouraged" and not supported by all hosts [lv2-instance-access,
lv2-data-access]. We will not use them. The OTT UI gets the current
parameter values from `port_event()` calls (the host forwards them when
the UI is instantiated and whenever they change) and pushes new values
via `write_function()`.

To get the initial values when the UI opens, the LV2 convention is: the
host calls `port_event()` for every control port shortly after
`instantiate()`. Some hosts do this automatically; others require the UI
to declare `ui:portNotification` for each port in the TTL. We will
declare them explicitly so it works everywhere:

```turtle
<https://github.com/Rui-727/OTT#ui>
    a ui:X11UI ;
    /* ... */
    ui:portNotification [
        ui:plugin <https://github.com/Rui-727/OTT> ;
        lv2:symbol "depth" ;
        ui:protocol ui:floatProtocol
    ] , [
        ui:plugin <https://github.com/Rui-727/OTT> ;
        lv2:symbol "time" ;
        ui:protocol ui:floatProtocol
    ] , /* ... and so on for all 13 control ports ... */ .
```

Using `lv2:symbol` instead of `ui:portIndex` is recommended by the spec
because port symbols are stable across versions while indices are not
[lv2-ui-spec].

### 6.4 How does real-time-safe parameter passing work?

The audio thread and the UI thread never directly communicate. The host
mediates entirely through the control port buffers [lv2-ui-spec,
lv2-examples-eg-sampler-ui].

Data flow, UI to plugin:

1. The user drags a knob. The UI's `on_event` handler (running in the UI
   thread) computes the new float value and calls
   `write_function(controller, port_index, sizeof(float), 0, &value)`.
2. The host's `write_function` implementation copies the float into the
   port's buffer (the same `float *` the host earlier passed to the
   plugin's `connect_port()`).
3. On the next `run()` call, the plugin's audio thread reads
   `*self->port_index` and uses it. This is exactly what `ott_lv2.c`
   already does with `read_port(self->depth, 1.0f)` [ott-lv2-c].

Data flow, plugin to UI:

1. The plugin writes nothing to input control ports. It only reads them.
2. The host watches control port values. When one changes (because the
   user moved the UI's knob, because automation wrote a new value,
   because a preset was loaded), the host calls the UI's `port_event()`
   in the UI thread with the new value.
3. The UI updates its cached value and triggers a redraw.

Real-time safety comes from two facts:

1. A 32-bit aligned float write is atomic on every CPU architecture that
   LV2 runs on (x86, x86_64, ARM, ARM64). The audio thread's read of
   `*port` and the host's write of `*port` cannot tear. The worst case
   is that the audio thread reads the value one block late.
2. The UI thread and the audio thread never share a data structure that
   requires locking. The only shared state is the single `float` per
   port, which is written by the host (acting on behalf of the UI) and
   read by the plugin's audio thread. No locks, no atomics, no memory
   barriers needed beyond what the single-word write provides.

The OTT plugin's `run()` already reads all 13 control ports at the start
of each block and never writes them [ott-lv2-c]. This is already
real-time safe. Adding a UI changes nothing in the plugin's run path.

The UI's `port_event()` runs in the UI thread, not the audio thread, and
is allowed to do whatever it wants: allocate, redraw, format strings.
The LV2 spec is explicit that "All functions [on the UI] may only be
called in the UI thread" [lv2-ui-spec]. The only threading constraint on
the UI is that it must not block the audio thread, which it cannot do
because the audio thread never calls into the UI.

If we later want metering (per-band gain reduction meters, output level
meters), we use the `ui:peakProtocol` instead of `ui:floatProtocol`. The
host computes peaks in the audio thread (cheaply) and ships them to the
UI at a fixed rate (typically 25-30 Hz) via `port_event()` with the peak
protocol. This keeps the audio thread free of UI work and the UI thread
free of audio work. For v1 of the OTT UI we will skip meters; the DSP
already has the per-band gain reduction state and exposing it later is
just a matter of adding output ports and peak subscriptions.

### 6.5 What's the minimum viable UI in terms of LOC?

Estimate based on the reference examples:

- eg-sampler/sampler_ui.c is 374 lines. It does pugl setup, Cairo
  drawing of a waveform, file open via `ui:requestValue`, MIDI note-on
  via atom forge, peak reception via atom forge. Lots of atom machinery
  we do not need for OTT.
- eg-scope/examploscope_ui.c is 714 lines. It does pugl setup, Cairo
  drawing of a 2-channel oscilloscope, mouse drag to change amp/speed,
  atom-based message passing. The mouse drag pattern is directly
  reusable.

For OTT, the minimum viable UI (14 widgets: 11 knobs, 2 vertical
sliders, 1 bypass button, plus pugl setup, Cairo drawing, mouse
handling, port_event, write_function calls, idle, extension_data,
lv2ui_descriptor) is approximately:

- pugl + LV2 boilerplate (instantiate, cleanup, idle, extension_data,
  lv2ui_descriptor, on_event switch): ~150 lines, mostly copied from
  eg-sampler.
- Widget table and hit testing (find_knob_at, find_slider_at,
  find_button_at): ~50 lines.
- Mouse drag handling (BUTTON_PRESS, MOTION, BUTTON_RELEASE with Shift
  for fine, touch notification): ~80 lines.
- Cairo drawing (background, 11 knobs, 2 sliders, 1 bypass button, all
  labels and value text): ~200 lines.
- port_event handler (update cached values, request redraw): ~30 lines.
- Value formatting (dB, percent, bypass): ~40 lines.

Total: approximately 550 lines for a complete, working UI. A really
stripped-down version (just the 4 top-row knobs and a bypass button, no
band sliders, no labels, no value text) could fit in 300 lines, but it
would not look like OTT and would not be useful enough to ship.

For comparison, the entire current OTT DSP and LV2 wrapper is about 600
lines of C (ott_dsp.c 440, ott_lv2.c 195). The UI will be roughly the
same size as the plugin itself. That is normal: a knob UI is mostly
drawing and event handling code, and there are 14 widgets to handle.

## 7. Implementation plan

Based on the above, the recommended path for adding a UI to OTT:

1. Vendor or pkg-config pugl. Decision: start with pkg-config
   (`pugl-0`, `pugl-cairo-0`, `cairo`), document the Debian/Arch/Fedora
   package names in the README, and revisit vendoring if it becomes a
   friction point.
2. Add `lv2/ui.h` to the repo (minimal, like the existing `lv2/lv2.h`).
3. Write `ott_ui.c` modelled on eg-sampler/sampler_ui.c, with the OTT
   widget table from section 4.3, the Cairo drawing from section 3, and
   the mouse handling from section 3.3.
4. Update `manifest.ttl` and `ott.ttl` per section 5.2 and 5.3.
5. Update the Makefile per section 5.4.
6. Test in Jalv (simplest host, no DAW setup needed): `jalv gtk
   https://github.com/Rui-727/OTT` should show the UI. Then Carla, then
   Ardour, then Qtractor, then Zrythm.
7. Run `lv2lint https://github.com/Rui-727/OTT` and fix any warnings
   (likely: add explicit `ui:portNotification` declarations, check
   feature usage, check binary naming).
8. Update README.md to mention the UI and the new `libpugl-dev`
   build dependency.

The UI is a separate translation unit and a separate .so. The existing
DSP and LV2 wrapper do not change. If the UI fails to build (missing
pugl), the plugin still builds and works with the host's generic
parameter sheet. This is the same resilience pattern as the LV2
reference examples, where the UI is built only if pugl is found
[lv2-examples-sampler-meson].

## Sources

- [lv2-ui-spec] LV2 UI extension specification. http://lv2plug.in/ns/extensions/ui
- [ui-h] LV2 UI header, `ui.h`. https://github.com/harryhaaren/lv2/blob/master/lv2/lv2plug.in/ns/extensions/ui/ui.h (and raw mirror)
- [lv2-instance-access] LV2 Instance Access extension. http://lv2plug.in/ns/ext/instance-access
- [lv2-data-access] LV2 Data Access extension. http://lv2plug.in/ns/ext/data-access
- [pugl-docs] Pugl 0.5.7 documentation. https://lv2.gitlab.io/pugl/c/singlehtml
- [pugl-repo] lv2/pugl repository. https://github.com/lv2/pugl
- [lv2-examples-eg-sampler-ui] eg-sampler.lv2/sampler_ui.c. https://github.com/lv2/lv2-examples/blob/main/plugins/eg-sampler.lv2/sampler_ui.c
- [lv2-examples-eg-sampler-manifest] eg-sampler.lv2/manifest.ttl.in. https://github.com/lv2/lv2-examples/blob/main/plugins/eg-sampler.lv2/manifest.ttl.in
- [lv2-examples-eg-scope-ui] eg-scope.lv2/examploscope_ui.c. https://github.com/lv2/lv2-examples/blob/main/plugins/eg-scope.lv2/examploscope_ui.c
- [lv2-examples-sampler-meson] eg-sampler.lv2/meson.build. https://github.com/lv2/lv2-examples/blob/main/plugins/eg-sampler.lv2/meson.build
- [lv2-examples-plugins-meson] plugins/meson.build (defines `ui_type` per platform). https://github.com/lv2/lv2-examples/blob/main/plugins/meson.build
- [cairo-tutorial] Cairo tutorial. https://www.cairographics.org/tutorial
- [edmprod-ott] "OTT Plugin: Why Does It Sound SO Good?" edmprod. https://www.edmprod.com/ott-plugin
- [production-expert-ott] "Free Plugin: Xfer Records OTT" Production Expert. https://www.production-expert.com/production-expert-1/free-plugin-xfer-records-ott
- [virtualplaying-ott] "Multiband compressor - Xfer OTT" Virtual Playing Orchestra. https://virtualplaying.com/multiband-compressor-xfer-ott
- [ott-ttl] ott.ttl in this repository. Existing port declarations for the OTT plugin.
- [ott-lv2-c] ott_lv2.c in this repository. Existing LV2 wrapper.
- [ott-makefile] Makefile in this repository. Existing build rules.
- [ott-manifest] manifest.ttl in this repository. Existing plugin manifest.
- [ott-readme] README.md in this repository. Existing build and install instructions.
- [ott-lv2-h] lv2/lv2.h in this repository. Minimal bundled LV2 core header.
- [reddit-toolkit] r/linuxaudio "Which UI toolkit for lv2 plugin development?" thread. https://www.reddit.com/r/linuxaudio/comments/1ktjycf/which_ui_toolkit_for_lv2_plugin_development
- [bwidgets] BWidgets: Widget toolkit based on Cairo and Pugl. https://github.com/sjaehn/BWidgets
- [xuidesigner] XUiDesigner: WYSIWYG LV2 GUI/plugin creator. https://github.com/brummer10/XUiDesigner
- [suil-mention-in-zrythm] Zrythm source referencing suil and instance/data access. https://gitlab.zrythm.org/zrythm/zrythm/-/blob/v1.0.0-alpha.19.0.1/inc/plugins/lv2/lv2_worker.h
