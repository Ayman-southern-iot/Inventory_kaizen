# Whisper returns invented text instead of nothing

| | |
|---|---|
| **Domain** | `Inventory-voice-Redwan-Ayman` |
| **Date** | 2026-10-05 |
| **Task** | [firmware](../tasks/2026-10-05-esp32-p4-voice-firmware/README.md) |

## Symptom

A spoken question comes back as confident, fluent text that bears no relation to what
was said. Repetitive phrases are the giveaway:

```
captured -36.0 dBFS, peak 2912  -> "That's amazing, that's amazing."
captured -29.0 dBFS, peak 12278 -> "Thank you very much. Thank you very much."
captured -13.2 dBFS, peak 32767 -> "you are quite exciting, but you will get up with rotational."
```

Across ~45 recordings on real hardware, **one** transcript was correct.

## Cause

Whisper is an autoregressive language model with no "I heard nothing" output. Given an
encoder input it cannot parse as speech, the decoder emits the most statistically likely
text from its training data (~680k hours of web audio and captions). "Thank you very
much" and "That's amazing" are among the most common caption phrases in existence, so
they are what it falls back to. This is a documented Whisper artefact, not a fault
specific to this deployment.

Confirmed by measuring the captured audio off-board: speech-band energy (100-1600 Hz)
barely exceeded high-frequency hash (3200-7800 Hz) -- ratio **1.17**, where clean speech
is 3-10. Zero-crossing rate ran 4500-7500/s where speech is 500-3000/s.

Note the −13.2 dBFS case: **peak 32767 is hard clipping.** Too loud fails as badly as
too quiet, with a different flavour of invention.

## Fix

Not yet resolved on this hardware. The leading fix, untried at the time of writing, is
to route the microphone through **ESP-SR's AFE** (noise suppression + AGC + VADNet)
before upload. The sibling project that *did* work used AFE; this one sent raw mic audio,
which plausibly explains the whole failure.

Server-side levers that specifically suppress this, all `faster-whisper` parameters:
- `no_speech_threshold` -- return empty instead of guessing
- `condition_on_previous_text=false` -- stops a loop feeding itself
- `compression_ratio_threshold` -- detects exactly these repetitive outputs

## Tried and did not work

- **Microphone gain 30, 32 and 42 dB.** 42 dB lifted levels as intended but drove a
  capture to full-scale clipping and produced **0 of 14** correct transcripts; 32 dB
  produced the only correct one. Raising gain made it worse.
- **Mono vs stereo capture** (deinterleaving the ES8311's channel 0). Both fail, which
  ruled out a channel/aliasing artefact.
- **A level gate** (reject below -40 dBFS). Necessary but not sufficient -- hallucinations
  also occur at -26 to -30 dBFS.
- **A repetition filter** requiring 4+ repeats of the opening phrase. Too lenient; real
  cases loop only 2-3 times.

Eliminated as causes by direct measurement: WAV format (verified 1 ch / 16-bit / 16 kHz /
exact duration), DC offset (0), clipping (0% at 30 dB).

## Prevention

Never treat a transcript as trustworthy just because it is non-empty. A hallucination
reads as ordinary text and **will be parsed as a query** -- in this task
`"...and model is not one of them..."` matched a real product and produced a confident
wrong answer, which is worse than no answer. Gate on audio quality before upload, and
measure the audio objectively (band-energy ratio, zero-crossing rate) rather than
trusting level alone.
