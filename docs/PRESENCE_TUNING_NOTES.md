# Presence Tuning Notes

## Intent

This tuning pass targets a musical "foreground clarity + background bloom" behavior:

- **Transient:** prioritize intelligibility of the dry source by reducing wet prominence.
- **Immediate post-attack:** deliberately raise wet presence so the texture blooms after the initial hit.
- **Sustain:** keep a stable, perceptible halo behind the source.
- **Decay:** thin the wet layer while preserving an audible tail.

The implementation is deterministic, CPU-light, and entirely based on existing low-cost primitives (one-pole filters, scalar gains, and state machine logic).

## Internal Bus Gain Weights (non-UI)

The engine now applies internal class-weighted scaling to its three wet buses:

- `micro` (attack-focused): **1.15**
- `short` (intermediate): **0.96**
- `sustain` (body/tail): **0.78**

These defaults are intentionally conservative to keep output headroom while increasing class contrast.

## State-Dependent Wet Presence Shaping

A control-rate wet-presence target is assigned by `EngineState_t` and smoothed each control tick:

- `TRANSIENT_BURST` → **0.58** (dry attack clarity)
- `ATTACK_ONGOING` → **1.28 → 1.02** (bloom window interpolation)
- `SUSTAIN_BODY` → **0.88** (stable halo)
- `SPARSE_DECAY` → **0.54** (thinner tail)
- `SILENCE` → **0.32** (residual ambience if voices remain)

### Bloom Window

On transient detection, a deterministic `bloom_timer_ticks` is set (`PRESENCE_BLOOM_TICKS = 32`).
During `ATTACK_ONGOING`, wet presence ramps from a deliberate overshoot toward a steady level,
creating an audible post-attack bloom without adding heavy DSP or stochastic timing.

## Spectral Contrast Strategy

To improve spectral separation using existing components:

- **Micro bus** keeps the fundamental and low-mid body via a gentle ~300 Hz HPF (M3.2A); it stays defined without collapsing into a thin "click".
- **Sustain bus** stays slightly darker than the attack via a dynamic ~3.5–7 kHz 1-pole LPF (M3.2A) that opens on attacks and closes through decay and with higher WARMTH, with subtle stage-dependent tilt.

This keeps the onset crisp while letting sustain content sit deeper in the mix, without the two buses sounding like disconnected sources.

## CPU and Determinism Notes

- No platform-specific instructions or non-deterministic math were introduced.
- Runtime additions are scalar multiplies, a tiny state counter, and a one-pole control smoother.
- All state transitions continue to be driven by existing deterministic envelope/derivative logic and fixed constants.
