# Owlet Custom Build - Feature Manual

This manual covers the features added in this custom build of the Owlet firmware (branch `owlet-custom`), on top of
everything the Owlet fork already offers. It also lists what was removed and what happens to older songs and presets.

**Pad positions** are given as *column* (1-16, counted from the left) and *row* (1-8, counted from the bottom). The two
sidebar columns to the right of the grid are called the *left* (mute) and *right* (audition) sidebar column.
**"Press twice"** means pressing the same shortcut pad again while its menu is open, which opens the pad's second
layer.

In **horizontal menus** (several controls side by side on the OLED), the **Synth / Kit / MIDI / CV** buttons pick the
1st / 2nd / 3rd / 4th control, the **select encoder** changes its value, and **Scale / Cross Screen** turn the page.

## Contents

1. [Kick synth mode](#1-kick-synth-mode)
2. [Vowel filter](#2-vowel-filter)
3. [Custom LFO shapes and the shape editor](#3-custom-lfo-shapes-and-the-shape-editor)
4. [8-bar LFO sync](#4-8-bar-lfo-sync)
5. [Saturation editor](#5-saturation-editor)
6. [Wavefold editor](#6-wavefold-editor)
7. [Bass Mono](#7-bass-mono)
8. [EQ Mid band](#8-eq-mid-band)
9. [Table Shaper Tone](#9-table-shaper-tone)
10. [Fixes](#10-fixes)
11. [Removed features and compatibility](#11-removed-features-and-compatibility)
12. [Shortcut pad overview](#12-shortcut-pad-overview)

---

## 1. Kick synth mode

A third synth mode next to Subtractive and FM, built for kicks, toms and 808 basses. Its heart is two **pitch
envelopes** that run sample by sample, so even a sweep over several octaves in a few milliseconds stays smooth. The
regular pitch modulation only moves once per audio block, which turns fast sweeps into audible steps.

### Switching it on

Open **Synth mode** (pad column 7, row 5, or the Master menu) and choose **Kick**. A sound that becomes a Kick gets a
ready-to-play kick straight away:

- **Pitch envelope 1 (envelope 3):** +36 semitones, about 4 ms, curve +30. This is the click.
- **Pitch envelope 2 (envelope 4):** +12 semitones, about 60 ms, curve +10. This is the body.
- **Envelope 1:** attack 0, decay 28, sustain 0, release 28. That gives a decaying hit instead of a sustained note.
- **Oscillators** still at transpose 0 go **two octaves down** into kick range: a kit row's C3 becomes C1, 65.4 Hz.
  Sample oscillators keep their pitch.

### What each part does in Kick mode

| Part | Role in Kick mode |
|------|-------------------|
| Oscillator 1 and 2 | Any type, as in Subtractive mode: sine, triangle, square, saw, samples, wavetables, PHI and so on. **Sine, triangle, square and saw** render sample by sample, which gives perfectly smooth sweeps and FM. Other types follow the pitch envelopes once per audio block. |
| Oscillator transpose | Shown as **TUNE**, with the note and frequency the oscillator lands on at the reference note (the note a kit row plays), for example `C1` / `65.4`. |
| Modulator 1 and 2 | Sine FM modulators that **bend** the oscillators. See [FM in Kick mode](#fm-in-kick-mode). |
| Noise | Noise, or the **click sample**. See [Click sample](#click-sample). |
| Envelope 1 | Amplitude, as always. |
| Envelope 2 | A free modulation envelope, as always. |
| Envelope 3 and 4 | **Pitch envelopes** (Pitch / Time / Curve). |

### Pitch envelopes (envelopes 3 and 4)

Each pitch envelope sweeps every oscillator and modulator from *Pitch* semitones above the note down to the note
over *Time*, along *Curve*. The two envelopes **add up**: use one fast and short for the click and one slower for the
body.

| Control | Range | Notes |
|---------|-------|-------|
| **Pitch** | 0 to +96 semitones | Stored in the envelope's sustain param, so it can be modulated and automated. Velocity → Pitch is the classic. |
| **Time** | 0.5 ms to 2 s | Log scaled: fine control over the short times. |
| **Curve** | -50 to +50 | 0 = straight line. Positive values drop fast and settle slowly, the analog kick sound. Negative values start slowly and drop at the end. |

**Where to find them:**

- The **envelope 3 / envelope 4 menus** show Pitch, Time and Curve instead of attack, decay, sustain and release. The
  graph draws the actual sweep: the time axis is stretched so a 3 ms click and a 300 ms tail both show, and a dotted line
  marks the end of the sweep.
- **Pads:** press an envelope 1 pad twice for pitch envelope 1. The envelope 1 pads are in column 9:
  - **attack** pad (row 4) → **Pitch**
  - **decay** pad (row 3) → **Time**
  - **sustain** pad (row 2) → **Curve**
- The same works on the envelope 2 pads (column 10) for pitch envelope 2.

The envelope's level is also a modulation source (envelope 3 / 4). It is 1 at note-on and 0 when the sweep ends, so
you can, for example, open the filter along with the pitch drop.

**Affect Entire** works: hold it on a kit row and the change goes to every kit row.

### FM in Kick mode

The two modulators are sine oscillators that bend the phase of the per-sample oscillators (sine, triangle, square and
saw). They follow the pitch envelopes too.

- **Modulator 1** bends **oscillator 1**.
- **Modulator 2** bends **oscillator 2**. If modulator 2's **Destination** is set to **Modulator 1**, it bends modulator 1
  instead, making a two-operator stack into oscillator 1.
- **Level** sets the depth, up to two cycles of deviation at full level.
- **Feedback** adds up to half a cycle of self-modulation. Use it for growl and grit.
- **Transpose** sets the modulator's ratio to the swept pitch. Transpose 0 = 1:1 ratio, +12 = 2:1, and so on.

Use it for knock, growl and metallic attacks. Oscillator types that aren't rendered per sample (samples, wavetables,
PHI) are not bent by the modulators.

### Click sample

In Kick mode the noise oscillator can play a **sample** instead of noise: a click, a transient, a snap, or a layer of a
sampled kick. It plays on every note, from the start, at its own speed. Its volume is the **Noise** level, so
everything patched or automated on the noise level controls the click too.

- **Open it:** **Click sample** in the **Osc mixer** menu, or press the **Noise pad (column 3, row 8) twice**.
- **Select:** opens the file browser. Choose a WAV file to load it.
- **Shift + select:** back to plain noise.
- Any WAV works: 8 / 16 / 24 / 32-bit or float, mono or stereo (mixed to mono), any sample rate.
- The first 1.5 seconds are kept in RAM, which is plenty for a click layer. Longer files are cut and faded there.
- The file is saved with the sound and loads with it.

### Starting points

These are recipes to tweak from, not fixed presets:

- **Punchy kick:** keep the defaults you get when switching to Kick mode. Raise pitch envelope 1's Pitch for more
  click and shorten pitch envelope 2's Time for a tighter body.
- **808 boom:** one sine oscillator, a long envelope 1 decay and release, pitch envelope 2 at +5 to +7 semitones over
  150-300 ms. Set pitch envelope 1 to 0 or keep a tiny click.
- **Hard / distorted kick:** add modulator 1 at transpose 0 with some level and feedback. Then put the
  [Saturation editor](#5-saturation-editor) on top with Tone up, so the sub stays clean.
- **Tom:** tune the oscillators an octave or so higher and use a smaller pitch envelope 2: +7 to +12 semitones over
  80-150 ms.
- **Layered click:** load a short transient into the click sample and balance it with the Noise level.

### Compatibility

Sounds saved with the old *Kick oscillator* from earlier custom builds load in Kick mode automatically.

---

## 2. Vowel filter

A formant filter that turns any source into a vowel: detuned saws, FM growls, wavetables, samples, even basses. It
is a cascade of four resonators (formants F1-F4) with unity gain at DC, so whatever goes in keeps its low end. A bass
stays a bass; it just starts to talk. There is no noise and no randomness.

### Switching it on

Set **LPF mode** (pad column 9, row 6) to **Vowel**. It comes after Drive in the list. It works on synths, kit rows and
audio tracks.

### Controls

| LPF control | Pad | In Vowel mode |
|-------------|-----|---------------|
| **Frequency** | column 9, row 8 | Walks through eight vowels: **A, AE (Ä), E, I, UE (Ü), OE (Ö), O, U**. The display shows the nearest vowel instead of a number. Neighbours glide naturally into each other: the tongue rises from A to I, then the lips round from I to U. |
| **Resonance** | column 9, row 7 | Makes the formants narrower and more pronounced. |
| **Morph** (shown as **Formant shift** / SHFT) | column 9, row 5 | Moves all formants up to an octave higher, like a smaller "speaker". Negative modulation moves them lower. |

### Good to know

- Levels are calibrated so every vowel comes out about as loud as it went in.
- Coefficients glide sample by sample, so LFOs and envelopes on the vowel never zipper.
- A **presence band** keeps the top end (4-8 kHz) alive, so vowels stay clear and present even with Formant shift at 0.
- The horizontal filter menu draws the **actual frequency response**, up to 8 kHz.
- Try a [custom LFO shape](#3-custom-lfo-shapes-and-the-shape-editor) on Frequency to make a sound "speak" a vowel
  sequence. Or put an envelope on it for a "wow".
- The Vowel filter replaces the old *PHI Vox* oscillator.

---

## 3. Custom LFO shapes and the shape editor

LFOs get a new shape, **Custom**: points joined by segments you can bend, Serum style, with up to 64 points per cycle.
The shape is saved with the sound.

### Opening the editor

1. Press an LFO's **Shape** pad. LFO 1 is column 13, row 3 and LFO 2 is column 14, row 3. Press twice for LFO 3 / 4.
2. Set **Shape** to **Custom**.
3. In the LFO menu (Shape / Draw / Sync / Rate), select **Draw** with its Synth / Kit / MIDI / CV button. Then press
   **select**, or press that button again. The horizontal menu shows a small preview of the shape.

### The display

- **OLED:** the title shows the LFO, the selected point and the point count (`LFO1 P3/12`), with the page at the top
  right (`2/4`). Points are small squares; the selected one is bigger and filled.
- **Pads:** the main grid shows the visible part of the cycle as bars from the centre line. Points are cyan and the
  selected point is white.

### Controls

| Control | Action |
|---------|--------|
| **Pad** | Sets a point in that column at the pad's height. A point already in the column just gets the new height. |
| **Shift + pad** | Removes the points in that column. The first point of the cycle stays. |
| **X encoder** | Moves the selected point in time. Hold shift for finer steps. |
| **X encoder pressed + turned** | Zooms x1 / x2 / x4 / x8. The page shows at the top right and on the left sidebar column. |
| **Y encoder** | Selects the previous / next point. |
| **Y encoder pressed + turned** | Right: splits the visible part into ever more equal parts (2 / 4 / 8 / 16) by adding points along the shape. Left: takes the finest split back out. |
| **Shift + Y encoder press** | Loads the next preset: Triangle, Sine, Saw up, Saw down, Square. |
| **Upper gold knob** | Height of the selected point: 2 % per detent, 0.5 % with shift. |
| **Upper gold knob press** | Deletes the selected point. |
| **Lower gold knob** | Bends the segment after the selected point. Turning right always bows it upwards. |
| **Lower gold knob pressed + turned** | Bends the segment before the selected point. |
| **Synth / Kit** | Previous point. With shift: previous page while zoomed in. |
| **MIDI / CV** | Next point. With shift: next page while zoomed in. |
| **Select encoder** | Selects points, like the Y encoder. |
| **Shift + select encoder** | **Undo** (left) / **redo** (right), one step per detent. The popup shows where you are, e.g. `UNDO 3/7`. |
| **Left sidebar column** | Jumps to a page while zoomed in. The visible page is white. |
| **Right sidebar column, bottom pad (red)** | **Undo**. Bright while there's a step to undo. |
| **Right sidebar column, 2nd pad from the bottom (green)** | **Redo**. Bright while there's a step to redo. |
| **Back** or **select press** | Leaves the editor, as in every menu. |
| **Hold back** | Leaves the whole menu. |

### Undo and redo

- Up to 24 steps.
- Turning a knob on and on counts as **one** step.
- The history **survives leaving the editor**. Open it again on the same LFO and you can still undo, as long as nothing
  else changed the shape in the meantime (for example a preset load). Opening another LFO starts a fresh history.

### Tips

- The first point sits at the start of the cycle and stays there. The cycle wraps from the last point back to the first.
- For rhythmic shapes, zoom in, split the page into 8 or 16 parts with the Y encoder, then set heights with the pads.
- With the Rate synced to the song, a custom shape becomes a one-bar (or up to [8-bar](#4-8-bar-lfo-sync)) modulation
  sequence.

---

## 4. 8-bar LFO sync

Synced LFOs reach **8 bars**: one more, slowest setting in **LFO Sync**, right after *Off* and before 4 bars. That's
at the default song resolution; it is always twice the longest regular setting. It's saved as a separate flag, so
older firmware reads it as 4 bars.

---

## 5. Saturation editor

A full saturation editor on its own pad, **column 8, row 7**. That pad was unused before. It is also in the **FX**
menu and in the horizontal menu chain.

| Control | Range | What it does |
|---------|-------|--------------|
| **Saturation** (Drive) | 0-50, 0 = off | Steepness of the tanh curve. It has real character at low and moderate settings, not only when driven to the rails. |
| **Asymmetry** | 0-50, default 25 | Adds even harmonics on top of tanh's odd ones: 0 = plain symmetric tanh, higher = dirtier, more analog. Works at any drive. |
| **Mix** | 0-50, default 50 | Dry/wet. 50 = fully wet. |
| **Tone** | 0-50, default 0 | **Clean lows:** splits the signal before the saturation and keeps everything below the crossover undistorted. 0 = the whole signal is saturated; towards 50 the crossover rises to about 1.5 kHz. |

- Synth/Kit/MIDI/CV pick Saturation / Asymmetry / Mix / Tone.
- The original Saturation pad (column 7, row 8) controls the same drive.
- **Audio tracks:** Tone is in the audio track's **Distortion** menu.

---

## 6. Wavefold editor

The wavefolder gets its own editor on the **Fold pad (column 8, row 8)**. Press it twice to jump straight to
Symmetry. It is also in the **FX** menu and the horizontal menu chain. It runs per voice, so it is available on synths
and kit rows.

| Control | Range | What it does |
|---------|-------|--------------|
| **Wavefold** (Amount) | 0-50 | The same fold amount as before. |
| **Symmetry** | -25 to +25, 0 = symmetric | Offsets the signal before the fold, so the positive and negative half-waves land on different parts of the fold curve. That adds even harmonics. Silence in stays silence out, and a DC blocker removes the offset afterwards. Patchable in the mod matrix and automatable. |
| **Tone** | 0-50, default 0 | Clean lows, as in the saturation editor: the low end bypasses the folder. |

---

## 7. Bass Mono

Narrows the stereo width **below a crossover** towards mono, so stereo unison, chorus and wide pads keep a solid, mono
low end. It is in the **FX** menu of synths and kit rows, in the audio track **FX** menu, and in the horizontal menu
chains.

| Control | Range | What it does |
|---------|-------|--------------|
| **Bass mono** (Amount) | 0-50, 0 = off | How far the lows are pulled to mono. |
| **Bass mono freq** | 40-500 Hz, shown in Hz | The crossover frequency, log scaled. Default about 121 Hz. |

On audio tracks it also works while you monitor the input.

---

## 8. EQ Mid band

The EQ gets a third band, **Mid**, next to Bass and Treble. It works on everything that has the EQ: synths, kit rows,
kits, audio tracks and the song.

- **Mid** boosts or cuts the band **between Bass frequency and Treble frequency**. Move those two to place the mid
  band.
- 0 = no change, the same convention as Bass.
- Find it in the **EQ** menu: Bass / Treble / Bass frequency / Treble frequency / **Mid**. Open the EQ with the Bass or
  Treble pads (columns 11 and 12, rows 7-8). The EQ graph shows a mid point.

---

## 9. Table Shaper Tone

The Table Shaper (FX → Shaping → Table Shaper) gets a fifth control, **Tone** (0-50, default 0).

- Like the saturation Tone, it keeps the low end out of the shaper below a crossover. 0 = the whole signal reaches
  the shaper.
- Menu: Drive / Knee / Color / Mix / **Tone**.
- It's saved with the sound.

---

## 10. Fixes

- **Harm effect:** Level and Fine Tune are now saved with the sound. Before, they were lost on reload.
- **Freezes when memory runs low:** freezes in long sessions with many synths and kits loaded, and a PHI synth crashing
  on the first note after reopening a project. When memory runs low, the firmware now falls back to other memory
  instead of locking up, and a PHI oscillator that can't get memory plays silence instead of crashing.
- **Crash fixes from upstream Owlet**, among them:
  - the voice-cull fix;
  - audio track recording sources: no crash when the source track is deleted, and MIDI / CV tracks are skipped as
    sources;
  - a buffer overflow when naming new files;
  - MIDI Follow keeping a stale clip after a clip swap.
- **Custom LFO shapes with more than 25 points** now load correctly.
- **LFO shape editor:**
  - Shift no longer stays stuck after leaving.
  - Back leaves the editor again, with undo and redo moved to the sidebar pads and shift + select.

---

## 11. Removed features and compatibility

Removed to free up firmware memory (DX7 and Ringmod alone saved about 38 KB) or replaced by something better:

| Removed | What happens to older songs and presets |
|---------|-----------------------------------------|
| **DX7 engine:** the DX7 oscillator type, the DX7 cartridge browser, the keyboard DX column and the "Enable DX7 engine" setting | DX7 oscillators load as **Sine**. |
| **Ringmod** synth mode | Loads in **Subtractive** mode. |
| **Kick oscillator** (earlier custom builds) | Converted to the **Kick synth mode**. |
| **PHI Stair, PHI Weave, PHI Vox, PHI Gendy** oscillators | Load as **PHI Morph**. PHI Morph and PHI Swarm remain. |
| **Club filter** | Loads as **Off**. |

The synth mode menu now offers **Subtractive / FM / Kick**.

---

## 12. Shortcut pad overview

Only the pads that are new, changed or used by the features above:

| Pad (column, row) | Opens |
|-------------------|-------|
| 3, 8 (Noise) | Noise level. **Press twice in Kick mode:** click sample. |
| 7, 5 | Synth mode (Subtractive / FM / Kick). |
| 7, 8 | Saturation (drive), original Distortion menu. |
| 8, 7 | **Saturation editor** (new pad). |
| 8, 8 | **Wavefold editor**. **Press twice:** Symmetry. |
| 9, 2 / 3 / 4 | Envelope 1 sustain / decay / attack. **Press twice in Kick mode:** pitch envelope 1's Curve / Time / Pitch. |
| 10, 2 / 3 / 4 | The same for envelope 2 → pitch envelope 2. |
| 9, 5 / 6 / 7 / 8 | LPF Morph (Formant shift) / Mode (→ Vowel) / Resonance / Frequency (vowel). |
| 11-12, 7-8 | EQ (Bass, Bass frequency, Treble, Treble frequency), with Mid in the same menu. |
| 13 / 14, 3 | LFO 1 / LFO 2 Shape (→ Custom → Draw). **Press twice:** LFO 3 / LFO 4. |
