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
are needed for the plugin itself. The LV2 host headers are not required: a
minimal `lv2/lv2.h` (and `lv2/ui.h` for the UI) is bundled with the source.
If your system has `lv2-dev` installed, the wrapper still works (the bundled
headers are sufficient and self-contained).

```
make
```

This produces `ott.lv2/ott.so` (the plugin) and, when pugl and Cairo are
available, `ott.lv2/ott_ui.so` (the graphical UI).

### Graphical UI (optional)

The UI is built with pugl and Cairo. The Makefile probes for pugl via
`pkg-config --exists pugl-0 pugl-cairo-0 cairo` and only builds the UI when
all three are found. If they are missing, the plugin still builds and works
with the host's generic parameter sheet.

Install the dependencies on common distros:

- Debian/Ubuntu: `apt install libpugl-dev libcairo2-dev` (note: the Debian
  `libpugl-dev` package ships the old OpenGL-only pugl API, so for the Cairo
  backend you may need to build pugl from source, see below).
- Arch: `pacman -S pugl cairo`.
- Fedora: `dnf install pugl-devel cairo-devel`.

If your distro's `libpugl-dev` is too old (pre-0.5) or missing the Cairo
backend, build pugl from git:

```
git clone https://github.com/lv2/pugl.git /tmp/pugl
cd /tmp/pugl
meson setup build --prefix=/usr/local --default-library=static \
    -Dcairo=enabled -Dopengl=disabled -Dvulkan=disabled \
    -Dexamples=disabled -Dtests=disabled
ninja -C build install
```

Then point `PKG_CONFIG_PATH` at the install prefix if you used a non-system
one (for example `PKG_CONFIG_PATH=/home/you/local/lib/x86_64-linux-gnu/pkgconfig make`).

The UI declares itself as `ui:X11UI` with `ui:fixedSize true`. Hosts that
support X11 embedding (Ardour, Carla, Jalv, Qtractor, Zrythm, REAPER) will
embed it directly; hosts that cannot embed fall back to the
`ui:showInterface` to open the UI in its own top-level window.

The UI is a fixed 580 x 440 pixel window with three rows of controls:
DEPTH, TIME, IN, OUT across the top; three gain-reduction meters (H, B, L)
in the middle; and per-band THRESH / GAIN pairs for HIGH, MID, LOW across
the bottom, with an ACTIVE button centered below. Knobs are flat white
circles with black outlines and black pointers, matching the Xfer OTT
visual. The `upward` and `downward` ports are not exposed as widgets:
they keep their default value (1.0) and can be adjusted through the
host's generic parameter sheet if needed.

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
  routes JACK audio through the plugin. If `ott_ui.so` is installed, use
  `jalv.gtk https://github.com/Rui-727/OTT` to get the bundled graphical UI.
- In Carla, add the plugin from the plugin browser (search "OTT") and wire
  audio on the patch canvas.
- In Ardour, add OTT to a track as an inline or post-fader plugin.

The bundled UI (when built) is a 560x360 fixed-size X11 window drawn with
Cairo. It exposes 12 knobs (Depth, Time, In Gain, Out Gain, three band
thresholds, three band gains, Upward, Downward) and a Bypass toggle.
Left-drag a knob vertically to change its value, hold Shift for fine
control, and double-click to reset to the default. The band columns are
arranged HIGH / MID / LOW (left to right) to match the Xfer OTT visual;
band indices in the DSP go low/mid/high, so the port ordering is reversed
relative to the visual order.

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
