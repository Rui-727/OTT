# OTT

OTT is a three-band multiband compressor that applies downward and upward
compression simultaneously in each band. Loud content gets pulled down toward
the threshold and quiet content gets pulled up, producing the dense, smashed
sound associated with the original Xfer Records OTT plugin. At low wet/dry
mixes it behaves like parallel compression, adding detail and shimmer without
killing transients. At 100 percent wet it is the full over-the-top smash.

This is an independent LV2 reimplementation for Linux. The DSP is a separate
C translation unit with no LV2 dependency, so it can be wrapped in other
formats (JACK standalone, VST3) later. The crossover is a Linkwitz-Riley 4th
order (24 dB/oct) at 88.3 Hz and 2500 Hz, matching the Ableton Multiband
Dynamics OTT preset that the original plugin replicates. Each band runs a
feed-forward compressor with soft-knee downward compression (high ratios, the
high band is a limiter) followed by 4:1 upward compression. Stereo linking
uses the max of the left and right per-band levels so the stereo image stays
stable under heavy gain reduction.

Credit for the original design goes to Steve Duda at Xfer Records and to the
Ableton Multiband Dynamics factory preset. This project is not affiliated with
either.

## Build

Requirements: a C compiler (`cc` or `gcc`) and `make`. No external libraries
are needed. The LV2 host headers are not required: a minimal `lv2/lv2.h` is
bundled with the source. If your system has `lv2-dev` installed, the wrapper
still works (the bundled header is sufficient and self-contained).

```
make
```

This produces `ott.lv2/ott.so`, the shared library for the plugin bundle.

## Test

Unit tests for the DSP core run without an LV2 host:

```
make test
```

The tests check bypass, DC stability, silence, sine audibility, dry/wet mix,
crossover flatness, upward compression direction, downward compression
direction, stereo linking, and NaN/Inf safety on a long low-level run.

## Install

Install the bundle to `~/.lv2/ott.lv2/` (per-user, no root):

```
make install
```

To install system-wide instead, override `LV2_DIR`:

```
make install LV2_DIR=/usr/local/lib/lv2
```

Uninstall with `make uninstall`.

## Use

After installing, the plugin appears in any LV2 host under the URI
`https://github.com/Rui-727/OTT`. Verified hosts include Ardour, Carla,
Qtractor, Zrythm, and Jalv. A few quick ways to try it:

- `jalv gtk https://github.com/Rui-727/OTT` opens a basic control UI and
  routes JACK audio through the plugin.
- In Carla, add the plugin from the plugin browser (search "OTT") and wire
  audio on the patch canvas.
- In Ardour, add OTT to a track as an inline or post-fader plugin.

## Parameters

| Index | Symbol          | Name              | Range         | Default |
|-------|-----------------|-------------------|---------------|---------|
| 0     | depth           | Depth             | 0.0 to 1.0    | 1.0     |
| 1     | time            | Time              | 0.0 to 1.0    | 0.5     |
| 2     | upward          | Upward            | 0.0 to 1.0    | 1.0     |
| 3     | downward        | Downward          | 0.0 to 1.0    | 1.0     |
| 4     | input_gain      | Input Gain        | -24 to +24 dB | +5.2    |
| 5     | output_gain     | Output Gain       | -24 to +24 dB | 0.0     |
| 6     | band1_thresh    | Band 1 Threshold  | -60 to 0 dB   | -30.0   |
| 7     | band2_thresh    | Band 2 Threshold  | -60 to 0 dB   | -30.0   |
| 8     | band3_thresh    | Band 3 Threshold  | -60 to 0 dB   | -30.0   |
| 9     | band1_gain      | Band 1 Gain       | -24 to +24 dB | +10.3   |
| 10    | band2_gain      | Band 2 Gain       | -24 to +24 dB | +5.7    |
| 11    | band3_gain      | Band 3 Gain       | -24 to +24 dB | +10.3   |
| 12    | bypass          | Bypass            | 0 or 1        | 0       |
| 13    | in_l            | In L              | audio         |         |
| 14    | in_r            | In R              | audio         |         |
| 15    | out_l           | Out L             | audio         |         |
| 16    | out_r           | Out R             | audio         |         |

- **Depth** is the wet/dry mix. 0 is the dry input, 1 is the fully processed
  signal. Low values (0.1 to 0.3) give a parallel-compression feel.
- **Time** scales the attack and release times of all three bands. 0.0 maps to
  0.1x the nominal times, 1.0 maps to 2.0x. The default 0.5 is roughly 1.0x.
- **Upward** scales the upward compression ratio from none (0.0) to the full
  4:1 from the Ableton preset (1.0).
- **Downward** scales the downward compression ratio from none (0.0) to the
  full preset ratios (66.7:1 on low and mid, infinity:1 on high).
- **Input Gain** drives the signal into the compressors. The +5.2 dB default
  matches the Ableton preset.
- **Output Gain** is the master makeup gain applied after the band sum.
- **Band N Threshold** sets the threshold for both the upward and downward
  compressor in that band.
- **Band N Gain** is the per-band makeup gain applied after compression and
  before the band sum.
- **Bypass** routes the input directly to the output, bypassing all
  processing.

## License

MIT. See `LICENSE`.
