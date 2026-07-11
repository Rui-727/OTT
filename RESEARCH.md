# OTT DSP and LV2 Plugin Research

Research notes for implementing Xfer Records' OTT as a Linux LV2 plugin in C.
Covers the exact OTT settings, the LV2 plugin format, and the DSP building
blocks (crossover, downward + upward compression, stereo linking).

Sources are inline. Where multiple sources disagree, the disagreement is
called out and a recommended value is picked.

## 1. OTT plugin specifics

### 1.1 What OTT is

OTT stands for "Over The Top". It started life as a factory preset named OTT
inside Ableton Live's Multiband Dynamics device. Steve Duda at Xfer Records
then shipped a free standalone VST/AU plugin called OTT that replicates the
preset for non-Ableton users [edmprod], [pml], [kvr-release]. Both versions
are 3-band multiband compressors that apply downward and upward compression
simultaneously in each band, producing a dense, smashed sound at full wet and
a subtle shimmer at low wet.

The standalone Xfer OTT plugin simplifies the Ableton Multiband Dynamics UI to
a fixed set of controls:

- Depth (wet/dry mix, 0 to 100 percent)
- Time (scales attack and release for all 3 bands, 0 to 100 percent)
- Input gain
- Output gain
- 3 band threshold sliders (vertical, fixed crossover)
- 3 band output gain knobs (L, M, H)
- Upward amount knob (scales the upward compression ratio, 0 to 100 percent)
- Downward amount knob (scales the downward compression ratio, 0 to 100 percent)
- Per-band bypass (click the T/B/A labels)
- CTRL+click resets a parameter [edmprod]

The crossover frequencies on the Xfer OTT plugin are not user-adjustable.
The thresholds are per band.

### 1.2 Crossover frequencies

This is where the popular guesses and the actual plugin disagree.

Common internet guesses: 200 Hz and 2 kHz, or 120 Hz and 2.5 kHz.

The actual Xfer OTT plugin uses the same crossover points as the Ableton
Multiband Dynamics OTT preset, which edmprod confirms by reading the preset
directly [edmprod]:

- Low to Mid crossover: 88.3 Hz
- Mid to High crossover: 2.5 kHz (2500 Hz)

Quoted from edmprod: "The cutoff frequency for each band is the following:
Lows: below 88.3 Hz, Mids: between 88.3 Hz and 2.5 kHz, Highs: above 2.5 kHz.
Sadly, these frequencies are not adjustable."

The reverse-engineered `xtractedott` repo on GitHub uses 200 Hz and 2000 Hz
[xtractedott-filters], but its own README warns "This is pretty rough and
partly incorrect. I wasn't very knowledgeable when I wrote it. Double check
everything!!!" so the 88.3 Hz / 2.5 kHz values from edmprod should be
preferred.

Recommended values for our implementation:
- Low/Mid: 88.3 Hz
- Mid/High: 2500 Hz

These can be constants in the plugin. If we want to expose them as
parameters, we should still default to 88.3 / 2500.

### 1.3 Band structure and signal flow

OTT processes 3 bands in parallel, compresses each, then sums them back
together. The signal flow per channel is:

```
input
  |
  v
3-band crossover (LR4, 88.3 Hz and 2500 Hz)
  |       |       |
 low    mid     high
  |       |       |
  v       v       v
input gain (per band, fixed in Xfer OTT, +5.2 dB on Ableton preset)
  |       |       |
  v       v       v
downward compressor  (above threshold, ratio scaled by Downward knob)
  |       |       |
  v       v       v
upward compressor    (below threshold, ratio scaled by Upward knob)
  |       |       |
  v       v       v
output gain (per band, L/M/H knobs on Xfer OTT)
  |       |       |
  v       v       v
sum
  |
  v
dry/wet mix (Depth)
  |
  v
output gain
  |
  v
output
```

The downward and upward compressors run in series per band. Downward is
applied first, then upward on the downward-reduced signal. This is the order
Ableton Multiband Dynamics uses (above-band block first, then below-band
block), and Xfer OTT inherits it [edmprod].

The dry/wet mix at the end is critical to the OTT sound. At 100 percent it
is the full smash. At low percentages (10 to 30) the wet signal is layered
under the dry, producing a parallel-compression effect that adds detail
without killing transients.

### 1.4 The Ableton Multiband Dynamics OTT preset settings

These are the exact values from the Ableton factory preset, as documented by
edmprod [edmprod]:

- Low/Mid crossover: 88.3 Hz (default is 250 Hz)
- Mid/High crossover: 2.5 kHz (default)
- Per-band input gain: +5.2 dB on all 3 bands (drives signal into the
  compressors)
- Above (downward) ratios:
  - High band: infinity:1 (brickwall limiter)
  - Mid band: 1:66.7 (effectively a limiter)
  - Low band: 1:66.7 (effectively a limiter)
- Above (downward) thresholds: very low on all bands (signals almost always
  exceed threshold, so the compressor is always working)
- Below (upward) ratio: 4:1 on all 3 bands
- Below (upward) thresholds: very low on all 3 bands (signals almost always
  fall above threshold, so upward compression is always pulling quiet content
  up)
- Per-band output gain:
  - Low: +10.3 dB
  - Mid: +5.7 dB
  - High: +10.3 dB

The Ableton preset also uses RMS detection (slower, smoother) and soft knees,
per the Multiband Dynamics device's defaults [edmprod].

### 1.5 The Xfer OTT plugin mapping

Xfer OTT does not expose threshold, ratio, attack, and release directly. It
exposes 3 macro knobs (Depth, Time, Upward, Downward) plus per-band
thresholds and per-band output gains. Internally the macros scale the
underlying Ableton-style parameters [edmprod], [kvr-release], [pml]:

- Depth: 0 to 100 percent, linear dry/wet mix.
- Time: 0 to 100 percent. Scales the attack and release times of all 3 bands
  simultaneously. Lower values = faster attack and release. Higher values =
  slower attack and release. At 50 percent (default) the times are at their
  nominal values.
- Upward: 0 to 100 percent. Scales the upward compression ratio. At 100
  percent the upward ratio matches the Ableton preset (4:1). At 0 percent
  there is no upward compression.
- Downward: 0 to 100 percent. Scales the downward compression ratio. At 100
  percent the downward ratios match the Ableton preset (infinity:1 high,
  66.7:1 mid and low). At 0 percent there is no downward compression.
- Per-band threshold sliders: black vertical sliders. These set the
  threshold for both the upward and downward compressors in that band (a
  single threshold drives both above and below dynamics, exactly like
  Ableton Multiband Dynamics).
- Per-band output gain (L, M, H): post-compression makeup gain per band.
  Defaults to 0 dB. The Ableton preset's defaults are +10.3 / +5.7 / +10.3
  dB, but Xfer OTT's UI starts these at 0 dB and lets the user dial them in.

The `xtractedott` reverse-engineering effort extracted a parameter list and
scaling formulas from the original VST binary [xtractedott-params]:

- Parameter 0: Depth (0.0 to 1.0, default 0.5)
- Parameter 1: Time (0.0 to 1.0, default 0.5)
- Parameter 2: Upward Ratio (0.0 to 1.0, default 0.5, complex scaling)
- Parameter 3: Downward Ratio (0.0 to 1.0, default 0.5, complex scaling)
- Parameters 5 to 7: Low/Mid/High band controls
- Parameters 8 to 10: Low/Mid/High band gains (doubled internally)
- Parameter 19: Bypass

The xtractedott ratio scaling formula:

```c
float CalculateCompressionRatio(float vstValue) {
    if (vstValue > 0.5f) {
        // Above center: 1.0 to 9.0 compression ratio
        return (vstValue - 0.5f) * 16.0f + 1.0f;
    } else {
        // Below center: expansion
        return vstValue * 2.0f;
    }
}
```

So a 0.5 (center) position maps to a 1.0 ratio (unity), and the maximum 1.0
position maps to 9.0 ratio. The Ableton preset's 66.7:1 and infinity:1
ratios are achieved by the band-specific scaling that xtractedott did not
fully recover. For our implementation we should expose the macros directly
and use the Ableton preset values as the 100 percent target.

### 1.6 Attack and release times

These are not directly documented for the Xfer OTT plugin. The xtractedott
header file [xtractedott-header] lists internal constants:

- `ENVELOPE_DECAY_RATE`: 2.49999994e-05 (peak envelope decay per sample at
  44.1 kHz, approximately 0.55 dB per second)
- `COMPRESSION_SCALING`: 0.519999981
- `UPWARD_MULT_1`: 2.27304697
- `UPWARD_MULT_2`: 0.927524984
- `LOG_SCALE_FACTOR`: 0x40215f2ced384f29 (a double-precision constant, hex
  for 8.677... which is 20 / ln(10), the dB-to-natural-log conversion factor
  used to convert dB to log domain)
- `MAX_COMPRESSION_RATIO`: 36.0

The Time knob scales attack and release. The Ableton Multiband Dynamics OTT
preset uses default attack and release values around 1 ms attack and 50 ms
release, scaled by the Time knob. Community measurements and the edmprod
walkthrough [edmprod] describe OTT as "fast" sounding, consistent with short
attack (1 to 5 ms) and short to medium release (50 to 150 ms).

Recommended defaults for our implementation:
- Attack: 1 ms (at Time = 50 percent)
- Release: 50 ms (at Time = 50 percent)
- Time knob: 0 to 100 percent maps to 0.1x to 2.0x scaling of these base
  times. So at Time = 0 percent, attack = 0.1 ms, release = 5 ms. At Time =
  100 percent, attack = 2 ms, release = 100 ms.

### 1.7 Crossover filter type

The xtractedott code uses cascaded biquads with a bilinear-transform
coefficient calculation [xtractedott-filters]. The math (tan(omega/2),
reciprocal) is consistent with a 2nd-order Butterworth, and cascading two of
them gives a 4th-order Linkwitz-Riley (LR4, 24 dB/oct).

For multiband crossovers the standard is Linkwitz-Riley 4th order [lr-wiki],
[ranecommercial], [juce-forum-lr] because:

- The low-pass and high-pass outputs sum flat (0 dB at the crossover
  frequency, all-pass overall).
- The 24 dB/oct slope is steep enough to keep bands separated for
  independent compression without too much overlap.
- Both outputs are in phase at the crossover (360 degree phase difference
  for LR4), so no polarity inversion is needed when summing.

Butterworth 2nd order has a +3 dB bump at the crossover when LP and HP are
summed, which produces a tonal coloration. LR fixes this by squaring
Butterworth (cascading two identical 2nd-order Butterworth sections) to get
a -6 dB point at the crossover frequency, and the sum is flat.

Recommended filter for our implementation: LR4 (24 dB/oct). Each band split
is implemented as two cascaded 2nd-order Butterworth biquads with Q =
1/sqrt(2) (0.7071), which is exactly LR4. This gives 4 biquads total for a
3-band stereo crossover: 2 LP + 2 HP for the low/mid split, then on the
highpass (mid+high) output another 2 LP + 2 HP for the mid/high split. Per
channel that is 4 biquads, 8 for stereo.

The xtractedott code uses 6 biquads total (3 per channel for stereo) which
matches an LR4 architecture: one filter stage for the low/mid split, then
two cascaded filter stages for the mid/high split on the highpass output
[xtractedott-filters].

## 2. LV2 plugin format for Linux

### 2.1 What LV2 is and why it is the right choice for Linux

LV2 (LADSPA Version 2) is the open-source audio plugin standard that
succeeded LADSPA. It is the dominant native plugin format on Linux, shipped
by every major Linux DAW: Ardour, Zrythm, Qtractor, Carla, Jalv, MOD Duo,
and Reaper (Linux build) all host LV2 [lv2-book], [lwn-lv2], [truce-lv2],
[reddit-formats].

Differences from VST:

- LV2 splits the plugin into a shared library (`.so`) and Turtle (`.ttl`)
  data files. The TTL describes ports, parameters, UI, and metadata in
  machine-readable RDF. Hosts can scan and inspect plugins without loading
  the binary. VST2 and VST3 bundle everything in the binary and require
  loading the DLL to inspect parameters.
- LV2 is fully open and standardized at lv2plug.in. VST3 is proprietary
  Steinberg SDK (free to use, but the spec is controlled by Steinberg).
- LV2 has a stable C API defined in a single header
  (`lv2/core/lv2.h` or the older `lv2/lv2plug.in/ns/lv2core/lv2.h`). VST3
  has a C++ API with a COM-like component model and significantly more
  boilerplate.
- LV2 supports a wide range of extensions (atom for MIDI/events, urid for
  URI mapping, state for preset saving, ui for graphical interfaces,
  port-groups for multichannel, time for transport sync, patch for
  property-based messaging) that can be mixed and matched. VST3 has all of
  this built in but you cannot opt out.

For a Linux-first OTT reimplementation, LV2 is the right primary target.
VST3 is also viable on Linux (Bitwig, Reaper, Ardour 7+ all host VST3) and
can be added later if cross-platform compatibility matters. LADSPA is too
limited (no MIDI, no presets, no state, no UI, deprecated). CLAP is a newer
open alternative gaining traction but has smaller host coverage than LV2 on
Linux today.

For a first implementation, LV2 only. The DSP code should be in a separate
translation unit so that wrapping it in VST3 or a JACK standalone later is
cheap.

### 2.2 LV2 plugin structure

An LV2 plugin is shipped as a "bundle": a directory ending in `.lv2`
containing at minimum:

- `manifest.ttl`: lists the plugin(s) in the bundle by URI, points to the
  binary and the data file.
- `<plugin>.ttl`: the full plugin description (ports, parameters, metadata).
- `<plugin>.so`: the shared library with the `lv2_descriptor()` entry point.

Optional files: GUI shared library, presets, icons, documentation.

The bundle is installed to one of:

- `~/.lv2/` (per-user, no root)
- `/usr/local/lib/lv2/` (system-wide, manually installed)
- `/usr/lib/lv2/` (system-wide, package manager)
- Any directory in the `LV2_PATH` environment variable [ardour-install],
[linuxmusicians-install], [lv2-github].

### 2.3 The manifest.ttl file

The manifest is small. Its only job is to tell the host "this URI is a
plugin, its binary is here, its full description is over there" [lv2-book].

```turtle
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

<https://my-domain/ott>
    a lv2:Plugin ;
    lv2:binary <ott.so> ;
    rdfs:seeAlso <ott.ttl> .
```

The URI (`<https://my-domain/ott>`) is the plugin's global identifier. It
does not need to be a real URL, it just needs to be unique. The
`lv2:binary` and `rdfs:seeAlso` paths are relative to the bundle directory.

### 2.4 The plugin data file

This file declares the plugin's ports, port ranges, defaults, units, and
metadata. Example for a simple amplifier [lv2-book]:

```turtle
@prefix doap: <http://usefulinc.com/ns/doap#> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix units: <http://lv2plug.in/ns/extensions/units#> .

<https://my-domain/ott>
    a lv2:Plugin , lv2:CompressorPlugin ;
    doap:name "OTT" ;
    doap:license <http://opensource.org/licenses/isc> ;
    lv2:optionalFeature lv2:hardRTCapable ;
    lv2:port [
        a lv2:InputPort , lv2:ControlPort ;
        lv2:index 0 ;
        lv2:symbol "depth" ;
        lv2:name "Depth" ;
        lv2:default 100.0 ;
        lv2:minimum 0.0 ;
        lv2:maximum 100.0 ;
        units:unit units:pc ;
    ] , [
        a lv2:InputPort , lv2:AudioPort ;
        lv2:index 1 ;
        lv2:symbol "in_l" ;
        lv2:name "In L" ;
    ] , [
        a lv2:InputPort , lv2:AudioPort ;
        lv2:index 2 ;
        lv2:symbol "in_r" ;
        lv2:name "In R" ;
    ] , [
        a lv2:OutputPort , lv2:AudioPort ;
        lv2:index 3 ;
        lv2:symbol "out_l" ;
        lv2:name "Out L" ;
    ] , [
        a lv2:OutputPort , lv2:AudioPort ;
        lv2:index 4 ;
        lv2:symbol "out_r" ;
        lv2:name "Out R" ;
    ] .
```

Key points:

- The plugin type `lv2:CompressorPlugin` lets hosts categorize the plugin
  in their browser. Other useful types: `lv2:MultibandPlugin` (custom),
  `lv2:DynamicsPlugin`.
- `lv2:optionalFeature lv2:hardRTCapable` declares the plugin is real-time
  safe (no malloc, no mutex, no file I/O in `run()`). Hosts can then run it
  in a real-time thread.
- Each port has an index (the order the C code sees in `connect_port`), a
  symbol (a short identifier used by hosts for automation mapping), and a
  human-readable name.
- Control ports carry a single `float` of audio-rate-or-slower parameter
  data. Audio ports carry `float[]` sample arrays.
- Control ports should declare `default`, `minimum`, `maximum`, and
  `units:unit` (dB, pc, ms, Hz, etc.) for good host support.

For OTT the port list is:

- 0: depth (control, 0 to 100 percent, default 100)
- 1: time (control, 0 to 100 percent, default 50)
- 2: upward (control, 0 to 100 percent, default 100)
- 3: downward (control, 0 to 100 percent, default 100)
- 4: input_gain (control, -24 to +24 dB, default 0)
- 5: output_gain (control, -24 to +24 dB, default 0)
- 6: low_threshold (control, -60 to 0 dB, default -30)
- 7: mid_threshold (control, -60 to 0 dB, default -30)
- 8: high_threshold (control, -60 to 0 dB, default -30)
- 9: low_gain (control, -24 to +24 dB, default 0)
- 10: mid_gain (control, -24 to +24 dB, default 0)
- 11: high_gain (control, -24 to +24 dB, default 0)
- 12: bypass (control, 0 or 1, default 0)
- 13: in_l (audio input)
- 14: in_r (audio input)
- 15: out_l (audio output)
- 16: out_r (audio output)

### 2.5 The shared library and the C entry point

The shared library exports one function: `lv2_descriptor`. The host calls it
with increasing integer indices until it returns NULL, collecting all
plugins defined in the library. Most LV2 plugins define exactly one plugin
per library, returning the descriptor at index 0 and NULL at index 1.

The header to include is `lv2/core/lv2.h` (newer) or
`lv2/lv2plug.in/ns/lv2core/lv2.h` (older). On Debian/Ubuntu this comes from
the `lv2-dev` package. The header defines:

- `LV2_Descriptor`: a struct holding the plugin URI and 6 function pointers
  (`instantiate`, `connect_port`, `activate`, `run`, `deactivate`,
  `cleanup`) plus `extension_data`.
- `LV2_Handle`: an opaque pointer to the plugin instance (typically a
  pointer to a struct you define).
- `LV2_Feature`: a struct for host-provided features.
- `LV2_SYMBOL_EXPORT`: a portable `__attribute__((visibility("default")))`
  for the descriptor export.

The full minimal C skeleton, derived from the official eg-amp example
[lv2-book]:

```c
#include "lv2/core/lv2.h"
#include <stdlib.h>

#define OTT_URI "https://my-domain/ott"

typedef enum {
    OTT_DEPTH = 0,
    OTT_TIME = 1,
    /* ... more ports ... */
    OTT_IN_L = 13,
    OTT_IN_R = 14,
    OTT_OUT_L = 15,
    OTT_OUT_R = 16,
} PortIndex;

typedef struct {
    const float *depth;
    const float *time;
    /* ... more port pointers ... */
    const float *in_l;
    const float *in_r;
    float *out_l;
    float *out_r;
    double sample_rate;
    /* internal state: filter states, envelope followers, etc. */
} Ott;

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
                              double rate,
                              const char *bundle_path,
                              const LV2_Feature *const *features) {
    Ott *self = (Ott *)calloc(1, sizeof(Ott));
    if (!self) return NULL;
    self->sample_rate = rate;
    /* initialise DSP state here */
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data) {
    Ott *self = (Ott *)instance;
    switch ((PortIndex)port) {
        case OTT_DEPTH: self->depth = (const float *)data; break;
        case OTT_TIME:  self->time  = (const float *)data; break;
        case OTT_IN_L:  self->in_l  = (const float *)data; break;
        case OTT_IN_R:  self->in_r  = (const float *)data; break;
        case OTT_OUT_L: self->out_l = (float *)data; break;
        case OTT_OUT_R: self->out_r = (float *)data; break;
        /* ... */
    }
}

static void activate(LV2_Handle instance) {
    Ott *self = (Ott *)instance;
    /* reset all internal state (filter states, envelopes, delay buffers) */
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    Ott *self = (Ott *)instance;
    /* read control ports, process audio, write outputs */
    /* must be real-time safe: no malloc, no mutex, no file I/O */
}

static void deactivate(LV2_Handle instance) {
    /* optional: free runtime resources, called after run() before cleanup */
}

static void cleanup(LV2_Handle instance) {
    Ott *self = (Ott *)instance;
    free(self);
}

static const void *extension_data(const char *uri) {
    return NULL;  /* no extensions for a basic plugin */
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
```

The 6 methods (instantiate, connect_port, activate, run, deactivate,
cleanup) form the plugin lifecycle. Each belongs to a "threading class":

- instantiation class: `instantiate`, `activate`, `deactivate`, `cleanup`.
  Called from the main thread, never concurrent with each other or with
  `run`.
- audio class: `connect_port`, `run`. Called from the audio thread. `run`
  must be real-time safe if the plugin declares `hardRTCapable`.
- discovery class: `lv2_descriptor`, `extension_data`. Called from the main
  thread, never concurrent with anything else in the library [lv2-book].

### 2.6 The run() function and audio processing

`run()` is the heart of the plugin. It receives a block of `n_samples`
samples (typically 32 to 1024, host-dependent) and must process all ports
for that block. The pattern is:

1. Read all control port values once at the top. Control values can change
   between `run()` calls but are constant within a single call (unless the
   host uses sample-accurate automation via the atom extension, which is
   out of scope for a basic plugin).
2. Loop over samples (or use SIMD for blocks). For each sample, read input,
   run the DSP, write output.
3. Update any internal state (envelope followers, filter states) so the
   next call picks up where this one left off.

For OTT the per-sample work per channel is roughly: read input, run 4
biquads (LR4 crossover), split into 3 bands, run downward compressor per
band, run upward compressor per band, apply makeup gain per band, sum bands
back, apply dry/wet mix, write output. That is about 30 to 50 floating
point operations per sample per channel, well within real-time budget for
modern CPUs at 44.1 kHz / 48 kHz.

For multiple audio ports (stereo in/out), the standard approach is to
declare 4 separate audio ports (`in_l`, `in_r`, `out_l`, `out_r`) and
process them in the same `run()` call. An alternative is to use the
`pg:group` extension to declare a stereo group, but separate ports are
simpler and supported by every host [lv2-book].

Sample rate is passed to `instantiate()` as a `double rate`. The plugin
must store it and use it to compute filter coefficients and envelope time
constants. Sample rate does not change during the plugin's lifetime; if the
host changes its sample rate, it will instantiate a new plugin instance.

### 2.7 Build system

The simplest build is a plain Makefile:

```make
CFLAGS = -O2 -Wall -Wextra -fPIC -I/usr/include
LDFLAGS = -shared

ott.so: ott.c
        $(CC) $(CFLAGS) $(LDFLAGS) -o ott.so ott.c -lm

install: ott.so
        mkdir -p ~/.lv2/ott.lv2
        cp ott.so manifest.ttl ott.ttl ~/.lv2/ott.lv2/

clean:
        rm -f ott.so
```

The `-fPIC` and `-shared` flags are required to build a shared library.
`-lm` links the math library for `expf`, `logf`, `tanf`, etc.

The official LV2 distribution uses meson [lv2-github]. Older versions used
waf. For a single-file plugin a plain Makefile is sufficient and avoids the
meson/waf dependency.

Headers: on Debian/Ubuntu install `lv2-dev` (or `liblv2-dev`). On Fedora
install `lv2-devel`. On Arch install `lv2`. The header is at
`/usr/include/lv2/core/lv2.h` (new layout) or
`/usr/include/lv2/lv2plug.in/ns/lv2core/lv2.h` (older layout). Both work;
prefer the new layout for new code.

### 2.8 Testing and validation

Tools and hosts for testing LV2 plugins on Linux:

- `lv2lint` [sfztools-lv2lint]: static validator. Checks the TTL files for
  correctness, completeness, and best practices (proper port ranges, units,
  thread-safety declarations, etc.). Run after every build:
  `lv2lint https://my-domain/ott`
- `jalv`: command-line and Qt LV2 host. `jalv gtk https://my-domain/ott`
  opens a basic UI and lets you play audio through the plugin. `jalv -c 2
  -i` runs in CLI mode for batch processing.
- `Carla`: full-featured JACK plugin host with a GUI. Drag the plugin from
  the plugin list onto the patch canvas, connect audio, play.
- `Ardour`: full DAW. Add the plugin to a track, play audio through it.
- `lv2bm`: benchmark tool for measuring plugin CPU usage.

The standard testing workflow:

1. Build and install to `~/.lv2/ott.lv2/`.
2. Run `lv2lint https://my-domain/ott`. Fix any reported issues.
3. Open in `jalv` to verify it loads and processes audio.
4. Open in Carla or Ardour to verify it integrates with the host UI.
5. Test with a known input (e.g., a sine sweep or a drum loop) and compare
   the output against a reference OTT recording.

### 2.9 Alternative formats

LADSPA: simpler than LV2 (one C struct, no TTL), but no MIDI, no presets,
no state, no UI, no parameter units. Effectively deprecated. Not worth
targeting except as a learning exercise.

VST3: wider host coverage (Windows, macOS, Linux). C++ API with significant
boilerplate (Steinberg SDK, COM-like IComponent/IAudioProcessor). For a
Linux-first OTT, build LV2 first and add VST3 later if needed. The JUCE
framework can wrap a single DSP class as both LV2 and VST3, but adds a
heavy dependency.

CLAP: newer open standard by u-he and Bitwig. Cleaner API than VST3, open
source, growing host support. Worth watching but smaller install base than
LV2 on Linux today.

JACK standalone: a single binary that connects to JACK directly, no plugin
format overhead. Useful for development and testing the DSP in isolation
before wrapping it in LV2. Can be shipped alongside the LV2 plugin.

Recommended scope: LV2 plugin first. Keep the DSP in a separate
translation unit (`ott_dsp.h` / `ott_dsp.c`) so a JACK standalone and a
VST3 wrap can be added later without touching the DSP code.

## 3. DSP implementation details

### 3.1 Linkwitz-Riley 4th-order crossover

An LR4 crossover is built by cascading two 2nd-order Butterworth biquads at
the same cutoff frequency with Q = 1/sqrt(2) = 0.7071 [lr-wiki],
[ranecommercial], [juce-forum-lr]. The Butterworth biquad coefficients
come from the RBJ Audio EQ Cookbook [rbj-cookbook]:

```
omega = 2 * pi * fc / fs
alpha = sin(omega) / (2 * Q)    where Q = 1/sqrt(2) for Butterworth

Low-pass:
b0 = (1 - cos(omega)) / 2
b1 = 1 - cos(omega)
b2 = (1 - cos(omega)) / 2
a0 = 1 + alpha
a1 = -2 * cos(omega)
a2 = 1 - alpha

High-pass:
b0 = (1 + cos(omega)) / 2
b1 = -(1 + cos(omega))
b2 = (1 + cos(omega)) / 2
a0 = 1 + alpha
a1 = -2 * cos(omega)
a2 = 1 - alpha
```

Normalize all coefficients by dividing by `a0` (so `a0` becomes 1 and the
biquad is in standard form).

Cascading two identical Butterworth biquads (same `fc`, same `Q`) gives an
LR4 response: 24 dB/oct slope, -6 dB at `fc`, and the LP+HP sum is flat
(0 dB everywhere) with a 360 degree phase difference between the two
outputs (no polarity inversion needed when summing) [lr-wiki].

For a 3-band crossover (low / mid / high), the standard topology is:

```
                  +-- LP1 --> LP2 --> low
                  |
        input ----+
                  |
                  +-- HP1 --> HP2 --> (mid + high)
                                    |
                                    +-- LP3 --> LP4 --> mid
                                    |
                                    +-- HP3 --> HP4 --> high
```

That is 4 biquads per channel for a 3-band LR4 crossover, 8 for stereo.

C implementation of a single biquad (Direct Form I, which is numerically
stable for audio):

```c
typedef struct {
    float b0, b1, b2;  // feedforward coefficients
    float a1, a2;      // feedback coefficients (a0 normalised to 1)
    float x1, x2;      // previous input samples
    float y1, y2;      // previous output samples
} Biquad;

static inline float biquad_process(Biquad *bq, float x) {
    float y = bq->b0 * x
            + bq->b1 * bq->x1
            + bq->b2 * bq->x2
            - bq->a1 * bq->y1
            - bq->a2 * bq->y2;
    bq->x2 = bq->x1;
    bq->x1 = x;
    bq->y2 = bq->y1;
    bq->y1 = y;
    return y;
}
```

Direct Form I is preferred for audio because it is more numerically stable
than Direct Form II when coefficients approach the unit circle (which
happens at low frequencies). Direct Form II Transposed is another good
choice, used by JUCE and most modern libraries.

For cascading two biquads into an LR4, just call `biquad_process` twice
with two `Biquad` structs that have the same coefficients:

```c
typedef struct {
    Biquad stage1;
    Biquad stage2;
} LR4;

static inline float lr4_process(LR4 *f, float x) {
    float y1 = biquad_process(&f->stage1, x);
    return biquad_process(&f->stage2, y1);
}
```

For a crossover you need both the LP and HP outputs from the same input.
Implement them as two separate `LR4` structs (one LP, one HP) at the same
frequency, and feed the same input to both. The LP and HP outputs are then
used as the band signals.

Phase compensation: LR4 introduces a 360 degree phase shift across each
band, which means all bands have the same group delay at the crossover
frequency. This is why no extra delay compensation is needed between bands
when summing. LR2 (12 dB/oct) has a 180 degree difference and requires
inverting one band. LR4 avoids this [lr-wiki].

### 3.2 Compressor DSP

The canonical reference for digital compressor design is Giannoulis,
Massberg, and Reiss, "Digital Dynamic Range Compressor Design: A Tutorial
and Analysis" (JAES 2012) [giannoulis], [ctagdrc]. The structure they
recommend for a feed-forward compressor is:

```
input --> level detector --> gain computer --> ballistics (smoothing) --> gain
   |                                                                         |
   +---------------------------------(* gain)------------------------------> output
```

That is: take the input, measure its level (peak or RMS), compute the
desired gain reduction from the level using threshold/ratio/knee, smooth
the gain reduction over time with attack/release, then multiply the input
by the smoothed gain. Feed-forward is preferred over feedback because it
is stable and supports lookahead [ctagdrc], [giannoulis].

#### 3.2.1 Level detection

Peak detection: `level = fabsf(x)`. Simple, fast, captures transients.

RMS detection: `level = sqrtf(mean(x^2))` over a window. Smoother, more
musical, but introduces a windowing delay.

For OTT the Ableton Multiband Dynamics preset uses RMS detection by
default [edmprod]. We should use RMS with a short window (a few ms) or a
one-pole running average, which is the standard digital approximation.

#### 3.2.2 Log-domain conversion

Convert the linear level to dB before the gain computer, because threshold,
ratio, and knee are specified in dB:

```c
float level_db = 20.0f * log10f(level + 1e-12f);  // 1e-12 prevents log(0)
```

After the gain computer, convert the gain reduction back to a linear
multiplier:

```c
float gain_linear = powf(10.0f, gain_reduction_db * 0.05f);
```

The factor 0.05 is `1/20` because dB is amplitude ratio (20 * log10).
Working in the log domain for the gain computer and ballistics is more
numerically stable and matches how analog compressors behave [ctagdrc].

#### 3.2.3 Gain computer (downward)

Given input level `xG` in dB, threshold `T`, ratio `R`, knee width `W`,
compute output level `yG` in dB. The compression gain is then `xG - yG`,
which is the gain reduction in dB.

Hard knee (W = 0):

```c
float gain_reduction_db;
float overshoot = xG - T;
if (overshoot <= 0.0f) {
    gain_reduction_db = 0.0f;  // below threshold, no compression
} else {
    gain_reduction_db = overshoot * (1.0f - 1.0f / R);
}
```

For example, threshold -30 dB, ratio 4:1, input -10 dB. Overshoot = 20 dB.
Gain reduction = 20 * (1 - 1/4) = 20 * 0.75 = 15 dB. Output = -10 - 15 =
-25 dB. So 20 dB above threshold becomes 5 dB above threshold, which is
the 4:1 ratio (20:5 = 4:1) [mastering-compression].

Soft knee (W > 0): smooth the transition from 1:1 to R:1 over a range of
W/2 below to W/2 above the threshold. The standard formula from
Giannoulis [giannoulis], [ctagdrc]:

```c
float gain_reduction_db;
float overshoot = xG - T;
float knee_half = W * 0.5f;
float slope = 1.0f - 1.0f / R;  // compression slope

if (overshoot <= -knee_half) {
    gain_reduction_db = 0.0f;
} else if (overshoot < knee_half) {
    // quadratic transition zone
    gain_reduction_db = 0.5f * slope * (overshoot + knee_half)
                      * (overshoot + knee_half) / W;
} else {
    gain_reduction_db = slope * overshoot;
}
```

For OTT, a soft knee of 2 to 6 dB matches the Ableton Multiband Dynamics
default and produces the "musical" smash OTT is known for.

For ratios approaching infinity (limiter), `1.0f / R` approaches 0 and
`slope` approaches 1, so any signal above threshold is fully pulled down
to the threshold. This is the brickwall limiter behavior the Ableton preset
uses on the high band [edmprod].

#### 3.2.4 Gain computer (upward)

Upward compression is the mirror image. Below a lower threshold, signals
are boosted up toward the threshold at a ratio. Above the threshold, no
compression [reddit-upward], [izotope-upward], [waves-upward], [kvr-upward].

The standard formulation uses a separate lower threshold `T_low` and the
same ratio. For input level `xG` in dB:

```c
float gain_boost_db;
float undershoot = T_low - xG;  // how far below the lower threshold
if (undershoot <= 0.0f) {
    gain_boost_db = 0.0f;  // above lower threshold, no upward compression
} else {
    gain_boost_db = -undershoot * (1.0f - 1.0f / R);
}
```

Note the sign: `gain_boost_db` is negative because the gain computer
returns a "gain reduction" and a boost is a negative reduction. Apply it
by adding to the input level (or multiplying the input by the linear gain
derived from a negative reduction).

Concretely: input -50 dB, lower threshold -30 dB, ratio 4:1. Undershoot =
20 dB. Gain boost = -20 * (1 - 1/4) = -15 dB. As a "reduction" that is
-15 dB, meaning the gain applied is +15 dB (multiplicative). Output level
= -50 + 15 = -35 dB. So 20 dB below threshold becomes 5 dB below
threshold, the 4:1 ratio mirrored.

For OTT both thresholds come from the same slider [edmprod]: the user sets
one threshold per band, and the upward compressor uses a threshold some
fixed distance below the downward threshold (or the same threshold, with
the upward compressor only acting on signals that fall below it). The
Ableton Multiband Dynamics preset sets both above and below thresholds
very low (so both compressors are always active) [edmprod]. For our
implementation we can use a single threshold per band and apply both
downward compression above it and upward compression below it, with a
configurable upward ratio (4:1 from the Ableton preset) and downward ratio
(66.7:1 mid/low, infinity:1 high from the Ableton preset).

#### 3.2.5 Attack and release smoothing (ballistics)

The raw gain reduction from the gain computer is per-sample and has
discontinuities at the threshold. A smoothing filter (one-pole low-pass)
introduces the attack and release times. The Giannoulis paper recommends
two topologies: smooth-branching and smooth-decoupled [giannoulis],
[ctagdrc].

Smooth-branching (simpler):

```c
// state is the smoothed gain reduction (in dB)
// alpha_a and alpha_r are attack and release coefficients
float process_ballistics(float target, float *state,
                         float alpha_a, float alpha_r) {
    if (target < *state) {
        // gain reduction is increasing (more compression) -> attack
        *state = alpha_a * *state + (1.0f - alpha_a) * target;
    } else {
        // gain reduction is decreasing (less compression) -> release
        *state = alpha_r * *state + (1.0f - alpha_r) * target;
    }
    return *state;
}
```

Smooth-decoupled (more stable, smoother):

```c
float process_ballistics_decoupled(float target, float *state1,
                                   float *state2,
                                   float alpha_a, float alpha_r) {
    // state2 is the max of target and the release-smoothed target
    *state2 = fmaxf(target, alpha_r * *state2 + (1.0f - alpha_r) * target);
    // state1 is the attack-smoothed state2
    *state1 = alpha_a * *state1 + (1.0f - alpha_a) * *state2;
    return *state1;
}
```

The coefficients `alpha_a` and `alpha_r` are derived from the attack and
release times in seconds:

```c
// tau is the time constant in seconds (attack or release time)
// fs is the sample rate
// alpha = exp(-1 / (tau * fs))
float alpha = expf(-1.0f / (tau_seconds * fs));
```

This gives a one-pole filter where the step response reaches 63 percent
(1 - 1/e) of the final value in `tau` seconds. Some implementations use
the time to reach 90 percent (which would be `tau * ln(10)`, i.e. a
factor of 2.3 difference). The convention varies; the Giannoulis 63
percent convention is the most common [ctagdrc].

For OTT, the attack and release are short. With the recommended defaults
(attack 1 ms, release 50 ms at Time = 50 percent), at 44.1 kHz:

- alpha_a = exp(-1 / (0.001 * 44100)) = exp(-0.0227) = 0.9776
- alpha_r = exp(-1 / (0.050 * 44100)) = exp(-0.000453) = 0.9995

The Time knob scales these `tau` values. At Time = 0 percent, tau_a =
0.0001 s (0.1 ms), tau_r = 0.005 s (5 ms). At Time = 100 percent, tau_a =
0.002 s (2 ms), tau_r = 0.1 s (100 ms).

#### 3.2.6 Applying the gain

After ballistics, the smoothed gain reduction in dB is converted to a
linear multiplier and applied to the input sample:

```c
float gain_linear = powf(10.0f, gain_reduction_db * 0.05f);
float output = input * gain_linear;
```

For OTT both downward and upward run in series per band. Apply downward
first, then upward on the downward-reduced signal. The two compressors
have separate envelope followers and ballistics, but they share the band's
threshold and Time knob.

### 3.3 Stereo linking

For a stereo compressor, the question is how to derive a single detector
signal from the left and right channels so that both channels are
compressed by the same amount. Without linking, the left and right
channels compress independently, which can cause the stereo image to shift
(panning) when one channel has a louder transient than the other
[varietyofsound-linking], [gearspace-stereo], [lsp-sc-comp].

Three common linking strategies:

1. **Max**: `detector = max(level_l, level_r)`. Both channels compress
   based on the louder one. Preserves the stereo image perfectly because
   both channels get the same gain. Used by Logic Pro's "Max" mode
   [logic-max].
2. **Sum / average**: `detector = (level_l + level_r) / 2`. Compresses
   based on the average. Also preserves the image. Used by Logic Pro's
   "Sum" mode [logic-max].
3. **Independent**: no linking. Each channel compresses independently.
   Cheapest but causes image shift.

For OTT, max linking is the standard. It guarantees both channels get
identical gain, which keeps the stereo image stable under heavy
compression (essential because OTT at 100 percent wet is extremely
aggressive and any image shift would be very obvious).

Implementation: compute the per-band level (RMS or peak) for both left
and right channels, take the max, run the gain computer and ballistics on
that max level, then apply the resulting gain to both channels.

```c
float level_l = compute_level(band_l);
float level_r = compute_level(band_r);
float level_max = fmaxf(level_l, level_r);
float gain_db = compute_gain_reduction(level_max, threshold, ratio, knee);
float gain_db_smooth = process_ballistics(gain_db, &state, alpha_a, alpha_r);
float gain_linear = powf(10.0f, gain_db_smooth * 0.05f);
band_l *= gain_linear;
band_r *= gain_linear;
```

### 3.4 Putting it together: per-band OTT processing

For each sample, per channel:

1. Run the 3-band LR4 crossover on the input sample. This produces 3 band
   signals (low, mid, high) per channel.
2. For each band:
   a. Compute the per-band input level (RMS over a short window or
      one-pole running average) for left and right, take the max for
      stereo linking.
   b. Convert to dB.
   c. Run the downward gain computer (threshold, ratio, knee).
   d. Run the upward gain computer on the same level (lower threshold or
      same threshold with mirrored logic).
   e. Sum the two gain reductions (downward reduces, upward boosts).
   f. Apply ballistics (attack/release smoothing) to the combined gain.
   g. Convert the smoothed gain to linear and multiply both left and
      right band signals by it.
   h. Apply the per-band output gain (L/M/H knob in dB).
3. Sum the 3 bands back together.
4. Apply the dry/wet mix (Depth): `output = dry * (1 - depth) + wet *
   depth`.
5. Apply the master output gain.

Per-band state to keep across samples:

- LR4 filter states (4 biquads x 2 states each = 8 floats per band per
  channel = 48 floats for stereo 3-band).
- RMS smoother state (1 float per band).
- Ballistics state (1 or 2 floats per band for the gain reduction
  envelope).

Total per-instance state: roughly 60 to 80 floats, negligible memory.

### 3.5 Real-time safety checklist

For `lv2:hardRTCapable`:

- No `malloc` / `free` / `new` / `delete` in `run()`. All allocation in
  `instantiate()`.
- No `printf`, file I/O, mutexes, or system calls in `run()`.
- No unbounded loops. All loops bounded by `n_samples` or fixed constants.
- No floating-point exceptions. Use `1e-12f` floors before `logf` and
  divisions.
- Avoid `powf` and `expf` in the hot loop where possible. `powf(10, x *
  0.05)` can be replaced with `expf(x * 0.05 * 2.302585)` (one fewer
  transcendent). Or precompute a dB-to-linear lookup table.
- Coefficient recalculation: only when parameters change. Cache the
  current parameter values and recompute filter coefficients and ballistics
  alphas when the control port values change between `run()` calls.

## 4. Sources

- [edmprod] https://www.edmprod.com/ott-plugin - detailed walkthrough of
  both the Ableton Multiband Dynamics OTT preset and the Xfer OTT plugin,
  with the exact crossover frequencies (88.3 Hz and 2.5 kHz), input gains
  (+5.2 dB per band), downward ratios (infinity:1 high, 66.7:1 mid/low),
  upward ratios (4:1 all bands), and output gains (+10.3 / +5.7 / +10.3 dB
  low/mid/high).
- [pml] https://www.productionmusiclive.com/blogs/news/explained-ott-compressor -
  overview of the OTT controls (crossover, output, time, amount/depth).
- [kvr-release] https://www.kvraudio.com/news/xfer-records-releases-ott-multiband-compressor-for-windows-and-os-x-vst-and-au-19972 -
  original Xfer OTT release announcement, confirms the Depth and Time
  controls.
- [xtractedott] https://github.com/jwfeniello/xtractedott - reverse-
  engineered C implementation of OTT. README warns it is "pretty rough and
  partly incorrect". Useful for the parameter index list and the rough
  structure.
- [xtractedott-header] https://github.com/jwfeniello/xtractedott/blob/main/ott_plugin.h
  - extracted constants (ENVELOPE_DECAY_RATE, COMPRESSION_SCALING,
  UPWARD_MULT_1, etc.) and struct layout.
- [xtractedott-params] https://github.com/jwfeniello/xtractedott/blob/main/ott_parameters.c
  - parameter table and the CalculateCompressionRatio scaling formula.
- [xtractedott-filters] https://github.com/jwfeniello/xtractedott/blob/main/ott_filters.c
  - biquad implementation and the 200/2000 Hz crossover values (which we
  believe are wrong; 88.3/2500 from edmprod is the correct value for the
  real Xfer OTT).
- [lv2-book] https://lv2plug.in/book/ - David Robillard's "Programming LV2
  Plugins" book. The canonical reference. Walks through eg-amp (simple
  amplifier), eg-midigate (MIDI), eg-fifths, eg-metro, eg-sampler,
  eg-scope, eg-params. The amp example is the right starting point for a
  basic audio plugin.
- [lwn-lv2] https://lwn.net/Articles/890272 - LWN introduction to Linux
  audio plugin APIs, covers LADSPA, DSSI, LV2 history.
- [truce-lv2] https://truce.audio/docs/formats/lv2 - overview of LV2 and
  its host support (Ardour, Qtractor, Carla, Zrythm, Jalv, Reaper,
  Bitwig).
- [reddit-formats] https://www.reddit.com/r/linuxaudio/comments/ebatmc/comparison_of_plugin_formats_lv2_ladspa_dssi_vst -
  community comparison of LV2, LADSPA, DSSI, VST on Linux.
- [ardour-install] https://discourse.ardour.org/t/failed-to-install-an-lv2-plugin/109750 -
  confirms the install paths (/usr/lib/lv2, /usr/local/lib/lv2, LV2_PATH).
- [linuxmusicians-install] https://linuxmusicians.com/viewtopic.php?t=25494 -
  user-installed plugins go in /usr/local/lib/lv2 or ~/.lv2.
- [lv2-github] https://github.com/lv2/lv2 - official LV2 repo, meson build
  system.
- [sfztools-lv2lint] https://github.com/sfztools/lv2lint - LV2 lint tool,
  checks TTL files for completeness and best practices.
- [juce-forum-lr] https://forum.juce.com/t/juce-dsp-iir-filter-linkwitz-riley-4th-order-coefficients-question/31921 -
  confirms LR4 = cascade of two 2nd-order Butterworth biquads.
- [lr-wiki] https://en.wikipedia.org/wiki/Linkwitz%E2%80%93Riley_filter -
  LR filter overview. LR4 = two cascaded 2nd-order Butterworth, 24 dB/oct,
  -6 dB at fc, summed LP+HP is flat, 360 degree phase difference.
- [ranecommercial] https://www.ranecommercial.com/legacy/note160.html -
  Rane's LR crossover primer. LR4 is the most commonly used audio
  crossover.
- [rbj-cookbook] https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html -
  RBJ Audio EQ Cookbook. Biquad coefficient formulas for all standard
  filter types. Q = 1/sqrt(2) gives Butterworth; two cascaded Butterworth
  biquads give LR4.
- [giannoulis] Giannoulis, Massberg, Reiss, "Digital Dynamic Range
  Compressor Design: A Tutorial and Analysis", JAES 2012.
  https://www.researchgate.net/publication/277772168 - the canonical
  reference for digital compressor design. Covers feed-forward vs feedback,
  peak vs RMS detection, gain computer, ballistics (smooth-branching and
  smooth-decoupled).
- [ctagdrc] https://github.com/p-hlp/CTAGDRC - open source JUCE compressor
  with excellent documentation of the gain computer, soft knee, and
  ballistics. Includes C++ code for smooth-branching and smooth-decoupled
  peak detectors.
- [mastering-compression] https://mastering.com/compression-explained-threshold-ratio-and-knee -
  plain-language explanation of threshold, ratio, knee.
- [reddit-upward] https://www.reddit.com/r/audioengineering/comments/125p42q -
  upward compression raises signals below a threshold, opposite of
  downward.
- [izotope-upward] https://www.izotope.com/community/blog/expanding-on-compression-3-overlooked-techniques-for-improving-dynamic-range -
  upward compression explanation.
- [waves-upward] https://www.waves.com/upwards-compresion-the-overlooked-mix-hack-youve-been-missing -
  upward compression use cases.
- [kvr-upward] https://www.kvraudio.com/forum/viewtopic.php?t=326786 -
  upward compression is the reverse of a gate; ratio below 1 (e.g. 0.5:1)
  means boost.
- [varietyofsound] https://varietyofsound.wordpress.com/2022/09/19/sidechain-linking-techniques -
  sidechain linking techniques for stereo compressors.
- [gearspace-stereo] https://gearspace.com/board/mastering-forum/347316-compressors-stereo-image.html -
  stereo linking preserves image, sum/average vs max.
- [logic-max] https://www.logicprohelp.com/forums/topic/38523-what-is-side-chain-detection-sum-or-max-in-logic-compressor -
  Logic Pro's Max vs Sum detection modes.
- [lsp-sc-comp] https://lsp-plug.in/plugins/lv2/sc_compressor_stereo - LSP
  LV2 stereo sidechain compressor, real-world reference implementation.

## 5. Recommended implementation approach

1. Build the DSP core as a separate C library (`ott_dsp.h`, `ott_dsp.c`)
   with no LV2 dependencies. It exposes a struct `Ott` and functions
   `ott_init`, `ott_process`, `ott_set_parameter`, `ott_reset`. This lets
   us unit-test the DSP in isolation and reuse it for a JACK standalone
   or VST3 build later.
2. Inside `ott_dsp.c`:
   - 4 LR4 biquads per channel for the 3-band crossover at 88.3 Hz and
     2500 Hz.
   - Per-band downward compressor (gain computer with soft knee,
     ballistics with smooth-decoupled peak detector).
   - Per-band upward compressor sharing the band threshold.
   - Stereo linking via max of left and right levels.
   - Default parameters from the Ableton preset (downward 66.7:1 mid/low
     and infinity:1 high, upward 4:1, soft knee 2 to 6 dB, attack 1 ms,
     release 50 ms, per-band input gain +5.2 dB, per-band output gain
     +10.3 / +5.7 / +10.3 dB, depth 100 percent, time 50 percent).
   - The Upward/Downward knobs scale the ratios from 1:1 (no compression)
     to the preset target.
   - The Time knob scales attack and release by 0.1x to 2.0x.
3. Build the LV2 wrapper (`ott_lv2.c`) that includes `lv2/core/lv2.h`,
   wraps the `Ott` struct, and exports `lv2_descriptor`. The wrapper is
   thin: `instantiate` calls `ott_init`, `connect_port` stores port
   pointers, `activate` calls `ott_reset`, `run` calls `ott_process`,
   `cleanup` calls `free`.
4. Write the `manifest.ttl` and `ott.ttl` files with the 17 ports listed
   in section 2.4. Declare `lv2:hardRTCapable`.
5. Build with a plain Makefile (section 2.7). Install to `~/.lv2/ott.lv2/`.
6. Validate with `lv2lint`. Test in `jalv` and `carla`.
7. Iterate on the sound. Compare against the real Xfer OTT on reference
   audio (drum loops, pads, full mixes) at various Depth and Time
   settings. Adjust the soft knee width, the exact Time scaling curve,
   and the per-band defaults until the sound matches.
8. Optional later: add a JACK standalone (`ott_jack.c`) for live use, and
   a VST3 wrap (via JUCE or directly) for cross-platform compatibility.
