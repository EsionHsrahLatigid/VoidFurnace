# VoidFurnace

VoidFurnace is a YUP stereo reverb built for Digital Harsh Noise rather than polite ambience. Four unequal delay lines, normalized Hadamard feedback mixing, frequency-dependent loop damping, sparse deterministic velvet impulses, stereo decorrelation, and bounded drive form a dark metallic chamber. Hosted builds preserve silence; Standalone adds an audition source and meters only at compile time.

## Identity and formats

- App/plugin ID: `jp.ehl.voidfurnace`
- Vendor: `ehl_`; AU manufacturer: `EHL1`; AU subtype: `VdFn`
- Version: `0.1.0`
- macOS: Standalone, VST3, AUv2
- Windows: Standalone, VST3
- Stereo effect, no MIDI

## Parameters

- `PreDelay`: 1–96 ms input-to-network offset.
- `Size`: scales the four mutually unequal delay lengths.
- `Decay`: bounded feedback-network regeneration.
- `Damping`: one-pole frequency loss inside each line.
- `Density`: injection level and sparse velvet-impulse density.
- `Diffusion`: Hadamard feedback amount and stereo width.
- `Mix`: dry/wet blend.

## Research basis

The reverb structure follows the unitary feedback-delay-network model described in [Schlecht and Habets, 2017](https://www.sebastianjiroschlecht.com/publication/schlecht-2017-il/) and the delay-network principles in [Physical Audio Signal Processing: Feedback Delay Networks](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html). Sparse deterministic diffusion is informed by [Dark Velvet Noise](https://research.aalto.fi/en/publications/dark-velvet-noise/). VoidFurnace's signal-activity gating, four-line voicing, saturation, and stereo output matrix are deliberate product choices.

## Build and artifacts

Clone with `--recurse-submodules`, or initialize the shared [yup-ehl-design-module](https://github.com/EsionHsrahLatigid/yup-ehl-design-module) before configuring:

```sh
git submodule update --init
```

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug --parallel
ctest --preset engine-debug --output-on-failure

cmake --preset plugin-release
cmake --build --preset plugin-release --parallel
ctest --preset plugin-release --output-on-failure
```

Human-facing products are staged under `artifacts/plugin-release/<platform-arch>/` in `standalone/`, `vst3/`, and macOS `au/`. For local macOS non-CI `plugin-release` builds, the staged VST3 and AU bundles are also physically copied into `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`; the Standalone app stays under `artifacts/`. Configure with `-DEHL_COPY_PLUGIN_AFTER_BUILD=OFF` to disable the local plugin copy. `build/` is internal compiler state.

## CI and release

The caller workflows pin `EsionHsrahLatigid/yup-actions` to a full commit SHA. CI runs deterministic tests and Release product staging on macOS arm64 and Windows x64, then creates checksummed latest ZIP artifacts. A `v*` tag promotes artifacts from the successful `main` CI run for that exact commit without rebuilding.

## Safety contract

The audio callback allocates no memory and performs no locks, I/O, logging, or UI work. Parameters and non-finite input are sanitized; the Hadamard matrix is normalized in the feedback write; velvet impulses require input activity; output stays finite within `+/-0.98`. Early-arrival, reverb-tail, density/diffusion, deterministic-render, extreme-value, hosted-silence, state, and Standalone-audition tests cover the contract.
