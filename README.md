# ShardNoise

ShardNoise is a MIDI-triggered high-frequency binary/velvet noise instrument built with [YUP](https://github.com/kunitoki/yup). It treats MIDI notes as deterministic trigger seeds and burst subdivision inputs, not oscillator pitch.

The project builds as a standalone app and VST3 on macOS and Windows, plus Audio Unit v2 on macOS. Its compact editor exposes all seven engine parameters, four presets, versioned state save/load, and stable host automation IDs.

## Sound Engine

The pure C++20 engine is allocation-free in the audio path and produces repeatable stereo shards from deterministic binary and sparse velvet generators. A note-on reseeds the generators, resets the short burst envelope, and drives a bank of high-pass and band-pass filters into fold stages with optional alias emphasis.

Output is clamped to finite bounded samples, and parameter inputs are sanitized internally so NaN, infinite, and out-of-range values cannot escape the DSP boundary.

## Parameters

| Parameter | Function |
| --- | --- |
| Cut | Moves the high-pass and shard-band centers through the high spectrum |
| Edge | Raises nonlinear fold drive and band Q |
| Burst | Controls decay length and note-derived subdivision density |
| Scatter | Controls sparse velvet impulse probability and band spread |
| Alias Budget | Crossfades smoothed folded output toward raw fold energy |
| Stereo Split | Offsets right-channel seeds and band centers |
| Output | Final gain, -48 to +6 dB before safety bounding |

## Plugin Identity

| Surface | Value |
| --- | --- |
| App ID | `audio.2bit.shardnoise` |
| Plugin ID | `audio.2bit.ShardNoise` |
| AU subtype | `ShRd` |
| AU manufacturer | `2Bit` |
| Formats | Standalone, VST3, AUv2 on macOS |
| Type | Synth, stereo output, MIDI input |

## Requirements

- macOS 11 or newer
- Apple Clang with C++20 support
- CMake 3.31 or newer
- Ninja
- Xcode / macOS SDK for AU builds
- A local YUP checkout at `../yup`, or network access for the pinned fallback checkout

Windows CI uses Visual Studio 2022 on `windows-2025` for x64 Debug tests and Release Standalone/VST3 bundle builds.

YUP is pinned to commit `9a1c9bc699b6a714f6f52486462d98a140c8bf95` when the adjacent checkout is absent. YUP is ISC-licensed; its own license and all fetched dependency licenses remain authoritative.

## Build And Test

Fast DSP-only loop:

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug
ctest --preset engine-debug
```

Release app and plugins:

```sh
cmake --preset plugin-release
cmake --build --preset plugin-release --parallel
ctest --preset plugin-release
```

Artifacts:

- `build/plugin-release/shardnoise_standalone_plugin.app`
- `build/plugin-release/VST3/Release/shardnoise_vst3_plugin.vst3`
- `build/plugin-release/shardnoise_au_plugin.component`

CI uploads `ShardNoise-latest-macos-arm64.zip` and `ShardNoise-latest-windows-x64.zip`. Tags named `v*` create or update one GitHub Release with versioned macOS and Windows ZIPs attached.

Local installation is intentionally separate from the build:

```sh
cp -R build/plugin-release/VST3/Release/shardnoise_vst3_plugin.vst3 "$HOME/Library/Audio/Plug-Ins/VST3/"
cp -R build/plugin-release/shardnoise_au_plugin.component "$HOME/Library/Audio/Plug-Ins/Components/"
```

The local macOS build ad-hoc signs the standalone app and VST3 bundle. Distribution still requires your Developer ID signing and notarization workflow.

## Verification Covered

The engine regression test checks:

- identical output for identical note, velocity, and parameters;
- divergence between different note-derived seeds;
- silence before trigger and after decay;
- finite bounded output at extreme parameter values;
- safe fallback for NaN and infinite parameter/sample-rate inputs;
- high-frequency behavior distinguishable from low-passed energy.

## Current Limits

- The editor is functional and intentionally minimal; it does not yet visualize DSP state.
- Verified build artifacts are local machine outputs; host scanning, AU/VST3 validation, listening, and calibrated loudness tests remain host-specific follow-up work.
