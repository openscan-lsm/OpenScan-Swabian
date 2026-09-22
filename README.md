# OpenScan-Swabian

An [OpenScan](https://github.com/openscan-lsm/OpenScanLib) device module that
adapts a [Swabian Instruments Time Tagger](https://www.swabianinstruments.com/static/documentation/TimeTagger/index.html)
as a **detector** for laser-scanning
microscopy TCSPC (time-correlated single-photon counting) acquisitions,
usable from [Micro-Manager](https://micro-manager.org/) via OpenScan's LSM
device.

The module reads raw event tags from the Time Tagger (sync, photon, and line
clock channels), correlates photons against sync pulses, bins them per pixel
using a [libtcspc](https://github.com/marktsuchida/libtcspc) processing
pipeline, and delivers each frame to OpenScanLib as a conventional 16-bit
image.

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
  written by the SDK's `Dump` measurement, and by this module's own "Save Raw
  Data" setting) into human-readable text.

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

### Channels

The Time Tagger's raw channel numbers that this module expects each signal
to be wired to. A positive number selects the input's rising edge and the
corresponding negative number its falling edge, so e.g. `-3` uses the
falling edge of input 3.

- **Sync Channel** (default `2`) — the laser sync / reference clock input.
  Only the configured edge is used and recorded; the input's other edge is
  never registered with the Time Tagger.
- **Photon Channel** (default `3`) — the detector (e.g. PMT) pulse input.
  Both edges of this input are used: the configured edge is the pulse's
  leading edge and the opposite edge its trailing edge. The module pairs each
  leading edge with its matching trailing edge and correlates the pulse's
  midpoint against the sync.
- **Line Clock Channel** (default `1`) — a per-scan-line marker from the
  scanner; the configured edge marks the start of a line. Used to derive
  per-pixel timing windows: each line-clock tick starts a run of `width`
  pixel windows, each `1e12 / pixelRate` picoseconds wide, where `width` and
  `pixelRate` come from the current OpenScan acquisition (ROI width, pixel
  rate), not from a setting here.

#### Conditional filter

At a typical laser sync rate (e.g. 80 MHz) the sync channel alone would
saturate the Time Tagger's USB bandwidth. For every acquisition the module
therefore enables the Time Tagger's on-board
[conditional filter](https://www.swabianinstruments.com/static/documentation/TimeTagger/sections/tutorials/conditionalFilter.html)
with both photon edges as the *trigger* channels and the sync channel as the
*filtered* channel: the device transmits only the first sync edge following
each photon edge, so the sync is transmitted once per photon (twice, when a
sync falls between a photon pulse's two edges) regardless of the laser rate.
Both photon edges must trigger because the pipeline correlates the pulse
midpoint, which the hardware cannot see. The consequences for **Sync Delay
(ps)** and **Max Diff Time (ps)** are described below.

### Timing

- **Sync Delay (ps)** (default `0`) — a fixed offset applied in software to
  sync detections before correlating them with photons (`delay()` ahead of
  the sync/photon pairing stage). Because the conditional filter passes the
  sync edge *following* each photon, this should be set to minus the laser
  sync period (e.g. `-12500` for 80 MHz), which moves that sync back to
  (approximately) where the preceding sync was, so that the difftime is the
  usual "time since the previous laser pulse". This is a software-side shift:
  it relabels the difftime axis but cannot change *which* sync edge the
  hardware filter passes.
- **Max Photon Pulse Width (ps)** (default `100000`) — the maximum allowed
  separation between a photon channel's rising and falling edges for them to
  be treated as one pulse. Should comfortably exceed the detector's real
  pulse width, but stay short enough to avoid accidentally pairing across two
  separate pulses.
- **Max Diff Time (ps)** (default `12500`) — the maximum allowed time between
  a sync tick and a photon detection for them to be correlated as "this
  photon resulted from this sync pulse." Only correlated photons are counted,
  in both the live image and the histogram. It should be set based on the
  laser's sync period (the default matches 80 MHz) and the fluorophore's
  expected decay time, **not** based on the pixel dwell time.
  Those are unrelated: pixel dwell time is about how long the scanner stays
  at one spatial position, while Max Diff Time (ps) is about how long after a
  single laser pulse a photon can plausibly arrive and still belong to that
  pulse. In a typical setup many sync pulses occur within one pixel's dwell
  time, each independently contributing photons to that pixel's histogram.

  It must not exceed the laser sync period. When a sync falls between a
  photon pulse's two edges, the conditional filter passes two sync edges for
  that photon; after the Sync Delay (ps) shift they sit one period apart, and
  only the one within Max Diff Time (ps) pairs with the photon. A larger value
  would count such photons twice.

### Histogram binning

- **Histogram Bins** (default `256`) — the per-pixel TCSPC histogram has this
  many bins, covering difftimes
  `[0, Histogram Bin Width × Histogram Bins)`.
- **Histogram Bin Width (ps)** (default `50`, range `1`–`100000`) — the width
  of each histogram bin.

  > [!NOTE]
  > Photons whose difftime is within **Max Diff Time (ps)** but at or beyond
  > `Histogram Bin Width × Histogram Bins` are dropped from the saved
  > histogram, yet still counted in the live image. Conversely, if
  > `Histogram Bin Width × Histogram Bins` exceeds **Max Diff Time (ps)**,
  > the trailing bins stay empty. With the defaults (50 × 256 = 12800 ps ≥
  > 12500 ps) nothing is dropped.
- **Cumulative** (default `false`) — if enabled, the per-pixel histogram
  accumulates across the whole acquisition instead of resetting every frame.
- **Save Histograms** (default `false`) — when enabled, the full per-pixel
  histogram (Histogram Bins bins/pixel) is additionally computed and
  written to `histogram_debug.bin` (next to the executable) once per frame,
  for offline inspection. This is off by default because computing and
  dumping it roughly doubles per-event processing cost and writes a large
  file (`width * height * Histogram Bins * 2` bytes) every frame — leave it
  off for normal/live acquisitions and enable it only when you need to
  inspect the underlying histogram data.

### Live image

Only the total photon count per pixel (summed across all histogram bins,
independent of Histogram Bins) is sent to OpenScanLib/Micro-Manager as the
live/displayed image — the frame callback contract only supports one 16-bit
sample per pixel. 

### File output

- **Save Raw Data** (default `false`) — when enabled, writes every raw tag
  received during the acquisition to a file in the vendor SDK's raw dump
  format (16-byte records; decode with `tools/dump_tags.py`).
- **File Name Prefix** (default `OpenScan-Swabian`) — path prefix for saved
  files; may include a directory (e.g. `C:/Data/myexperiment`) or be a bare
  name (relative to Micro-Manager's working directory). Each acquisition's
  actual filename is `<prefix>_NNNN.raw`, where `NNNN` (0000-9999) is the
  smallest index not already in use.

## Simulate mode details

When built with `-Dsimulate=true`, `src/fake_timetagger/` synthesizes
activity on three fixed channels so the pipeline has realistic-looking data
to process without hardware: a line clock, a sync channel, and gated Poisson
photon noise correlated to the sync channel. These are fixed, hardcoded
constants (`LineClockChannel`, `SyncChannel`, `PhotonChannel`, and their
rates, in
`src/fake_timetagger/include/TimeTagger.h`) rather than derived from the
current OpenScan acquisition's settings — the fake has no access to
OpenScan-specific concepts like pixel rate or ROI, by design (see that
file's comments for the full rationale). In practice this means the
simulated channel numbers and rates need to be kept roughly in step with the
Sync/Photon/Line Clock Channel settings and the acquisition's pixel rate for
the simulated data to look meaningful; see the comments alongside those
constants for the current assumptions and how to recompute them if you
change the acquisition's ROI or pixel rate.
