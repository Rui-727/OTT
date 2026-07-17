/* ott_lv2.c - LV2 wrapper around the OTT DSP core.
 *
 * URI: https://github.com/Rui-727/OTT
 *
 * The wrapper is intentionally thin: instantiate() allocates two ott_dsp_t
 * instances (left, right), connect_port() stores port pointers, activate()
 * resets the DSP, run() reads control ports and calls ott_dsp_process_stereo.
 * No allocation or system calls happen inside run().
 */

#include "ott_dsp.h"
#include "lv2/lv2.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OTT_URI "https://github.com/Rui-727/OTT"

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
    OTT_PORT_IN_L,
    OTT_PORT_IN_R,
    OTT_PORT_OUT_L,
    OTT_PORT_OUT_R,
    OTT_PORT_BAND1_METER,   /* output, dB: net gain applied to the low band  */
    OTT_PORT_BAND2_METER,   /* output, dB: net gain applied to the mid band  */
    OTT_PORT_BAND3_METER,   /* output, dB: net gain applied to the high band */
    OTT_PORT_COUNT
};

typedef struct {
    const float *depth;
    const float *time;
    const float *upward;
    const float *downward;
    const float *input_gain;
    const float *output_gain;
    const float *band1_thresh;
    const float *band2_thresh;
    const float *band3_thresh;
    const float *band1_gain;
    const float *band2_gain;
    const float *band3_gain;
    const float *bypass;
    const float *in_l;
    const float *in_r;
    float *out_l;
    float *out_r;

    /* Output control ports: net gain applied per band on the last sample,
     * in dB, for the UI's live meter display. */
    float *band1_meter;
    float *band2_meter;
    float *band3_meter;

    ott_dsp_t *left;
    ott_dsp_t *right;
    double sample_rate;
} Ott;

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
                              double rate,
                              const char *bundle_path,
                              const LV2_Feature *const *features) {
    (void)descriptor; (void)bundle_path; (void)features;
    Ott *self = (Ott *)calloc(1, sizeof(*self));
    if (!self) return NULL;
    self->sample_rate = rate;
    self->left = ott_dsp_new(rate);
    self->right = ott_dsp_new(rate);
    if (!self->left || !self->right) {
        ott_dsp_free(self->left);
        ott_dsp_free(self->right);
        free(self);
        return NULL;
    }
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data) {
    Ott *self = (Ott *)instance;
    switch (port) {
        case OTT_PORT_DEPTH:        self->depth         = (const float *)data; break;
        case OTT_PORT_TIME:         self->time          = (const float *)data; break;
        case OTT_PORT_UPWARD:       self->upward        = (const float *)data; break;
        case OTT_PORT_DOWNWARD:     self->downward      = (const float *)data; break;
        case OTT_PORT_INPUT_GAIN:   self->input_gain    = (const float *)data; break;
        case OTT_PORT_OUTPUT_GAIN:  self->output_gain   = (const float *)data; break;
        case OTT_PORT_BAND1_THRESH: self->band1_thresh  = (const float *)data; break;
        case OTT_PORT_BAND2_THRESH: self->band2_thresh  = (const float *)data; break;
        case OTT_PORT_BAND3_THRESH: self->band3_thresh  = (const float *)data; break;
        case OTT_PORT_BAND1_GAIN:   self->band1_gain    = (const float *)data; break;
        case OTT_PORT_BAND2_GAIN:   self->band2_gain    = (const float *)data; break;
        case OTT_PORT_BAND3_GAIN:   self->band3_gain    = (const float *)data; break;
        case OTT_PORT_BYPASS:       self->bypass        = (const float *)data; break;
        case OTT_PORT_IN_L:         self->in_l          = (const float *)data; break;
        case OTT_PORT_IN_R:         self->in_r          = (const float *)data; break;
        case OTT_PORT_OUT_L:        self->out_l         = (float *)data; break;
        case OTT_PORT_OUT_R:        self->out_r         = (float *)data; break;
        case OTT_PORT_BAND1_METER:  self->band1_meter   = (float *)data; break;
        case OTT_PORT_BAND2_METER:  self->band2_meter   = (float *)data; break;
        case OTT_PORT_BAND3_METER:  self->band3_meter   = (float *)data; break;
        default: break;
    }
}

static void activate(LV2_Handle instance) {
    Ott *self = (Ott *)instance;
    ott_dsp_reset(self->left);
    ott_dsp_reset(self->right);
}

static void deactivate(LV2_Handle instance) {
    (void)instance;
}

/* Helper: read a control port safely (some hosts may connect a port to NULL
 * briefly during setup; fall back to the provided default). */
static float read_port(const float *port, float fallback) {
    return port ? *port : fallback;
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    Ott *self = (Ott *)instance;
    if (!self->in_l || !self->out_l) {
        return;
    }

    ott_params_t p;
    p.depth        = read_port(self->depth,        1.0f);
    p.upward       = read_port(self->upward,       1.0f);
    p.downward     = read_port(self->downward,     1.0f);
    p.input_gain   = read_port(self->input_gain,   5.2f);
    p.output_gain  = read_port(self->output_gain,  0.0f);
    p.band_thresh[0] = read_port(self->band1_thresh, -30.0f);
    p.band_thresh[1] = read_port(self->band2_thresh, -30.0f);
    p.band_thresh[2] = read_port(self->band3_thresh, -30.0f);
    p.band_gain[0]   = read_port(self->band1_gain,   10.3f);
    p.band_gain[1]   = read_port(self->band2_gain,    5.7f);
    p.band_gain[2]   = read_port(self->band3_gain,   10.3f);
    p.bypass       = (int)read_port(self->bypass, 0.0f);

    /* Map the Time control (0..1) to time_scale (0.1..2.0) linearly. */
    float t = read_port(self->time, 0.5f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    p.time_scale = 0.1f + 1.9f * t;

    ott_dsp_set_params(self->left,  &p);
    ott_dsp_set_params(self->right, &p);

    /* If the host only connected the left audio ports (mono), fall back to
     * mono processing on the left instance. */
    if (self->in_r && self->out_r) {
        ott_dsp_process_stereo(self->left, self->right,
                               self->in_l, self->in_r,
                               self->out_l, self->out_r, n_samples);
    } else {
        ott_dsp_process_block(self->left,
                              self->in_l, self->out_l, n_samples);
        /* If a right output exists but no right input, copy the left result. */
        if (self->out_r && self->out_r != self->out_l) {
            memcpy(self->out_r, self->out_l, n_samples * sizeof(float));
        }
    }

    /* Publish the per-band meter for the UI. left->band_net_gr_db reflects
     * the last sample processed above, whether that came from the stereo
     * (linked) or mono path. bands[0]=low, [1]=mid, [2]=high. */
    float meter[3];
    ott_dsp_get_band_meter(self->left, meter);
    if (self->band1_meter) *self->band1_meter = meter[0];
    if (self->band2_meter) *self->band2_meter = meter[1];
    if (self->band3_meter) *self->band3_meter = meter[2];
}

static void cleanup(LV2_Handle instance) {
    Ott *self = (Ott *)instance;
    ott_dsp_free(self->left);
    ott_dsp_free(self->right);
    free(self);
}

static const void *extension_data(const char *uri) {
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    OTT_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index) {
    return index == 0 ? &descriptor : NULL;
}
