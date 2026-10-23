# OpenScan-Swabian

An [OpenScan](https://github.com/openscan-lsm/OpenScanLib) device module that
adapts a [Swabian Instruments Time Tagger](https://www.swabianinstruments.com/static/documentation/TimeTagger/index.html)
as a **detector** for laser-scanning
microscopy TCSPC (time-correlated single-photon counting) acquisitions,
usable from [Micro-Manager](https://micro-manager.org/) via OpenScan's LSM
device.

The module reads raw event tags from the Time Tagger (sync, line clock, and
up to 4 detector inputs), correlates photons against sync pulses, bins them
per pixel using a [libtcspc](https://github.com/marktsuchida/libtcspc)
processing pipeline, and delivers each frame to OpenScanLib as a
conventional 16-bit image per detector.

## Repository layout

- `src/TimeTagger.cpp`, `src/TimeTaggerSettings.cpp`, `src/TimeTaggerPrivate.h`
  — the `OScDev_DeviceImpl` device module itself: device lifecycle
  (`Open`/`Close`/`Arm`/`Start`/`Stop`/`IsRunning`/`Wait`) and the settings
  described below.
- `src/Processing.cpp`/`.h` — the libtcspc processing graph: decodes raw
  tags, pairs sync/photon detections, derives per-pixel timing windows from
  the line clock, bins photons into per-pixel histograms, and delivers
  frames.
- `src/AcquisitionRun.*`, `src/TagStreamMeasurement.*`,
  `src/TagStreamProcessor.*`, `src/ProcessingThread.*` — the acquisition
  lifecycle: the SDK measurement that delivers raw tags, the thread that
  pumps the processing graph, and the object that owns them for one armed
  acquisition.
- `src/fake_timetagger/` — a from-scratch, header-only fake of the vendor
  Time Tagger C++ SDK, used for `simulate=true` builds (see below). Its goal
  is to be a faithful stand-in: code written against it should build and run
  unmodified against the real SDK headers. It also synthesizes plausible
  sync/photon/line-clock activity so the pipeline has something to process
  without real hardware attached.
- `tools/dump_tags.py` — decodes a raw Time Tagger tag dump (the format
  written by the SDK's `Dump` measurement, and by this module's own
  `SaveRawData` setting) into human-readable text.
- `tools/dump_graph.cpp` — the `dump_graph` executable, which prints the
  libtcspc processing graph (built by the same `MakeProcessingPipeline` the
  module uses) in Graphviz dot format. See
  [Processing graph](#processing-graph).

## Building

Building this device module requires [Meson](https://mesonbuild.com/) (>=1.9.0) and Ninja, and an MSVC or
`clang-cl` toolchain (this project is Windows-only, matching the Time Tagger
SDK). Building should be done on a Visual Studio developer command prompt (or after running
`vcvars64.bat`).

### Hardware

By default the module builds against a real Time Tagger and the vendor SDK.

If the vendor SDK isn't installed yet, download and run the Windows
installer from Swabian Instruments'
[installation guide](https://www.swabianinstruments.com/static/documentation/TimeTagger/gettingStarted/installation.html)
before building — this installs the headers and libraries the build links
against, along with the USB drivers and license/runtime components needed
to actually run against hardware.

The build locates the installed SDK via the `TIMETAGGER_INSTALL_PATH`
environment variable (set by Swabian's installer to the SDK's install
root). If that variable isn't set, the build falls back to the default
install location (`C:/Program Files/Swabian Instruments/Time Tagger`) and
logs a warning — set `TIMETAGGER_INSTALL_PATH` yourself if the SDK is
installed somewhere else.

```
meson setup builddir --buildtype=release
meson compile -C builddir
```

> [!NOTE]
> `--buildtype=release` is important, as [libtcspc](https://github.com/marktsuchida/libtcspc)
> is optimization-sensitive, showing up directly in this module's live-acquisition
> throughput.

### Simulation

To build against the fake SDK in `src/fake_timetagger/` instead — no
hardware or vendor SDK installation needed, useful for developing and
testing the pipeline itself:

```
meson setup builddir -Dsimulate=true --buildtype=release
meson compile -C builddir
```

No source changes are needed to switch between the two — `#include
<TimeTagger.h>` resolves to one or the other purely via the include path
meson selects, based on the `simulate` option.

### Processing graph

The build also produces `dump_graph.exe`, which prints the processing graph
in Graphviz dot format. The graph's shape depends on the "cumulative" and
file output settings, selected with `--cumulative`, `--histogram-dump`, and
`--raw-data`, and on the number of detectors, selected with `--detectors N`
(1–4, default 1):

```
builddir\dump_graph.exe --histogram-dump | dot -Tsvg -o graph.svg
```

### Installing into Micro-Manager

Copy the built `OpenScanSwabian.osdev` into your Micro-Manager installation's
device adapter directory alongside the other OpenScan device modules
(`OpenScan.dll`, `OpenScan-NIDAQ.osdev`, etc.), then add the device
(`Swabian Time Tagger`) as a detector in your OpenScan `.cfg` configuration —
see the OpenScan/Micro-Manager documentation for how detector/scanner/clock
roles are assembled into an LSM device.

## Settings

These are exposed as standard MicroManager device properties (device
`Swabian Time Tagger`, prefixed with the model name in the property browser).

Settings with an `_ns` suffix are in nanoseconds and are rounded to the
nearest picosecond.

### Inputs

The Time Tagger inputs (as the SDK's channel numbers) that this module
expects each signal to be wired to. A positive number selects the input's
rising edge and the corresponding negative number its falling edge, so e.g.
`-3` uses the falling edge of input 3. The allowed values are queried from
the connected device.

- **SyncInput** (default `2`) — the laser sync / reference clock input.
  Only the configured edge is used and recorded; the input's other edge is
  never registered with the Time Tagger.
- **LineClockInput** (default `1`) — a per-scan-line marker from the
  scanner; the configured edge marks the start of a line. Used to derive
  per-pixel timing windows: each line-clock tick starts a run of `width`
  pixel windows, each `1e12 / pixelRate` picoseconds wide, where `width` and
  `pixelRate` come from the current OpenScan acquisition (ROI width, pixel
  rate), not from a setting here.

### Detectors

Up to 4 detectors (e.g. PMTs) can be acquired simultaneously. There are 4
detector slots, `Detector0` to `Detector3`, each with its own settings (`N`
below is the slot number):

- **EnableDetectorN** (default `true` for Detector0, `false` for the others)
  — whether the detector is acquired.
- **DetectorNInput** (defaults `-3`, `-4`, `-5`, `-6`) — the detector's pulse
  input. Both edges of this input are used: the configured edge is the
  pulse's leading edge and the opposite edge its trailing edge. The module
  pairs each leading edge with its matching trailing edge and correlates the
  pulse's midpoint against the sync. The negative defaults select the
  falling edge as the leading edge, matching the negative pulse a typical
  detector/preamp produces; use the positive input number for a positive
  pulse.
- **DetectorNTriggerLevel_V** — see [Trigger levels](#trigger-levels).
- **DetectorNHardwareDelay_ns** — see [Timing](#timing).

The enabled detectors become OpenScan channels `0` to `n-1` in slot order,
where `n` is the number of enabled detectors: for example, with only
Detector1 and Detector3 enabled, Detector1 is channel 0 and Detector3 is
channel 1.

When an acquisition is armed, it fails if no detector is enabled, or if any
two of the sync, the line clock, and the enabled detectors use the same
physical input (regardless of edge).

#### Conditional filter

At a typical laser sync rate (e.g. 80 MHz) the sync channel alone would
saturate the Time Tagger's USB bandwidth. For every acquisition the module
therefore enables the Time Tagger's on-board
[conditional filter](https://www.swabianinstruments.com/static/documentation/TimeTagger/sections/tutorials/conditionalFilter.html)
with both edges of every enabled detector as the *trigger* channels and the
sync channel as the *filtered* channel: the device transmits only the first
sync edge following each photon edge (of any detector), so the sync is
transmitted about once per photon (twice, when a sync falls between a photon
pulse's two edges) regardless of the laser rate. Both photon edges must
trigger because the pipeline correlates the pulse midpoint, which the
hardware cannot see. The consequences for
**SyncDelay_ns** and **MaxDiffTime_ns** are described below.

### Trigger levels

The trigger level is the comparator threshold, in volts, of a physical Time
Tagger input: an edge is detected when the signal crosses this level. A level
belongs to the input, not to an edge, so both edges of an input share one
level; in particular a single setting covers a detector input's leading and
trailing edges. The levels are written to the device each time an
acquisition is armed.

- **SyncTriggerLevel_V** (default `0.0`) — threshold for the sync input.
- **DetectorNTriggerLevel_V** (default `-0.1`) — threshold for the detector
  input. Negative because a typical detector/preamp pulse is negative; a good
  starting point is about half the pulse amplitude. This default and the
  DetectorNInput defaults (falling leading edge) are chosen together for
  such a pulse.

The allowed range is queried from the device: `0.0`–`2.5 V` on a Time Tagger
20 (so the detector levels must be made positive there, along with the
DetectorNInput values), `-2.5`–`2.5 V` on a Time Tagger Ultra, and `-1`–`1 V`
on a Time Tagger X. The line clock input has no setting: its level is fixed at `1.5 V`
(TTL/LVTTL compatible), or the device's range maximum if that is lower
(`1.0 V` on a Time Tagger X).

### Timing

- **SyncDelay_ns** (default `-12.5`) — a fixed offset applied in software to
  sync detections before correlating them with photons (`delay()` ahead of
  the sync/photon pairing stage). Because the conditional filter passes the
  sync edge *following* each photon, this should be set to minus the laser
  sync period (the default, `-12.5`, is for 80 MHz), which moves that sync back to
  (approximately) where the preceding sync was, so that the difftime is the
  usual "time since the previous laser pulse". This is a software-side shift:
  it relabels the difftime axis but cannot change *which* sync edge the
  hardware filter passes; for that, see **DetectorNHardwareDelay_ns**.
- **DetectorNHardwareDelay_ns** (default `0`) — a hardware delay applied on
  the Time Tagger to both edges of the detector's input. The allowed range is
  queried from the device (±2500 ns on a Time Tagger X, ±2000 ns on a Time
  Tagger Ultra); the Time Tagger 20 has no hardware delay, and there these
  settings must stay `0` (delays are then never written to the device). The
  delay is
  applied on board, before the conditional filter, so it shifts which sync
  edge the filter passes for each photon and therefore where within the sync
  period the fluorescence decay lands. Tune it so that the decay does not
  straddle a sync edge: a decay whose start coincides with a sync edge would
  otherwise be split between the beginning and the end of the difftime
  window. SyncDelay_ns cannot do this, because it is applied in software
  after the filter has already chosen the sync edge. Fixed offsets between
  the sync and detector paths (e.g. cable length) are also compensated here,
  per detector. The sync and line clock inputs always get a hardware delay
  of `0`.

The following are shared by all detectors:

- **MaxPhotonPulseWidth_ns** (default `100`) — the maximum allowed
  separation between a detector input's rising and falling edges for them to
  be treated as one pulse. Should comfortably exceed the detector's real
  pulse width, but stay short enough to avoid accidentally pairing across two
  separate pulses.
- **MaxDiffTime_ns** (default `12.5`) — the maximum allowed time between
  a sync tick and a photon detection for them to be correlated as "this
  photon resulted from this sync pulse." Only correlated photons are counted,
  in both the live image and the histogram. It should be set based on the
  laser's sync period (the default matches 80 MHz) and the fluorophore's
  expected decay time, **not** based on the pixel dwell time.
  Those are unrelated: pixel dwell time is about how long the scanner stays
  at one spatial position, while MaxDiffTime_ns is about how long after a
  single laser pulse a photon can plausibly arrive and still belong to that
  pulse. In a typical setup many sync pulses occur within one pixel's dwell
  time, each independently contributing photons to that pixel's histogram.

  It must not exceed the laser sync period. When a sync falls between a
  photon pulse's two edges, the conditional filter passes two sync edges for
  that photon; after the SyncDelay_ns shift they sit one period apart, and
  only the one within MaxDiffTime_ns pairs with the photon. A larger value
  would count such photons twice.

### Histogram binning

- **HistogramBins** (default `256`) — the per-pixel TCSPC histogram has this
  many bins, covering difftimes
  `[0, HistogramBinWidth_ps × HistogramBins)`.
- **HistogramBinWidth_ps** (default `50`, range `1`–`100000`) — the width
  of each histogram bin, in picoseconds.

  > [!NOTE]
  > Photons whose difftime is within **MaxDiffTime_ns** but at or beyond
  > `HistogramBinWidth_ps × HistogramBins` are dropped from the saved
  > histogram, yet still counted in the live image. Conversely, if
  > `HistogramBinWidth_ps × HistogramBins` exceeds **MaxDiffTime_ns**,
  > the trailing bins stay empty. With the defaults (50 ps × 256 = 12.8 ns ≥
  > 12.5 ns) nothing is dropped.
- **Cumulative** (default `false`) — if enabled, the per-pixel histogram
  accumulates across the whole acquisition instead of resetting every frame.
- **SaveHistograms** (default `false`) — when enabled, the full per-pixel
  histogram (HistogramBins bins/pixel) of every enabled detector is
  additionally computed and written to `<prefix>_NNNN.hist` (see
  **FileNamePrefix**), for offline inspection. The file is raw native-endian
  `uint16` with shape (frame, channel, y, x, bin), or (channel, y, x, bin)
  when Cumulative (written once, at the end of the acquisition); `channel`
  is the OpenScan channel index (see [Detectors](#detectors)). This is off by
  default because computing and dumping it roughly doubles per-event
  processing cost and writes a large file
  (`channels * width * height * HistogramBins * 2` bytes per frame) — leave
  it off for normal/live acquisitions and enable it only when you need to
  inspect the underlying histogram data.

### Live image

Each enabled detector has its own live image (OpenScan channel). Only the
total photon count per pixel (summed across all histogram bins, independent
of HistogramBins) is sent to OpenScanLib/Micro-Manager as the
live/displayed image — the frame callback contract only supports one 16-bit
sample per pixel.

### File output

- **SaveRawData** (default `false`) — when enabled, writes every raw tag
  received during the acquisition to a file in the vendor SDK's raw dump
  format (16-byte records; decode with `tools/dump_tags.py`).
- **FileNamePrefix** (default `OpenScan-Swabian`) — path prefix for saved
  files; may include a directory (e.g. `C:/Data/myexperiment`) or be a bare
  name (relative to Micro-Manager's working directory). Each acquisition's
  actual filenames are `<prefix>_NNNN.raw` and `<prefix>_NNNN.hist`, where
  `NNNN` (0000-9999) is the smallest index not in use by either extension;
  both files of one acquisition share the index.

## Simulate mode details

When built with `-Dsimulate=true`, `src/fake_timetagger/` synthesizes
activity on fixed inputs so the pipeline has realistic-looking data to
process without hardware: a line clock on input 1, a sync on input 2, and
independent gated Poisson photon noise, correlated to the sync, on inputs 3
and 4. The simulated photons are negative pulses (falling edge first),
matching the default Detector0Input and Detector1Input of `-3` and `-4`
(enable Detector1 to see both). The simulated device has 8 inputs; inputs
5–8 produce no events. These are fixed, hardcoded constants
(`LINE_CLOCK_CHANNEL`, `SYNC_CHANNEL`, `PHOTON_CHANNELS`, and their rates, in
`src/fake_timetagger/include/TimeTagger.h`) rather than derived from the
current OpenScan acquisition's settings — the fake has no access to
OpenScan-specific concepts like pixel rate or ROI, by design (see that
file's comments for the full rationale). In practice this means the
simulated channel numbers and rates need to be kept roughly in step with the
SyncInput/DetectorNInput/LineClockInput settings and the acquisition's pixel rate for
the simulated data to look meaningful; see the comments alongside those
constants for the current assumptions and how to recompute them if you
change the acquisition's ROI or pixel rate.
