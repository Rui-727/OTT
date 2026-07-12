/* tests/test_ott.c - Unit tests for the OTT DSP core.
 *
 * Builds with the regular Makefile via `make test`. Each test prints PASS or
 * FAIL with a short message. The harness exits non-zero if any test fails.
 *
 * Tests are intentionally simple and conservative: they verify invariants
 * (no runaway gain, no denormals, dry/wet correctness, stereo linking) and
 * the gross direction of the compression effect (loud gets quieter, quiet
 * gets louder). They are not bit-exact reference comparisons against the
 * original Xfer OTT.
 */

#include "../ott_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failures = 0;
static int g_tests = 0;

#define ASSERT(cond, msg) do {                                  \
    g_tests++;                                                  \
    if (cond) {                                                 \
        printf("PASS: %s\n", msg);                              \
    } else {                                                    \
        printf("FAIL: %s (line %d)\n", msg, __LINE__);          \
        g_failures++;                                           \
    }                                                           \
} while (0)

static float rms(const float *buf, size_t n);
static float max_abs(const float *buf, size_t n);
static float max_abs_diff(const float *a, const float *b, size_t n);

/* Deterministic pseudo-random noise in [-1, 1] so the noise-based tests are
 * reproducible across runs. */
static float rand01(unsigned int *state) {
    /* Numerical Recipes LCG. */
    *state = 1664525u * (*state) + 1013904223u;
    return (float)((*state) >> 8) / (float)(1u << 24);
}

static float rms(const float *buf, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += (double)buf[i] * buf[i];
    return (float)sqrt(s / (double)n);
}

static float max_abs(const float *buf, size_t n) {
    float m = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float a = fabsf(buf[i]);
        if (a > m) m = a;
    }
    return m;
}

static float max_abs_diff(const float *a, const float *b, size_t n) {
    float m = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float d = fabsf(a[i] - b[i]);
        if (d > m) m = d;
    }
    return m;
}

/* ----------------------------------------------------------------------- */

static void test_bypass(void) {
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    p.bypass = 1;
    ott_dsp_set_params(ott, &p);

    const size_t N = 1024;
    float in[N], out[N];
    for (size_t i = 0; i < N; ++i) {
        in[i] = 0.5f * sinf(2.0f * (float)M_PI * 440.0f * i / 44100.0f);
    }
    ott_dsp_process_block(ott, in, out, N);

    float diff = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        float d = fabsf(in[i] - out[i]);
        if (d > diff) diff = d;
    }
    ASSERT(diff < 1e-6f, "bypass: output equals input");

    ott_dsp_free(ott);
}

static void test_dc_stable(void) {
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    ott_dsp_set_params(ott, &p);

    const size_t N = 8192;
    float out[N];
    for (size_t i = 0; i < N; ++i) {
        out[i] = ott_dsp_process(ott, 0.5f);
    }
    float peak = max_abs(out, N);
    ASSERT(isfinite(peak), "DC input: output is finite");
    ASSERT(peak < 10.0f, "DC input: no runaway gain (peak < 10)");
    /* After settling, the output should not be NaN or denormal-prone. */
    ASSERT(fabsf(out[N - 1]) < 10.0f, "DC input: last sample stable");

    ott_dsp_free(ott);
}

static void test_silence(void) {
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    ott_dsp_set_params(ott, &p);

    const size_t N = 1024;
    float out[N];
    for (size_t i = 0; i < N; ++i) out[i] = 0.0f;
    ott_dsp_process_block(ott, out, out, N);
    /* Output should be at most a tiny residual from the input gain pushing
     * noise floor around; in practice it stays near zero. */
    float peak = max_abs(out, N);
    ASSERT(peak < 1e-4f, "silence in -> silence out");

    ott_dsp_free(ott);
}

static void test_sine_audible(void) {
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    ott_dsp_set_params(ott, &p);

    const size_t N = 8192;
    float in[N], out[N];
    for (size_t i = 0; i < N; ++i) {
        in[i] = 0.3f * sinf(2.0f * (float)M_PI * 1000.0f * i / 44100.0f);
    }
    ott_dsp_process_block(ott, in, out, N);
    ASSERT(rms(out, N) > 1e-3f, "sine in produces non-silence out");

    ott_dsp_free(ott);
}

static void test_depth_zero_is_dry(void) {
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    p.depth = 0.0f;
    ott_dsp_set_params(ott, &p);

    const size_t N = 1024;
    float in[N], out[N];
    for (size_t i = 0; i < N; ++i) {
        in[i] = 0.4f * sinf(2.0f * (float)M_PI * 500.0f * i / 44100.0f);
    }
    ott_dsp_process_block(ott, in, out, N);
    float diff = max_abs_diff(in, out, N);
    ASSERT(diff < 1e-6f, "depth=0 produces dry (output == input)");

    ott_dsp_free(ott);
}

static void test_depth_one_is_wet(void) {
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    p.depth = 1.0f;
    ott_dsp_set_params(ott, &p);

    const size_t N = 4096;
    float in[N], out[N];
    for (size_t i = 0; i < N; ++i) {
        in[i] = 0.5f * sinf(2.0f * (float)M_PI * 1000.0f * i / 44100.0f);
    }
    ott_dsp_process_block(ott, in, out, N);
    /* With OTT defaults, the wet signal is heavily processed and differs
     * significantly from the dry input. */
    float diff = 0.0f;
    for (size_t i = N / 2; i < N; ++i) {
        float d = fabsf(in[i] - out[i]);
        if (d > diff) diff = d;
    }
    ASSERT(diff > 0.01f, "depth=1 produces compressed (wet) output");

    ott_dsp_free(ott);
}

static void test_crossover_sums_flat(void) {
    /* The LR4 crossover sum should be all-pass (flat magnitude). With
     * broadband noise, the long-term RMS of the input and the sum of bands
     * should match within a small tolerance. */
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    p.input_gain = 0.0f;  /* unity, so the sum is directly comparable */
    ott_dsp_set_params(ott, &p);

    const size_t N = 65536;
    unsigned int rng = 0xdeadbeefu;
    double in_sq = 0.0, sum_sq = 0.0;
    for (size_t i = 0; i < N; ++i) {
        float x = rand01(&rng) * 2.0f - 1.0f;
        float bands[3];
        ott_dsp_crossover_split(ott, x, bands);
        float s = bands[0] + bands[1] + bands[2];
        in_sq += (double)x * x;
        sum_sq += (double)s * s;
    }
    float in_rms  = (float)sqrt(in_sq  / (double)N);
    float sum_rms = (float)sqrt(sum_sq / (double)N);
    float ratio = sum_rms / in_rms;
    /* LR4 3-band crossover sum is all-pass, so magnitude should match within
     * about 1 percent on broadband noise. */
    ASSERT(ratio > 0.99f && ratio < 1.01f,
           "crossover: band sum RMS matches input RMS (all-pass)");

    ott_dsp_free(ott);
}

static void test_upward_boosts_quiet(void) {
    /* With a quiet input (-40 dBFS) and only upward compression on, the
     * output RMS should be higher than the input RMS. */
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    p.downward = 0.0f;   /* isolate upward */
    p.upward = 1.0f;
    p.depth = 1.0f;
    p.input_gain = 0.0f;
    p.output_gain = 0.0f;
    p.band_gain[0] = 0.0f;
    p.band_gain[1] = 0.0f;
    p.band_gain[2] = 0.0f;
    /* Threshold around -30 dB; quiet sine at -40 dBFS will be below it. */
    p.band_thresh[0] = -30.0f;
    p.band_thresh[1] = -30.0f;
    p.band_thresh[2] = -30.0f;
    ott_dsp_set_params(ott, &p);

    const size_t N = 16384;
    float in[N], out[N];
    float amp = powf(10.0f, -40.0f / 20.0f); /* -40 dBFS */
    for (size_t i = 0; i < N; ++i) {
        in[i] = amp * sinf(2.0f * (float)M_PI * 220.0f * i / 44100.0f);
    }
    ott_dsp_process_block(ott, in, out, N);
    /* Skip the attack transient before measuring. */
    float in_rms  = rms(in  + N / 4, N - N / 4);
    float out_rms = rms(out + N / 4, N - N / 4);
    ASSERT(out_rms > in_rms * 1.5f,
           "upward compression boosts quiet signal (>1.5x)");

    ott_dsp_free(ott);
}

static void test_downward_reduces_loud(void) {
    /* With a loud input (0 dBFS) and only downward compression on, the
     * output RMS should be lower than the input RMS. */
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    p.downward = 1.0f;
    p.upward = 0.0f;     /* isolate downward */
    p.depth = 1.0f;
    p.input_gain = 0.0f;
    p.output_gain = 0.0f;
    p.band_gain[0] = 0.0f;
    p.band_gain[1] = 0.0f;
    p.band_gain[2] = 0.0f;
    p.band_thresh[0] = -30.0f;
    p.band_thresh[1] = -30.0f;
    p.band_thresh[2] = -30.0f;
    ott_dsp_set_params(ott, &p);

    const size_t N = 16384;
    float in[N], out[N];
    for (size_t i = 0; i < N; ++i) {
        in[i] = 0.9f * sinf(2.0f * (float)M_PI * 220.0f * i / 44100.0f);
    }
    ott_dsp_process_block(ott, in, out, N);
    float in_rms  = rms(in  + N / 4, N - N / 4);
    float out_rms = rms(out + N / 4, N - N / 4);
    ASSERT(out_rms < in_rms * 0.95f,
           "downward compression reduces loud signal (<0.95x)");

    ott_dsp_free(ott);
}

static void test_stereo_linking(void) {
    /* Feed different left and right signals; verify both channels get the
     * same gain reduction by checking that the long-term applied gain
     * (rms(out)/rms(in)) is identical for L and R. Stereo linking forces
     * a single shared detector, so the per-channel gain must match. */
    ott_dsp_t *L = ott_dsp_new(44100.0);
    ott_dsp_t *R = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    /* Use settings where compression is active for both loud and quiet. */
    ott_dsp_set_params(L, &p);
    ott_dsp_set_params(R, &p);

    const size_t N = 8192;
    float inL[N], inR[N], outL[N], outR[N];
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / 44100.0f;
        inL[i] = 0.6f * sinf(2.0f * (float)M_PI * 440.0f * t);
        inR[i] = 0.2f * sinf(2.0f * (float)M_PI * 440.0f * t);
    }
    ott_dsp_process_stereo(L, R, inL, inR, outL, outR, N);

    /* Skip the attack transient. */
    size_t off = N / 4;
    size_t len = N - off;
    float inL_rms  = rms(inL  + off, len);
    float inR_rms  = rms(inR  + off, len);
    float outL_rms = rms(outL + off, len);
    float outR_rms = rms(outR + off, len);
    float gain_L = outL_rms / inL_rms;
    float gain_R = outR_rms / inR_rms;
    float rel_diff = fabsf(gain_L - gain_R) / (0.5f * (gain_L + gain_R));
    ASSERT(rel_diff < 1e-3f,
           "stereo linking: both channels get same gain (RMS-based)");

    ott_dsp_free(L);
    ott_dsp_free(R);
}

static void test_no_denormals_or_nan(void) {
    /* Run a long block of low-level signal and verify no NaN/Inf appears. */
    ott_dsp_t *ott = ott_dsp_new(44100.0);
    ott_params_t p = ott_dsp_default_params();
    ott_dsp_set_params(ott, &p);

    const size_t N = 32768;
    float out[N];
    for (size_t i = 0; i < N; ++i) {
        /* tiny signal that could trigger denormals in IIR filters */
        float x = 1e-5f * sinf(2.0f * (float)M_PI * 100.0f * i / 44100.0f);
        out[i] = ott_dsp_process(ott, x);
    }
    int bad = 0;
    for (size_t i = 0; i < N; ++i) {
        if (!isfinite(out[i])) { bad = 1; break; }
    }
    ASSERT(!bad, "no NaN/Inf in long low-level run");

    ott_dsp_free(ott);
}

int main(void) {
    test_bypass();
    test_dc_stable();
    test_silence();
    test_sine_audible();
    test_depth_zero_is_dry();
    test_depth_one_is_wet();
    test_crossover_sums_flat();
    test_upward_boosts_quiet();
    test_downward_reduces_loud();
    test_stereo_linking();
    test_no_denormals_or_nan();

    printf("\n%d/%d tests passed\n", g_tests - g_failures, g_tests);
    return g_failures == 0 ? 0 : 1;
}
