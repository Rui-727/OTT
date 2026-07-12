/*
 * Minimal LV2 core header.
 *
 * Includes only the definitions needed by this plugin: the LV2_Descriptor
 * struct, LV2_Handle, LV2_Feature, the lv2_descriptor() entry point, and the
 * LV2_CORE__hardRTCapable feature URI. This lets the plugin build without the
 * full lv2-dev package installed. If the real lv2/core/lv2.h is present on
 * the system, prefer it by including it first via the build's include path.
 *
 * This is a subset of the public-domain LV2 specification by David Robillard.
 * See https://lv2plug.in/ for the full spec.
 */

#ifndef LV2_H_INCLUDED
#define LV2_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LV2_CORE_URI "http://lv2plug.in/ns/lv2core"
#define LV2_CORE_PREFIX LV2_CORE_URI "#"

#define LV2_CORE__hardRTCapable LV2_CORE_PREFIX "hardRTCapable"

/* Plugin handle (opaque pointer to instance data). */
typedef void *LV2_Handle;

/* A host-provided feature: URI + data pointer. Forward-declared so the
 * LV2_Descriptor's instantiate() signature can reference it. */
typedef struct _LV2_Feature {
    const char *URI;
    void *data;
} LV2_Feature;

/* Plugin descriptor. Returned by lv2_descriptor(). */
typedef struct _LV2_Descriptor {
    /* A globally unique URI for this plugin. */
    const char *URI;

    /* Create a new instance. Returns NULL on failure. */
    LV2_Handle (*instantiate)(const struct _LV2_Descriptor *descriptor,
                              double sample_rate,
                              const char *bundle_path,
                              const LV2_Feature *const *features);

    /* Connect a port to a data location. */
    void (*connect_port)(LV2_Handle instance, uint32_t port, void *data);

    /* Initialise the plugin after instantiation and before run(). */
    void (*activate)(LV2_Handle instance);

    /* Process a block of samples. */
    void (*run)(LV2_Handle instance, uint32_t n_samples);

    /* Counterpart to activate(). */
    void (*deactivate)(LV2_Handle instance);

    /* Free an instance. */
    void (*cleanup)(LV2_Handle instance);

    /* Return extension data by URI, or NULL if unsupported. */
    const void *(*extension_data)(const char *uri);
} LV2_Descriptor;

/* Plugin libraries export this. The host calls it with index 0, 1, 2, ...
 * until it returns NULL. */
#define LV2_SYMBOL_EXPORT __attribute__((visibility("default")))
const LV2_Descriptor *lv2_descriptor(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* LV2_H_INCLUDED */
