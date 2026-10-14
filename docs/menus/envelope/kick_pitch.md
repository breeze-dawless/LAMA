# Kick pitch envelope (envelopes 3 and 4 in the Kick synth mode)

In the **Kick** synth mode, envelopes 3 and 4 are pitch envelopes instead of attack / decay / sustain / release. Each sweeps every oscillator and modulator from **Pitch** semitones above the note down to the note over **Time**, along **Curve**. The two add up: use one fast and short (the click, e.g. +40 semitones in 4 ms) and one slower (the body, e.g. +12 semitones in 60 ms). The sweep runs per sample, so even octaves in a few milliseconds stay smooth.

- **Pitch**: 0 to +96 semitones. Stored in the envelope's sustain param, so it can be modulated - velocity → pitch is the classic.
- **Time**: 0.5 ms to 2 s.
- **Curve**: -50 to +50. 0 is a straight line, positive drops fast and settles slowly (the analog kick sound), negative starts slowly and drops at the end.

The envelope's level (1 at note-on, 0 when the sweep has ended) is also available as a modulation source (envelope 3 / 4), for example for the filter.

Available when the synth mode is Kick.
