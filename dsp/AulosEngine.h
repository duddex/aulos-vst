////////////////////////////////////////////////////////////
//
//   AulosEngine.h
//
//   Framework-agnostic extraction of the Aulos synthesis engine.
//   No VCV Rack, no VST3/JUCE — just plain input fields in, stereo out.
//
//   The host layer sets the control fields and per-voice pitch/gate, then
//   calls process() once per sample. Voice DSP (processVoice) and the control
//   scheduling (skip block, pitch decimation, legato) are ported verbatim from
//   the Rack module so the sound is identical.
//
////////////////////////////////////////////////////////////

#pragma once
#include "AulosDSP.h"
#include <cstdint>

static constexpr int AULOS_MAX_POLY = 16;

// ─────────────────────────────────────────────────────────────────────────────
// AulosVoice — one bore/exciter voice. Ported verbatim from Aulos.cpp.
// ─────────────────────────────────────────────────────────────────────────────
struct AulosVoice {
    AulosWaveguide   rightGoingRail;
    AulosWaveguide   leftGoingRail;
    AulosJetDelay    jetDelay;
    AulosBreathEnv   breathEnv;
    ADAADrive        loopSaturator;
    AulosDCBlocker   dcBlocker;
    AulosDCBlocker   outputDCBlocker;
    AulosNyquistCap  outputCap;
    AulosEnvFollower dynFollower;
    AulosOnePoleHPF  envHPF;
    AulosOnePoleLPF  envLPF;

    bool  lastGateHigh      = false;

    float breathOut = 0.f;
    float dynEnvOut = 0.f;

    float t_reedMorph = 0.5f, a_reedMorph = 0.5f;
    float t_bore      = 0.3f, a_bore      = 0.3f;
    float t_tone      = 0.5f, a_tone      = 0.5f;
    float t_lip       = 0.8f, a_lip       = 0.8f;
    float t_noise     = 0.1f, a_noise     = 0.1f;
    float t_decay     = 0.7f, a_decay     = 0.7f;
    float t_damp      = 0.1f, a_damp      = 0.1f;
    float t_chiff     = 0.0f, a_chiff     = 0.0f;
    float t_blend     = 0.7f, a_blend     = 0.7f;

    int   chiffCounter        = 0;
    float chiffZ1             = 0.f;
    float cachedChiffDuration = 0.f;
    float cachedChiffAmp      = 0.f;

    float cachedDampCoeff = 0.02f;
    float cachedFeedback  = 0.93f;
    float cachedDecayGain = 0.9999f;

    float exciteDCBlockZ1 = 0.f;
    float bellLowpassZ1   = 0.f;

    float a_fingerDelay = 0.f;

    float safetyRMS   = 0.f;
    float safetyDecay = 0.f;

    float registerSmooth = 0.f;

    bool asleep = true;

    float startupGain = 1.f;

    float loudnessSmooth = 0.f;

    void init(float sr) {
        rightGoingRail.init(sr, 0.055f);
        leftGoingRail.init(sr,  0.028f);
        jetDelay.init(sr, 0.03f);
        dcBlocker.setSampleRate(sr);
        outputDCBlocker.setSampleRate(sr);
        outputCap.setSampleRate(sr);
        dynFollower.setCoeff(sqrtf(150.f * 5000.f) / sr, 0.25f, sr);
        envHPF.setCutoff(sr, 150.f);
        envLPF.setCutoff(sr, 5000.f);
        breathEnv.reset();
        loopSaturator.reset();
        lastGateHigh      = false;
        breathOut         = 0.f;
        dynEnvOut         = 0.f;
        chiffCounter      = 0x7FFFFFFF;
        chiffZ1           = 0.f;
        safetyRMS         = 0.f;
        safetyDecay       = 0.f;
        registerSmooth    = 0.f;
        a_fingerDelay     = 0.f;
        asleep            = true;
        startupGain       = 1.f;
        loudnessSmooth    = 0.f;
        exciteDCBlockZ1 = 0.f;
        bellLowpassZ1   = 0.f;
    }

    void clear() {
        rightGoingRail.clear();
        leftGoingRail.clear();
        jetDelay.clear();
        breathEnv.reset();
        loopSaturator.reset();
        dcBlocker.reset();
        outputDCBlocker.reset();
        outputCap.reset();
        dynFollower.reset();
        envHPF.reset();
        envLPF.reset();
        lastGateHigh      = false;
        breathOut         = 0.f;
        dynEnvOut         = 0.f;
        chiffCounter      = 0x7FFFFFFF;
        chiffZ1           = 0.f;
        safetyRMS         = 0.f;
        safetyDecay       = 0.f;
        registerSmooth    = 0.f;
        a_fingerDelay     = 0.f;
        asleep            = true;
        startupGain       = 1.f;
        loudnessSmooth    = 0.f;
        exciteDCBlockZ1 = 0.f;
        bellLowpassZ1   = 0.f;
        a_reedMorph = t_reedMorph = 0.5f;
        a_bore      = t_bore      = 0.3f;
        a_tone      = t_tone      = 0.5f;
        a_lip       = t_lip       = 0.8f;
        a_noise     = t_noise     = 0.1f;
        a_decay     = t_decay     = 0.7f;
        a_damp      = t_damp      = 0.1f;
        a_chiff     = t_chiff     = 0.0f;
        a_blend     = t_blend     = 0.7f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// AulosEngine — set inputs, call process() once per sample.
// ─────────────────────────────────────────────────────────────────────────────
struct AulosEngine {

    // ── Control parameters (final values; CV already folded in by the host) ──
    // Eight panel sliders, 0..1 unless noted.
    float reed          = 0.1f;   // REED_PARAM
    float bore          = 0.0f;   // BORE_PARAM
    float drive         = 0.6f;   // DRIVE_PARAM (a_tone)
    float res           = 0.6f;   // RES_PARAM  (loop feedback)
    float air           = 0.3f;   // AIR_PARAM  (noise)
    float chiff         = 0.4f;   // CHIFF_PARAM
    float secondaryPipe = 0.0f;   // AULOS_PARAM, -1..+1 oct
    float wood          = 0.1f;   // WOOD_PARAM (damp)

    float breath        = 0.7f;   // BREATH_PARAM, 0..1 (pre-nonlinear)
    float volume        = 0.5f;   // VOLUME_PARAM, 0..1
    float pipeTune      = 0.0f;   // PIPE_TUNE_PARAM, oct
    float fingerTune    = 0.0f;   // FINGER_TUNE_PARAM, oct

    float audioInGain   = 0.5f;   // AUDIO_IN_GAIN_PARAM, 0..1

    // ── Advanced (context-menu) parameters ──
    float followTime    = 0.25f;
    float attackCurve   = 0.3f;
    float releaseCurve  = -0.5f;
    float waveguideGain = 1.4f;
    float decayValue    = 0.9f;
    float attackValue   = 0.3f;
    float releaseValue  = 0.5f;
    float vibratoRate        = 5.0f;
    float vibratoBreathDepth = 0.5f;
    float vibratoAtt         = 0.3f;   // VIBRATO_ATT (depth), 0..1
    bool  legatoEnabled = false;
    float legatoTime    = 20.f;
    bool  aulosTrack    = false;
    bool  droneActive   = false;

    // ── Per-sample inputs (set before each process() call) ──
    int   nVoices = 1;
    float gate[AULOS_MAX_POLY]       = {};   // gate voltage, >1 = on
    float pipeVoct[AULOS_MAX_POLY]   = {};   // V/oct at pipe input
    float fingerVoct[AULOS_MAX_POLY] = {};   // V/oct at finger input
    float voiceBreathScale[AULOS_MAX_POLY] = {};  // per-voice breath mult (velocity)
    bool  pipeVoctConnected   = false;
    bool  fingerVoctConnected = true;
    bool  manualGateActive    = false;
    float vibratoCV           = 0.f;   // volts at vibrato input
    bool  vibratoPatched      = false;
    float audioInVolts        = 0.f;   // volts at audio input
    bool  rOutputConnected    = true;  // process the R/secondary bank

    // ── Output of the last process() call ──
    struct Stereo { float l, r; };

    // ── Display state, updated at the pitch-decimation rate (voice 0) ──
    // Read by the GUI. Plain floats: the host layer copies these to atomics.
    struct Display {
        float pipeFreq        = 261.63f;
        float activeFraction  = 1.f, activeFractionR = 1.f;
        float bore            = 0.f;
        float breath          = 0.f, breathR = 0.f;
        float overblow        = 0.f, overblowR = 0.f;
        float reg             = 0.f, regR = 0.f;   // register 0..2
        float rms             = 0.f, rmsR = 0.f;
        float air             = 0.f;
        float chiff           = 0.f, chiffR = 0.f;
        float reed            = 0.f;
        float saturation      = 0.f, saturationR = 0.f;
        float pipeRatio       = 1.f;
    };
    const Display& getDisplay() const { return display; }

    // ── Lifecycle ──
    void setSampleRate(float sr);
    void reset();
    void panic();

    // Advance one sample. Reads the input fields, returns summed stereo:
    //   l = sum of primary (L) voices, r = sum of secondary/drone (R) voices.
    Stereo process();

private:
    static constexpr int SKIP_MAX     = 200;
    static constexpr int PITCH_DECIM  = 8;
    const float idleThreshold = 0.0005f;

    AulosVoice voices[AULOS_MAX_POLY];
    AulosVoice voicesR[AULOS_MAX_POLY];
    int   prevVoices = 0;
    float sampleRate = 48000.f;
    float moduleTime = 0.f;

    int skipCounter  = SKIP_MAX;
    int pitchCounter = 0;

    float cachedPipeFreq[AULOS_MAX_POLY]    = {};
    float cachedFingerFreq[AULOS_MAX_POLY]  = {};
    float cachedPipeFreqR[AULOS_MAX_POLY]   = {};
    float cachedFingerFreqR[AULOS_MAX_POLY] = {};

    float cachedAttackSamples  = 1000.f;
    float cachedReleaseSamples = 1000.f;

    bool prevRConnected = false;
    bool prevDrone      = false;

    float vibratoPhase = 0.f;

    float legatoStartL[AULOS_MAX_POLY]      = {};
    float legatoStartR[AULOS_MAX_POLY]      = {};
    float legatoLog2TargetL[AULOS_MAX_POLY] = {};
    float legatoLog2TargetR[AULOS_MAX_POLY] = {};
    float legatoProgressL[AULOS_MAX_POLY]   = {};
    float legatoProgressR[AULOS_MAX_POLY]   = {};
    float legatoDipL[AULOS_MAX_POLY]        = {};
    float legatoDipR[AULOS_MAX_POLY]        = {};
    float cachedLegatoStep = 0.f;

    float droneLevelSmooth = 1.f;

    Display display;

    uint32_t rngState = 0xA510500Du;
    inline float lcgRandf() {
        rngState = rngState * 1664525u + 1013904223u;
        return (float)(rngState & 0xFFFF) / 32767.5f - 1.f;
    }

    static float voct2freq(float voct) {
        return 261.63f * exp2_taylor5(voct);
    }
    static float freqToDelaySamples(float freq, float bore, float sr) {
        (void)bore;
        return sr / freq;
    }

    float processVoice(AulosVoice& v,
                       float sr, bool gateHigh, float breathRaw,
                       float attackSamples, float releaseSamples,
                       float attackCurveV, float releaseCurveV,
                       float pipeFreq, float fingerFreq, float audioIn,
                       float waveguideGainV, float cachedDecayGainV,
                       float legatoDip, bool legatoActive = false,
                       float refPipeFreq = 0.f);
};
