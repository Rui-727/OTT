/*
 * Minimal LV2 UI extension header.
 *
 * Includes only the definitions needed by this plugin's UI: the
 * LV2UI_Descriptor struct, the LV2UI_Handle / LV2UI_Controller /
 * LV2UI_Widget typedefs, the LV2UI_Write_Function prototype, the
 * LV2UI_Feature struct, the LV2UI_Port_Index / LV2UI_Touch /
 * LV2UI_Resize / LV2UI_Idle_Interface / LV2UI_Show_Interface helper
 * structs, the LV2_UI__* URI macros, and the lv2ui_descriptor() entry
 * point. This lets the UI build without the full lv2-dev package
 * installed. If the real lv2/lv2plug.in/ns/extensions/ui/ui.h is
 * present on the system, prefer it via the build's include path.
 *
 * This is a subset of the public-domain / MIT LV2 specification by
 * David Robillard. See https://lv2plug.in/ for the full spec.
 */

#ifndef LV2_UI_H_INCLUDED
#define LV2_UI_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "lv2.h"  /* for LV2_Feature */

#define LV2_UI_URI "http://lv2plug.in/ns/extensions/ui"
#define LV2_UI_PREFIX LV2_UI_URI "#"

#define LV2_UI__UI              LV2_UI_PREFIX "UI"
#define LV2_UI__GtkUI           LV2_UI_PREFIX "GtkUI"
#define LV2_UI__Gtk3UI          LV2_UI_PREFIX "Gtk3UI"
#define LV2_UI__Qt4UI           LV2_UI_PREFIX "Qt4UI"
#define LV2_UI__Qt5UI           LV2_UI_PREFIX "Qt5UI"
#define LV2_UI__X11UI           LV2_UI_PREFIX "X11UI"
#define LV2_UI__WindowsUI       LV2_UI_PREFIX "WindowsUI"
#define LV2_UI__CocoaUI         LV2_UI_PREFIX "CocoaUI"

#define LV2_UI__ui              LV2_UI_PREFIX "ui"
#define LV2_UI__binary          LV2_UI_PREFIX "binary"
#define LV2_UI__widget          LV2_UI_PREFIX "widget"

#define LV2_UI__parent          LV2_UI_PREFIX "parent"
#define LV2_UI__resize          LV2_UI_PREFIX "resize"
#define LV2_UI__touch           LV2_UI_PREFIX "touch"
#define LV2_UI__idleInterface   LV2_UI_PREFIX "idleInterface"
#define LV2_UI__showInterface   LV2_UI_PREFIX "showInterface"
#define LV2_UI__portMap         LV2_UI_PREFIX "portMap"
#define LV2_UI__portSubscribe   LV2_UI_PREFIX "portSubscribe"
#define LV2_UI__portNotification LV2_UI_PREFIX "portNotification"
#define LV2_UI__portIndex       LV2_UI_PREFIX "portIndex"
#define LV2_UI__protocol        LV2_UI_PREFIX "protocol"
#define LV2_UI__floatProtocol   LV2_UI_PREFIX "floatProtocol"
#define LV2_UI__peakProtocol    LV2_UI_PREFIX "peakProtocol"
#define LV2_UI__fixedSize       LV2_UI_PREFIX "fixedSize"
#define LV2_UI__noUserResize    LV2_UI_PREFIX "noUserResize"
#define LV2_UI__scaleFactor     LV2_UI_PREFIX "scaleFactor"
#define LV2_UI__backgroundColor LV2_UI_PREFIX "backgroundColor"
#define LV2_UI__foregroundColor LV2_UI_PREFIX "foregroundColor"
#define LV2_UI__windowTitle     LV2_UI_PREFIX "windowTitle"
#define LV2_UI__updateRate      LV2_UI_PREFIX "updateRate"

/* Opaque UI instance handle (analogous to LV2_Handle). */
typedef void *LV2UI_Handle;

/* Opaque host controller pointer. Passed back as the first argument to
 * write_function. The UI must not interpret this pointer. */
typedef void *LV2UI_Controller;

/* Native window/widget handle handed back to the host. For an X11UI this is
 * the X11 Window ID (cast through the pointer, not through a pointer-to). */
typedef void *LV2UI_Widget;

/* Feature handle (opaque host pointer for feature-specific data). */
typedef void *LV2UI_Feature_Handle;

/* UI to plugin: write a value to a port. The host routes this call to the
 * plugin's control port buffer. format==0 means ui:floatProtocol (buffer
 * points at a single float). */
typedef void (*LV2UI_Write_Function)(LV2UI_Controller controller,
                                     uint32_t         port_index,
                                     uint32_t         buffer_size,
                                     uint32_t         port_protocol,
                                     const void      *buffer);

/* Host feature entry, same shape as LV2_Feature but kept separate for
 * clarity. The UI walks the features array passed to instantiate() looking
 * for the URIs it cares about. */
typedef struct _LV2UI_Feature {
    const char *URI;
    void       *data;
} LV2UI_Feature;

/* Optional host feature: notify the host that a control was grabbed or
 * released so the host can suspend automation on it. */
typedef struct _LV2UI_Touch {
    LV2UI_Feature_Handle handle;
    void (*touch)(LV2UI_Feature_Handle handle,
                  uint32_t              port_index,
                  bool                  grabbed);
} LV2UI_Touch;

/* Optional host feature or UI extension data: request or be asked to
 * resize the UI. */
typedef struct _LV2UI_Resize {
    LV2UI_Feature_Handle handle;
    int (*ui_resize)(LV2UI_Feature_Handle handle, int width, int height);
} LV2UI_Resize;

/* Optional host feature: map port symbol -> port index. */
typedef struct _LV2UI_Port_Index {
    LV2UI_Feature_Handle handle;
    uint32_t (*port_index)(LV2UI_Feature_Handle handle, const char *symbol);
} LV2UI_Port_Index;

/* UI extension data: drive the UI's event loop from the host's UI thread. */
typedef struct _LV2UI_Idle_Interface {
    int (*idle)(LV2UI_Handle ui);
} LV2UI_Idle_Interface;

/* UI extension data: fallback show/hide for hosts that cannot embed the
 * widget. MUST be accompanied by idleInterface. */
typedef struct _LV2UI_Show_Interface {
    int (*show)(LV2UI_Handle ui);
    int (*hide)(LV2UI_Handle ui);
} LV2UI_Show_Interface;

/* UI descriptor. Returned by lv2ui_descriptor(). */
typedef struct _LV2UI_Descriptor {
    /* Globally unique URI for this UI. */
    const char *URI;

    /* Create a new UI instance. Returns NULL on failure. On success the UI
     * writes its native window ID through *widget (for an X11UI this is the
     * X11 Window ID, cast through LV2UI_Widget). */
    LV2UI_Handle (*instantiate)(const struct _LV2UI_Descriptor *descriptor,
                                const char                     *plugin_uri,
                                const char                     *bundle_path,
                                LV2UI_Write_Function            write_function,
                                LV2UI_Controller                controller,
                                LV2UI_Widget                   *widget,
                                const LV2_Feature *const       *features);

    /* Destroy a UI instance created by instantiate(). */
    void (*cleanup)(LV2UI_Handle ui);

    /* Host to UI: a port value changed. format==0 means ui:floatProtocol. */
    void (*port_event)(LV2UI_Handle ui,
                       uint32_t      port_index,
                       uint32_t      buffer_size,
                       uint32_t      format,
                       const void   *buffer);

    /* Return extension data by URI, or NULL if unsupported. */
    const void *(*extension_data)(const char *uri);
} LV2UI_Descriptor;

/* UI libraries export this. The host calls it with index 0, 1, 2, ... until
 * it returns NULL. */
#define LV2UI_SYMBOL_EXPORT __attribute__((visibility("default")))
const LV2UI_Descriptor *lv2ui_descriptor(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* LV2_UI_H_INCLUDED */
