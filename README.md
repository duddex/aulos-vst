# Aulos — VST3 / Standalone

A polyphonic **physical-model wind-instrument synthesizer**, ported from the
[CV funk](https://github.com/codygeary/CVfunk-Modules) *Aulos* [VCV Rack](https://vcvrack.com/) module
(by Cody Geary) to a standalone **VST3** and **desktop app** using
[JUCE](https://juce.com).

> **About this port:** This is an independent port of an original
> [VCV Rack](https://vcvrack.com/) module, developed entirely with the
> assistance of large language models by **duddex**. All credit for the
> instrument — its synthesis, design, and sound — belongs to its creator,
> **Cody Geary / CV funk**. If you enjoy Aulos, please support him by purchasing
> the [CV funk premium modules](https://library.vcvrack.com/?page=1&limit=50&query=&tag=&sort=creationTimestamp&brand=CV%20funk&license=premium&modules=&plugins=).

Aulos is built on a **bidirectional digital waveguide**: two traveling-wave
rails run the length of a bore and reflect into one another at the embouchure
and the bell. The `Reed` and `Bore` controls reshape those reflections directly,
changing the instrument's actual boundary conditions and harmonic series — from
a breathy open-ended flute, through a hollow odd-harmonic clarinet, into a
flared brass horn — rather than just filtering a fixed tone.

The full synthesis engine is identical to the Rack module; only the I/O layer
changed (CV/gate → MIDI, panel → JUCE editor).

---

## Features

- **VST3** plugin + **Standalone** application (Windows).
- Up to **16-voice** polyphony with MIDI note allocation and voice stealing.
- **Expressive MIDI**:
  - Note **velocity** → per-voice breath (soft = airy, hard = full/overblown).
  - **CC 2** (breath controller) → breath pressure.
  - **CC 1** (mod wheel) → vibrato depth.
  - **Pitch bend** → ±2 semitones.
  - **CC 64** (sustain pedal) → holds notes.
- **26 automatable parameters** (all state saved with the project).
- Animated **pipe visualizer** — the two-tube standing-wave / bore / breath
  display from the original module, redrawn in JUCE.
- **9 factory presets** (Flute, Recorder, Clarinet, Oboe, Brass, drones…).
- Passes **pluginval** strictness level 8.

See [docs/Aulos.md](docs/Aulos.md) for the full control reference.

---

## Repository layout

```
aulos-vst/
├─ dsp/                     Framework-agnostic engine (no VCV Rack, no JUCE)
│  ├─ AulosDSP.h            Rack-free port of the module's FilterAulos.h
│  ├─ AulosEngine.h/.cpp    Voice + process loop, plain-float I/O
├─ juce/                    JUCE plugin wrapper
│  ├─ CMakeLists.txt        VST3 + Standalone target (fetches JUCE)
│  ├─ PluginProcessor.*     MIDI → voices, params, state, presets
│  └─ PluginEditor.*        Faceplate GUI + pipe visualizer
├─ test/                    Offline render harness
│  └─ render_wav.cpp        Renders a note sequence to WAV (engine sanity check)
├─ tools/                   pluginval.exe (downloaded)
├─ docs/Aulos.md            Control reference & usage
├─ CMakeLists.txt           Builds the offline harness
├─ LICENSE                  AGPLv3 (this port)
├─ NOTICE                   Third-party component notices
└─ licenses/                Aulos MIT notice (Cody Geary / CV funk)
```

The `dsp/` engine has **zero** framework dependencies, so it can be reused in
another host (CLAP, AU, an offline renderer, etc.).

---

## Building

### Requirements
- **Visual Studio 2022/2026** with the C++ toolset (**MSVC is required** — JUCE 8
  dropped MinGW support).
- **CMake ≥ 3.22** and a generator (Ninja recommended).
- Internet access on first configure (JUCE is fetched via CMake `FetchContent`).

### Plugin (VST3 + Standalone)
From a **Developer** shell (or after calling `vcvars64.bat`):

```powershell
cd juce
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artifacts:
- `juce/build/Aulos_artefacts/Release/VST3/Aulos.vst3`
- `juce/build/Aulos_artefacts/Release/Standalone/Aulos.exe`

### Offline engine harness (optional)
A tiny, framework-free sanity check for the `dsp/` engine — it renders a short
phrase to a WAV without JUCE, useful for regression-testing the DSP in
isolation. Builds with MinGW or MSVC; the generated `.exe` and `.wav` are
throwaway (git-ignored):

```powershell
g++ -std=c++17 -O2 -Idsp test/render_wav.cpp dsp/AulosEngine.cpp -o render_wav.exe
./render_wav.exe
```

### Install
Copy `Aulos.vst3` to your VST3 folder:
`%CommonProgramFiles%\VST3` (e.g. `C:\Program Files\Common Files\VST3`).

### Validate
```powershell
tools\pluginval.exe --strictness-level 8 --validate <path>\Aulos.vst3
```

---

## Credits & license

- Original *Aulos* module, synthesis design, and DSP: **Cody Geary / CV funk** —
  MIT License. All credit for the instrument and its sound belongs to the
  original author. Manual: <https://codygeary.github.io/CV-Funk-Manual/free/>
- This VST3/Standalone port keeps the engine intact and adds a JUCE host layer.
- JUCE is used under its **AGPLv3** option (see Licensing below).

## Licensing

This port is released under the **GNU Affero General Public License v3.0
(AGPLv3)** — see [LICENSE](LICENSE). The complete corresponding source code is
therefore made available to anyone who receives the software.

It combines three components (full details in [NOTICE](NOTICE)):

| Component | Author | License here |
|---|---|---|
| Aulos synthesis engine (`dsp/`) | Cody Geary / CV funk | **MIT** — [licenses/Aulos-CVfunk-MIT.txt](licenses/Aulos-CVfunk-MIT.txt) (MIT is compatible with AGPLv3) |
| JUCE framework | Raw Material Software | **AGPLv3 option** of JUCE's dual license |
| VST3 SDK (bundled by JUCE) | Steinberg | dual GPLv3 / Steinberg; used under GPL-compatible terms |

Because JUCE is used under its **AGPLv3** option, the whole plugin is AGPLv3 and
its source must be distributed with it. The MIT license and attribution for the
Aulos engine are preserved. "VST" is a trademark of Steinberg Media Technologies
GmbH.

> If you redistribute this software (source or binary), you must pass along the
> complete source code and keep the `LICENSE`, `NOTICE`, and `licenses/` files.

