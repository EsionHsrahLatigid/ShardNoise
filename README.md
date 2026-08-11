# ShardNoise

ShardNoise is a MIDI-triggered high-frequency binary/velvet noise instrument built with [YUP](https://github.com/kunitoki/yup). It treats MIDI notes as deterministic trigger seeds and burst subdivision inputs, not oscillator pitch.

The project builds as a standalone app and VST3 on macOS and Windows, plus Audio Unit v2 on macOS. Its compact editor exposes all seven engine parameters, a built-in momentary standalone trigger, output activity metering, four presets, versioned state save/load, and stable host automation IDs.

## Sound Engine

The pure C++20 engine is allocation-free in the audio path and produces repeatable stereo shards from deterministic binary and sparse velvet generators. A note-on reseeds the generators, resets the short burst envelope, and drives a bank of high-pass and band-pass filters into fold stages with optional alias emphasis.

Output is clamped to finite bounded samples, and parameter inputs are sanitized internally so NaN, infinite, and out-of-range values cannot escape the DSP boundary.

The editor trigger does not inject MIDI into the host path. It writes processor-owned atomic gate commands that the audio thread consumes before MIDI events in the render loop; external MIDI input remains enabled.

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

## Standalone Trigger And Meter

- `Trigger` is a visible momentary gate for quick standalone auditioning.
- Spacebar mirrors the same gate while the editor has keyboard focus.
- The output meter is an activity/peak indicator polled by the editor timer from the processor.
- External MIDI note-on/note-off remains the primary trigger path for DAWs and hardware controllers.

## Plugin Identity

| Surface | Value |
| --- | --- |
| App ID | `jp.ehl.shardnoise` |
| Plugin ID | `jp.ehl.shardnoise` |
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

Windows CI uses Visual Studio 2026 on `windows-2025` for x64 Debug tests and Release Standalone/VST3 bundle builds.

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

- `artifacts/plugin-release/macos-arm64/standalone/shardnoise_standalone_plugin.app`
- `artifacts/plugin-release/macos-arm64/vst3/shardnoise_vst3_plugin.vst3`
- `artifacts/plugin-release/macos-arm64/au/shardnoise_au_plugin.component`

CI runs only for `main` pushes, pull requests, and manual dispatch. A path classifier skips the macOS and Windows bundle jobs for docs-only changes while still publishing a stable summary job result. Heavy CI uploads `ShardNoise-latest-macos-arm64` and `ShardNoise-latest-windows-x64` artifacts for 14 days; each artifact contains one latest ZIP plus `SHA256SUMS.txt`.

Release tags do not build. The Release workflow accepts a pushed `v*` tag or a manually supplied tag, normalizes it to `vMAJOR.MINOR.PATCH`, resolves annotated tags to the exact commit SHA, checks that `CMakeLists.txt` declares the same `project(ShardNoise VERSION ...)`, and promotes only the unique successful `main` push CI run whose `head_sha` matches that tag commit. Both unexpired artifact IDs must be present, their strict SHA256 manifests must validate, the payload ZIPs must pass `unzip -t`, and the draft release must contain exactly:

- `ShardNoise-<version>-macos-arm64.zip`
- `ShardNoise-<version>-windows-x64.zip`

Local installation is intentionally separate from the build:

```sh
cp -R artifacts/plugin-release/macos-arm64/vst3/shardnoise_vst3_plugin.vst3 "$HOME/Library/Audio/Plug-Ins/VST3/"
cp -R artifacts/plugin-release/macos-arm64/au/shardnoise_au_plugin.component "$HOME/Library/Audio/Plug-Ins/Components/"
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
- plugin bridge behavior where the synthetic standalone trigger produces nonzero audio through `AudioProcessContext`;
- external MIDI still triggers through the processor after adding the standalone gate path.

## Current Limits

- The editor meter is an output activity indicator, not a calibrated loudness or true-peak meter.
- Verified build artifacts are local machine outputs; host scanning, AU/VST3 validation, listening, and calibrated loudness tests remain host-specific follow-up work.
