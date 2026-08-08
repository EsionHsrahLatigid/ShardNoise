# Design

## Source Of Truth

- Status: Active
- Last refreshed: 2026-08-08
- Primary product surfaces: macOS Standalone, VST3 editor, AUv2 editor
- Evidence reviewed: `README.md`, `.github/workflows/ci.yml`, `.github/workflows/release.yml`, `source/violent/plugins/ShardNoisePlugin.*`, `source/violent/ParameterGridEditor.*`, `include/violent/ShardNoiseEngine.h`, `tests/ShardNoiseEngineTests.cpp`, `tests/ShardNoisePluginTests.cpp`

## Brand

- Personality: sharp binary pressure, exposed deterministic machinery, restrained threat
- Trust signals: deterministic note seeds, visible values, finite output ceiling, reproducible presets
- Avoid: decorative glitch noise, fake terminal text, unreadable labels, historical provenance claims

## Product Goals

- Goals: expose a MIDI-triggered high-frequency shard synth; make every structural control legible; keep extreme monitoring recoverable
- Non-goals: emulate undisclosed artist workflows; hide sound design behind broad macro labels; depend on runtime random devices
- Success signals: note changes produce deterministic structural changes; controls produce audible and repeatable variation; no crashes or non-finite output

## Personas And Jobs

- Primary personas: experimental electronic musicians, sound designers, noise performers
- User jobs: trigger repeatable shards from MIDI or the standalone audition gate; automate density, fold, alias, and stereo behavior; preserve preset states across hosts
- Key contexts of use: loud monitoring, DAW automation, standalone improvisation, rapid MIDI pattern exploration

## Information Architecture

- Primary navigation: one-page instrument panel
- Core routes/screens: parameter grid with a compact trigger/meter strip
- Content hierarchy: standalone trigger and output activity at the header edge; spectral placement and edge first, burst/scatter density next, alias/stereo/output last

## Design Principles

- Expose the mechanism: labels and values name actual DSP controls.
- Excess through structure: deterministic binary/velvet sources and filter/fold topology define the sound.
- Preserve a safe exit: output remains bounded and every non-finite input clamps safely.
- Tradeoffs: the editor stays compact, but labels and values must remain readable in plugin and standalone surfaces.

## Visual Language

- Color: near-black field, warning orange accent, neutral gray control chrome
- Typography: compact system sans for controls; numeric values remain high contrast
- Spacing/layout rhythm: equal square rotary bounds in a fixed-ratio parameter grid
- Shape/radius/elevation: hard rectangles and circular controls; minimal shadows
- Motion: bounded UI polling only; no decorative flicker
- Imagery/iconography: future visuals should reflect trigger envelopes, shard bands, or deterministic seeds

## Components

- Existing components to reuse: YUP `Slider`, `Label`, `TextButton`, `AudioProcessorEditor`
- New/changed components: momentary trigger button and output activity meter
- Variants and states: standalone gate held/released, triggered shard, released tail, preset selection, host automation
- Token/component ownership: editor-local constants until YUP exposes a stable theme/token workflow

## Accessibility

- Target standard: practical desktop accessibility within current YUP capabilities
- Keyboard/focus behavior: Spacebar mirrors the momentary trigger while the editor has keyboard focus; no hidden pointer-only mode switches
- Contrast/readability: labels and numeric values remain readable against the dark field
- Screen-reader semantics: constrained by current YUP accessibility support; control names must remain explicit
- Reduced motion and sensory considerations: no full-screen flashes; future animation must be state-driven and disableable

## Responsive Behavior

- Supported breakpoints/devices: desktop plugin windows and macOS Standalone
- Layout adaptations: fixed aspect ratio; parameter count determines grid occupancy
- Touch/hover differences: rotary vertical drag remains the primary interaction; hover is nonessential

## Interaction States

- Loading: immediate deterministic initialization
- Empty: silent until MIDI note-on or standalone trigger
- Error: invalid/non-finite parameter values clamp safely
- Success: parameter values update visibly and audio changes deterministically
- Disabled: no hidden disabled controls
- Offline/slow network: no runtime network dependency

## Content Voice

- Tone: technical, terse, direct
- Terminology: use concrete controls such as cut, edge, burst, scatter, alias budget, and stereo split
- Microcopy rules: short noun phrases; never imply historical provenance for reference aesthetics

## Implementation Constraints

- Framework/styling system: C++20 and YUP GUI/audio processor modules
- Design-token constraints: current YUP slider styling is theme-owned; values use separate labels
- Performance constraints: no allocation, file access, locks, or non-deterministic calls on the audio thread; UI trigger commands and output meter state cross threads only through processor-owned atomics
- Compatibility constraints: macOS arm64 currently targeted; state version changes require backward-compatible migration
- Test/screenshot expectations: engine regression tests, plugin bridge trigger tests, three-format build/signature checks, Standalone launch and screenshot inspection

## CI And Release Provenance

- CI cost policy: trigger CI only on `main` pushes, pull requests, and manual dispatch; classify docs-only changes before allocating macOS and Windows runners; keep a summary job as the required stable result.
- Heavy build policy: run macOS arm64 and Windows x64 bundle/test jobs only for build-impacting changes or manual forced runs.
- Artifact policy: upload `ShardNoise-latest-macos-arm64` and `ShardNoise-latest-windows-x64` artifacts with a 14-day retention window; each artifact must include one ZIP and a strict `SHA256SUMS.txt` line for that ZIP.
- Release policy: never build in the tag workflow. Resolve the normalized semver tag to a commit SHA, require the same version in CMake, require one successful `main` push CI run at the exact `head_sha`, verify both unexpired artifact IDs, validate SHA256 and ZIP integrity, upload exact draft assets, then publish.

## Open Questions

- [ ] Should the output activity meter later become a calibrated loudness/true-peak meter, or stay a lightweight performance indicator?
