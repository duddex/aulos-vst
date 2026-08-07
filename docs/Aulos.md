# Aulos — Control Reference

A polyphonic physical-model wind synthesizer. This document adapts the
[original CV funk manual](https://codygeary.github.io/CV-Funk-Manual/free/) to
the VST3/Standalone build, where CV/gate inputs are replaced by MIDI and the
panel becomes automatable parameters.

---

## How it works

Aulos is a **bidirectional digital waveguide**. Two traveling-wave rails run the
length of the bore and reflect into one another at both ends:

- The **embouchure** end (where you blow) and the **bell** end (where sound
  radiates). `Reed` and `Bore` reshape these two reflections directly, so they
  change the instrument's real boundary conditions and harmonic series — not a
  filter or a mix.
- **Reed** morphs the exciter *and* the embouchure reflection together. Near 0 it
  is an **air-jet** exciter with an open-end reflection (a flute's full harmonic
  series). Near 1 it is a **beating reed** with a closed-end reflection (an
  odd-harmonic clarinet-like tone roughly an octave lower). The middle is a
  deliberately non-physical, characterful crossover.
- **Bore** reshapes the bell using a horn-cutoff model. Through the lower ~75% of
  its range the tube widens like a clarinet body (fuller harmonics without
  brightening); in the top quarter it blooms into a flared **brass** character
  with a waveform-steepening nonlinearity.
- **Register** is set by the ratio of the played (finger) pitch to the bore's
  resonant (pipe) pitch. Play near 2× and the bore overblows to the octave; near
  4×, another octave. Very loud playing can also push the register up on its own
  (**energy overblow**). The pipe visualizer mirrors all of this live.

In this port, the played MIDI note drives **both** the pipe (resonant) pitch and
the finger (fingering) pitch, so notes normally play at their fundamental. The
`Octave` control and pitch bend transpose both together.

---

## Parameters

### VOICE
| Control | Range | Description |
|---|---|---|
| **Reed** | 0–1 | Exciter/reflection morph, flute → reed. Low = airy open-end jet (full harmonics); high = buzzy closed-end reed (odd harmonics, ~1 octave lower, stable). Middle = unnatural crossover with its own voice. |
| **Bore** | 0–1 | Bell/horn shape, cylindrical → flared brass. Below ~0.75 the body widens and fills out; above ~0.75 a brass flare brightens and reshapes the harmonics. |
| **Drive** | 0–1 | Gain into the excitation-stage saturator. Adds harmonic richness at the *source* only (the tube stays linear), so it won't destabilize the loop. |
| **Res** | 0–1 | Loop feedback / sustain (the module's *Lip/Holes*). Higher = more resonant and sustaining. If it squeals or self-oscillates, turn this down. |
| **Air** | 0–1 | Continuous breath-noise amplitude. Shown as the streaming air lines at the bell. |
| **Chiff** | 0–1 | Attack-transient "tongued" burst on each note-on. 0 = none; high = long/loud. Shown as the outer line bursts at the bell. |
| **2nd Pipe** | −1…+1 oct | Pitch offset of the secondary (R) voice/pipe. 0 = unison; +0.585 ≈ a fifth; +1 = an octave. |
| **Wood** | 0–1 | Bore damping — a low-pass inside the loop from bright (0) to dark (1), scaled with register so high notes keep body. |

### RESONATOR / ENVELOPE
| Control | Range | Description |
|---|---|---|
| **WG Gain** | 0–1.75 | Waveguide input gain — overall level/balance into the resonator. |
| **Decay** | 0–1 | Ring-off time after the note is released. |
| **Attack** | 0–1 | Breath-envelope attack time (exponential mapping). |
| **Release** | 0–1 | Breath-envelope release time. |
| **Atk Crv** | −1…+1 | Attack curve: negative = logarithmic, positive = exponential. |
| **Rel Crv** | −1…+1 | Release curve. |
| **Follow** | 0–1 | Integration time of the RMS dynamics follower. |

### VIBRATO
| Control | Range | Description |
|---|---|---|
| **Rate** | 3–12 Hz | Internal vibrato LFO speed. |
| **Breath** | 0–1 | How much the LFO wobbles breath alongside pitch. |
| **Depth** | 0–1 | Vibrato depth. The **mod wheel (CC 1)** adds to this. |

### MIX
| Control | Range | Description |
|---|---|---|
| **Breath** | 0–1 | Breath pressure — the driving force behind both exciters. At 0 the instrument is **silent** (not a static tone); past a light threshold it speaks and blooms over the attack. Higher = louder and pushes into overblow. **CC 2** and note velocity add to this. |
| **Volume** | 0–1 | Output gain (0–3×). |
| **Octave** | −2…+2 oct | Global transpose of pipe + finger pitch. |

### ARTICULATION
| Control | Range | Description |
|---|---|---|
| **Legato ms** | 5–80 ms | Portamento glide time used when Legato is on. |
| **Legato** | on/off | When on, pitch changes while notes overlap **glide** rather than retrigger; the loop briefly dips feedback for the audible "give" of a slur. |
| **2nd Track** | on/off | Secondary-voice mode. **On** = Tracking (interval preserved across registers). **Off** = Two Pipes (the R voice finds its own register). |
| **Drone** | on/off | Locks the secondary voice's gate permanently on (sustains under the melody, slightly ducked). Pairs with `2nd Pipe` for a harmony drone. |
| **Man Gate** | on/off | Holds the gate high for all voices with no MIDI note — useful for tuning/sound-design. |

---

## MIDI mapping

| MIDI | Effect |
|---|---|
| Note on/off | Voice on/off; note number sets pitch (note 60 = C4). |
| Velocity | Per-voice breath scale (soft = airy/quiet, hard = full, more overblow). |
| **CC 2** (breath) | Adds to the Breath parameter. |
| **CC 1** (mod wheel) | Adds to Vibrato Depth. |
| **CC 64** (sustain) | Holds released notes until the pedal lifts. |
| Pitch bend | ±2 semitones. |

---

## Factory presets

| Preset | Character |
|---|---|
| **Init** | Neutral default. |
| **Flute** | Breathy open-tube flute, gentle vibrato, legato. |
| **Recorder** | Small bright pipe, more air and chiff, non-legato. |
| **Clarinet** | Reed exciter, hollow odd-harmonic tone. |
| **Oboe** | Hard reed, wider bore, reedy and present. |
| **Brass Bore** | High Bore into the brass flare, bright and bloomed. |
| **Breathy Whistle** | Pure jet, lots of air/chiff, whistly. |
| **Pan Flute Drone** | Melody + sustained secondary a fifth up (Drone on). |
| **Two Pipes (5th)** | Two independent pipes a fifth apart (Tracking on). |

Presets are selectable from the combo box in the plugin header. They set the
sound-shaping parameters and leave Volume/Octave and the advanced envelope
curves under your control.

---

## Usage tips

- **Basic wind melody**: keep `Reed` low for a flute, `Drive` ~0.5, add `Chiff`
  for articulation. Raise `Breath` to play louder / overblow.
- **Reed lead**: push `Reed` high; add `Bore` past 0.75 for a brighter,
  brass-leaning reed.
- **Register playing**: because the played note drives both pitches, notes play
  at their fundamental; use `Octave`/bend to move around. (Advanced independent
  pipe-vs-finger tuning from the Rack module is folded into the single note here.)
- **Bore sweep**: automate `Bore` 0→1 for a continuous morph a real fixed bore
  could never do — body fills out, then blooms brassy in the last quarter.
- **Drone harmony**: set `2nd Pipe` to an interval (+0.585 = fifth, +1 = octave)
  and enable `Drone`; the secondary voice sustains under the melody.
- **Stability**: if it squeals, lower `Res` or `Drive`.

---

## Differences from the Rack module

- CV/gate inputs are replaced by **MIDI**; attenuverter + CV jacks are not
  exposed (host automation covers modulation instead).
- The `AULOS` slider is labeled **2nd Pipe**; `Lip/Holes` is **Res**.
- The Rack `ENV` and `RMS` output jacks are not exposed (no CV outputs in a
  synth plugin).
- Stereo out = primary voice bank (L) + secondary/drone bank (R), decorrelated
  for natural width.
- Context-menu options are exposed as normal automatable parameters/toggles.
