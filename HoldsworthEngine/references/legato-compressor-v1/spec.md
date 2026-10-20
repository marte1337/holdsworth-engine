# Legato Compressor v1 — frozen M1 contract

Implementation contract approved 2026-10-06. User utility; no historical
authenticity claim. The defaults below are experimental implementation values;
no final DSP defaults have been musically accepted.

Status 2026-10-20: objective validation passed. Preliminary guitar audition was
encouraging and the user finds compression beneficial, but wants more playing
time before accepting its DSP settings. Final musical acceptance is DEFERRED.
Hotter inputs can attenuate the first weak legato note; the measured limitation
remains documented in README.md and has not been hidden by retuning defaults.

Placement: existing mono input-processing result -> independent compressor ->
exclusive Boost/Drive pedal domain -> gate detector/NAM. The existing 32-sample
pedal domain and NAM latency contract are unchanged, including Pedal Off.

Mono feedforward downward compression; causal 4 ms exponential power detector,
fixed 2:1 ratio, fixed 6 dB soft knee, maximum 6 dB reduction. Manual makeup
-6..+4 dB (default +3). No audio EQ, saturation, limiter, multiband, lookahead or
oversampling. Zero DSP latency. Startup Enable Off.

Independent thresholds: calibrated -36..0 dBu RMS (default -18), normalized
fallback -48..0 dBFS RMS (default -30). Calibration is the existing bridge's
actual availability, not an assumption that its default +12 dBu is measured.
Detector scale = hostToVoltsGain / selectedInputGain while calibrated, otherwise
1. Apply gain to the existing audible buffer without a voltage round trip.

Attack 8..30 ms (default 15), release 40..180 ms (default 60). These are 63.2%
one-pole time constants in the dB gain-reduction stage, measured separately
from RMS acquisition. Power updates per sample; control interval is round(fs /
2000) samples and survives callback boundaries. Curve and dB gain smoothing
update at this interval; amplitude interpolates across that interval. No further
optimization without profiling. Control and local bypass ramps are 10 ms.

For d = level - threshold, q = 0 below -3 dB, (d+3)^2/24 inside the knee,
and d/2 above +3 dB. Requested reduction = clamp(q,0,6). Output gain combines
interpolated reduction and manual makeup, bounded to +4 dB. Bypassed finite
samples return exactly; detector/reduction keep advancing. Silence recovers
reduction to zero, leaving manual makeup. Reset starts a fresh enable ramp.

The two threshold ramps are independent. A detector-unit change clears power
while retaining smoothed gain and interpolation, avoiding a discontinuity.
Scale changes within the same unit mode do not clear physical detector history.
Nonfinite audio becomes zero; finite overflow protection is numerical only.
Detector floor -180 dB; tiny internal power is flushed below 1e-30. No dither.

Fixtures are frozen in tests/LegatoCompressorFixtures.h. Harmonic carriers use
one theoretical RMS scale, never per-note normalization. Picked notes include a
2 ms onset and 8 ms decaying transient; legato uses the same 2 ms onset without
the transient. Nominal picked body -12 dBu. A 300 ms picked note precedes the
150 ms -24 dBu first weak note, the -8/-12/-16 dB sequence, and rapid sequences
of six independently measured weak notes at 20/40/60/80/120 ms spacings.
Alternating, 2 s decay, silence/note, seeded noise+50 Hz hum, and equal-level
82.4/110/220/440/1000 Hz controls complete the fixture set.

Primary benefit uses picked body 100..280 ms and first weak body 40..140 ms:
contrast improvement >=1.5 dB and positive absolute weak lift. Also report the
whole first weak note and first 5 ms; report every rapid weak note and flag
attenuation >0.5 dB. Compare first 3 ms pick peak with identical scalar makeup
(<1 dB attenuation nominally). Musical fixture renders use 50 ms silent
preroll to settle enable; enable-ramp measurements are separate. The scalar boost is validation infrastructure,
not a UI feature. Do not retune defaults to conceal synthetic limitations.

This contract was frozen through M1-M3. Commit and push of the experimental
feature branch were authorized on 2026-10-20 to preserve the work. Final
musical acceptance remains pending, and merging into main is not authorized.
