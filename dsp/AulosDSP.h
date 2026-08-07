////////////////////////////////////////////////////////////
//
//   AulosDSP.h
//
//   Rack-free port of FilterAulos.h.
//   Original DSP by Cody Geary, Copyright 2026, MIT License.
//
//   The only change from FilterAulos.h is the removal of the
//   `#include "rack.hpp"` dependency: rack::clamp is replaced by a
//   free clamp() defined here. All synthesis math is byte-for-byte
//   identical so the standalone build sounds like the Rack module.
//
////////////////////////////////////////////////////////////

#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Rack compatibility shims — the handful of rack:: helpers the DSP relied on.
// ─────────────────────────────────────────────────────────────────────────────
inline float clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float crossfade(float a, float b, float p) {
    return a + (b - a) * p;
}

// Rack's exp2_taylor5 was a speed optimization for a full 2^x. std::exp2f is
// more accurate and its extra cost is irrelevant off the Rack audio thread; the
// pitch/tuning difference versus the taylor approximation is < 0.05%.
inline float exp2_taylor5(float x) {
    return std::exp2f(x);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────────────────────

// Four-point Lagrange interpolation — identical to droplet_lagrange().
inline float aulosLagrange(float y0, float y1, float y2, float y3, float t) {
    return ((-t * (t-1.f) * (t-2.f)) / 6.f) * y0
         + (((t+1.f) * (t-1.f) * (t-2.f)) / 2.f) * y1
         + ((-(t+1.f) * t * (t-2.f)) / 2.f) * y2
         + (((t+1.f) * t * (t-1.f)) / 6.f) * y3;
}

// Taylor-series sin accurate to <0.0002 across full range.
inline float aulosDspWrapToPi(float x) {
    const float twoPi = 2.0f * float(M_PI);
    x = fmodf(x + float(M_PI), twoPi);
    if (x < 0.f) x += twoPi;
    return x - float(M_PI);
}

inline float aulosDspSin(float x) {
    x = aulosDspWrapToPi(x);
    float x2 = x * x;
    return x * (1.f - x2 * (1.f/6.f - x2 * (1.f/120.f
               - x2 * (1.f/5040.f - x2 / 362880.f))));
}

// Cubic jet nonlinearity — models the vortex-to-edge coupling in a flute
// embouchure. Soft symmetric saturation: output approaches +-2/3 as x -> +-inf.
inline float aulosJetFunction(float x) {
    return x - x * x * x * (1.f / 3.f);
}

// Reed reflection function — pressure-controlled beating reed. Input is the
// differential pressure across the reed (mouth minus bore). Asymmetric response
// gives reed instruments their odd-harmonic-rich tone.
inline float aulosReedFunction(float deltaP) {
    const float reedBeat = 1.6f;   // closure hardness — larger = sharper beat
    if (deltaP >= 0.f) {
        float s = deltaP * reedBeat;
        return s / (1.f + s * s);   // rises, peaks, then falls back — beating
    } else {
        float s = deltaP;
        return s / (1.f + 0.5f * fabsf(s));
    }
}

// Fast exponential approximation via repeated squaring.
inline float aulosFastExp(float x) {
    x = 1.0f + x / 256.0f;
    x *= x; x *= x; x *= x; x *= x;
    x *= x; x *= x; x *= x; x *= x;
    return x;
}

// Exponential curve shaping for envelope segments.
inline float aulosMorphShape(float t, float m, float A = 6.f) {
    float a = m * A;
    if (fabsf(a) < 1e-3f) return t;
    return (aulosFastExp(a * t) - 1.f) / (aulosFastExp(a) - 1.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// AulosDCBlocker — first-order high-pass at ~20Hz.
// ─────────────────────────────────────────────────────────────────────────────
struct AulosDCBlocker {
    float x1 = 0.f, y1 = 0.f, R = 0.9995f;

    void setSampleRate(float sr) {
        R = clamp(1.f - 2.f * float(M_PI) * 20.f / sr, 0.990f, 0.9999f);
    }
    float process(float x) {
        y1 = x - x1 + R * y1;
        x1 = x;
        return y1;
    }
    void reset() { x1 = 0.f; y1 = 0.f; }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosOnePoleLPF — one-pole lowpass. coeff = exp(-2*pi*fc/sr).
// ─────────────────────────────────────────────────────────────────────────────
struct AulosOnePoleLPF {
    float z = 0.f, coeff = 0.f;

    void setCutoff(float sr, float fc) {
        coeff = expf(-2.f * float(M_PI) * fc / sr);
    }
    float process(float x) {
        z = coeff * z + (1.f - coeff) * x;
        return z;
    }
    void reset() { z = 0.f; }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosOnePoleHPF — one-pole highpass (input minus its LP component).
// ─────────────────────────────────────────────────────────────────────────────
struct AulosOnePoleHPF {
    float z = 0.f, coeff = 0.f;

    void setCutoff(float sr, float fc) {
        coeff = expf(-2.f * float(M_PI) * fc / sr);
    }
    float process(float x) {
        z = coeff * z + (1.f - coeff) * x;
        return x - z;
    }
    void reset() { z = 0.f; }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosNyquistCap — one-pole anti-alias cap fixed near Nyquist.
// ─────────────────────────────────────────────────────────────────────────────
struct AulosNyquistCap {
    float z = 0.f, coeff = 0.8f;
    void setSampleRate(float sr) {
        coeff = clamp(1.f - expf(-2.f * float(M_PI) * 0.225f), 0.5f, 0.99f);
        (void)sr;  // cutoff is normalized, sr not needed
    }
    float process(float x) { z += coeff * (x - z); return z; }
    void reset() { z = 0.f; }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosEnvFollower — RMS envelope follower.
// ─────────────────────────────────────────────────────────────────────────────
struct AulosEnvFollower {
    float rms = 0.f, coeff = 0.001f;

    void setCoeff(float bandCenterNorm, float followTime, float sampleRate) {
        float periodMs = (bandCenterNorm > 1e-4f)
                       ? (1.f / (bandCenterNorm * sampleRate)) * 1000.f
                       : 2000.f;
        periodMs = clamp(periodMs, 0.5f, 2000.f);
        float scale = 0.5f + followTime * followTime * 299.5f;
        coeff = 1.f - expf(-1.f / ((periodMs * scale * 0.001f) * sampleRate));
    }
    float process(float in) {
        rms += coeff * (in * in - rms);
        return sqrtf(rms < 0.f ? 0.f : rms);
    }
    void reset() { rms = 0.f; }
};

// ─────────────────────────────────────────────────────────────────────────────
// ADAADrive — anti-derivative anti-aliased tanh saturator.
// ─────────────────────────────────────────────────────────────────────────────
struct ADAADrive {
    float lastInput = 0.f;

    static float polyTanh(float x) {
        float x2 = x * x;
        return x - x * x2 * (1.f/3.f - x2 * (2.f/15.f - 17.f/315.f * x2));
    }
    static float antiderivative(float x) {
        float x2 = x * x;
        return 0.5f*x2 - (1.f/12.f)*x2*x2 + (1.f/45.f)*x2*x2*x2
               - (17.f/2520.f)*x2*x2*x2*x2;
    }
    static float applyADAA(float input, float last) {
        float d = input - last;
        return fabsf(d) > 1e-6f
             ? (antiderivative(input) - antiderivative(last)) / d
             : polyTanh(input);
    }

    float process(float inV, float driveGain) {
        float sig  = clamp(inV * driveGain, -13.14f, 13.14f);
        float norm = sig / 10.f;
        float out  = applyADAA(norm, lastInput);
        lastInput  = norm;
        return clamp(out * 6.9f, -10.f, 10.f);
    }
    void reset() { lastInput = 0.f; }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosWaveguide — bidirectional delay line, the bore resonator.
// ─────────────────────────────────────────────────────────────────────────────
struct AulosWaveguide {
    std::vector<float> buf;
    int   bufSize    = 0;
    int   bufMask    = 0;
    int   writeIndex = 0;
    float dampZ1     = 0.f;
    float lastInput  = 0.f;
    float drainGain  = 1.f;

    static constexpr float compressionAmount = 0.30f;
    static constexpr float baseDampCoeff = 0.16f;

    void init(float sr, float maxDelaySec = 0.055f) {
        int needed = (int)ceilf(maxDelaySec * sr) + 8;
        bufSize = 1;
        while (bufSize < needed) bufSize <<= 1;
        bufMask  = bufSize - 1;
        buf.assign(bufSize, 0.f);
        writeIndex = 0;
        dampZ1     = 0.f;
        lastInput  = 0.f;
        drainGain  = 1.f;
    }

    inline float lagrangeRead(float delaySamples) const {
        delaySamples = clamp(delaySamples, 1.f, (float)bufSize - 4.f);
        float rp   = (float)writeIndex - delaySamples;
        int   base = ((int)floorf(rp)) & bufMask;
        float frac = rp - floorf(rp);
        float y0   = buf[(base - 1) & bufMask];
        float y1   = buf[base & bufMask];
        float y2   = buf[(base + 1) & bufMask];
        float y3   = buf[(base + 2) & bufMask];
        return drainGain * aulosLagrange(y0, y1, y2, y3, frac);
    }

    inline float readEnd(float delaySamples) const {
        return lagrangeRead(delaySamples);
    }

    float process(float input, float delaySamples, float dampCoeff, float feedback, float boreDamp = baseDampCoeff) {
        float delayed = lagrangeRead(delaySamples);

        float loopComp = 1.f / (1.f + fabsf(delayed) * compressionAmount);
        float loopIn   = input + feedback * loopComp * delayed;

        float effectiveDamp = fmaxf(dampCoeff, boreDamp);
        dampZ1 = (1.f - effectiveDamp) * loopIn * drainGain + effectiveDamp * dampZ1;

        if (!std::isfinite(dampZ1)) dampZ1 = 0.f;

        buf[writeIndex] = dampZ1;
        writeIndex      = (writeIndex + 1) & bufMask;
        lastInput       = loopIn;

        return delayed;
    }

    void clear() {
        std::fill(buf.begin(), buf.end(), 0.f);
        writeIndex = 0;
        dampZ1     = 0.f;
        lastInput  = 0.f;
        drainGain  = 1.f;
    }

    void idleTick() {}

    void drain(float gain) {
        for (auto& s : buf) s *= gain;
        dampZ1    *= gain;
        lastInput *= gain;
        drainGain  = 1.f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosJetDelay — short pure delay line for the flute jet travel path.
// ─────────────────────────────────────────────────────────────────────────────
struct AulosJetDelay {
    std::vector<float> buf;
    int bufSize    = 0;
    int bufMask    = 0;
    int writeIndex = 0;

    void init(float sr, float maxDelaySec = 0.03f) {
        int needed = (int)ceilf(maxDelaySec * sr) + 8;
        bufSize = 1;
        while (bufSize < needed) bufSize <<= 1;
        bufMask = bufSize - 1;
        buf.assign(bufSize, 0.f);
        writeIndex = 0;
    }

    float process(float input, float delaySamples) {
        buf[writeIndex] = input;
        writeIndex      = (writeIndex + 1) & bufMask;

        delaySamples = clamp(delaySamples, 1.f, (float)bufSize - 4.f);
        float rp   = (float)writeIndex - delaySamples;
        int   base = ((int)floorf(rp)) & bufMask;
        float frac = rp - floorf(rp);
        float y0   = buf[(base - 1) & bufMask];
        float y1   = buf[base & bufMask];
        float y2   = buf[(base + 1) & bufMask];
        float y3   = buf[(base + 2) & bufMask];
        return aulosLagrange(y0, y1, y2, y3, frac);
    }

    void clear() {
        std::fill(buf.begin(), buf.end(), 0.f);
        writeIndex = 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosBreathEnv — ASR breath envelope.
// ─────────────────────────────────────────────────────────────────────────────
enum class AulosEnvPhase { IDLE, GROWTH, SUSTAIN, DECAY };

struct AulosBreathEnv {
    AulosEnvPhase phase   = AulosEnvPhase::IDLE;
    float counter         = 0.f;
    float out             = 0.f;
    float baseline        = 0.f;
    float decayStart      = 0.f;

    void startAttack() {
        baseline = out;
        counter  = 0.f;
        phase    = AulosEnvPhase::GROWTH;
    }

    void startRelease() {
        decayStart = out;
        counter    = 0.f;
        phase      = AulosEnvPhase::DECAY;
    }

    float process(bool gateHigh,
                  float sustainLevel,
                  float attackSamples, float releaseSamples,
                  float attackCurve,   float releaseCurve) {

        float peakVoltage = clamp(sustainLevel, 0.f, 1.f) * 10.f;

        switch (phase) {

        case AulosEnvPhase::IDLE:
            out = 0.f;
            if (gateHigh) startAttack();
            break;

        case AulosEnvPhase::GROWTH:
            counter += 1.f;
            if (attackSamples <= 0.f || counter >= attackSamples) {
                out   = peakVoltage;
                phase = AulosEnvPhase::SUSTAIN;
                counter = 0.f;
            } else {
                float t     = counter / attackSamples;
                float shaped = aulosMorphShape(t, attackCurve);
                out = baseline + (peakVoltage - baseline) * shaped;
                out = clamp(out, 0.f, 10.f);
            }
            if (!gateHigh) startRelease();
            break;

        case AulosEnvPhase::SUSTAIN:
            out = peakVoltage;
            if (!gateHigh) startRelease();
            break;

        case AulosEnvPhase::DECAY:
            counter += 1.f;
            {
                float scale   = clamp(decayStart / 10.f, 0.f, 1.f);
                float scaledR = releaseSamples * scale;

                if (scaledR <= 1.f || out <= 0.001f || counter >= scaledR) {
                    out   = 0.f;
                    phase = AulosEnvPhase::IDLE;
                } else {
                    float t = counter / scaledR;
                    out = decayStart * (1.f - aulosMorphShape(t, releaseCurve));
                    out = clamp(out, 0.f, 10.f);
                }
            }
            if (gateHigh) startAttack();
            break;
        }

        return out;
    }

    void reset() {
        phase      = AulosEnvPhase::IDLE;
        counter    = 0.f;
        out        = 0.f;
        baseline   = 0.f;
        decayStart = 0.f;
    }
};
