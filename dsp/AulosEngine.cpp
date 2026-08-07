////////////////////////////////////////////////////////////
//
//   AulosEngine.cpp
//
//   Ported synthesis loop. processVoice() is byte-for-byte identical to the
//   Rack module; process() replaces the Rack params/inputs/outputs plumbing
//   with the engine's plain input fields and returns summed stereo.
//
////////////////////////////////////////////////////////////

#include "AulosEngine.h"

void AulosEngine::setSampleRate(float sr) {
    sampleRate = sr;
    for (int vi = 0; vi < AULOS_MAX_POLY; ++vi) {
        voices[vi].init(sampleRate);
        voicesR[vi].init(sampleRate);
        voiceBreathScale[vi] = 1.f;
    }
}

void AulosEngine::reset() {
    for (int vi = 0; vi < AULOS_MAX_POLY; ++vi) {
        voices[vi].clear();
        voicesR[vi].clear();
        voiceBreathScale[vi] = 1.f;
    }
    droneLevelSmooth = 1.f;
    moduleTime = 0.f;
    for (int vi = 0; vi < AULOS_MAX_POLY; ++vi) {
        cachedPipeFreq[vi]    = 261.63f;
        cachedFingerFreq[vi]  = 261.63f;
        cachedPipeFreqR[vi]   = 261.63f;
        cachedFingerFreqR[vi] = 261.63f;
    }
}

void AulosEngine::panic() {
    for (int vi = 0; vi < AULOS_MAX_POLY; ++vi) {
        voices[vi].clear();
        voicesR[vi].clear();
    }
}

// ── DSP helper: process one voice, one sample (verbatim from Aulos.cpp) ───────
float AulosEngine::processVoice(AulosVoice& v,
                   float sr,
                   bool  gateHigh,
                   float breathRaw,
                   float attackSamples,
                   float releaseSamples,
                   float attackCurveV,
                   float releaseCurveV,
                   float pipeFreq,
                   float fingerFreq,
                   float audioIn,
                   float waveguideGainV,
                   float cachedDecayGainV,
                   float legatoDip,
                   bool  legatoActive,
                   float refPipeFreq) {

    if (gateHigh && !v.lastGateHigh) {
        v.chiffCounter = 0;

        bool coldStart = (v.breathOut < 0.5f) && (v.safetyRMS < 0.05f);
        if (coldStart) {
            v.safetyRMS     = 0.f;
            v.safetyDecay   = 0.f;
            v.a_fingerDelay = 0.f;
            if (!legatoActive) v.startupGain = 0.f;
        }
    }
    v.lastGateHigh = gateHigh;

    v.breathOut = v.breathEnv.process(gateHigh, breathRaw,
        attackSamples, releaseSamples,
        attackCurveV,  releaseCurveV);
    float breathLevel = v.breathOut * 0.1f * breathRaw;

    float activity = v.breathOut + fabsf(v.safetyDecay);
    if (!gateHigh && activity < idleThreshold) {
        v.asleep         = true;
        v.breathOut      = 0.f;
        v.safetyRMS      = 0.f;
        v.dynEnvOut      = 0.f;
        v.registerSmooth = 0.f;
        return 0.f;
    }

    float refFreq     = (refPipeFreq > 0.f) ? refPipeFreq : pipeFreq;
    float fingerRatio = fingerFreq / refFreq;

    float registerTarget;
    float foldedFingerFreq;
    if (fingerRatio >= 3.88f) {
        registerTarget   = 2.f;
        foldedFingerFreq = fingerFreq * 0.25f;
    } else if (fingerRatio >= 1.94f) {
        registerTarget   = 1.f;
        foldedFingerFreq = fingerFreq * 0.5f;
    } else {
        registerTarget   = 0.f;
        foldedFingerFreq = fingerFreq;
    }

    const float registerLerpCoeff = 0.002f;
    v.registerSmooth += registerLerpCoeff * (registerTarget - v.registerSmooth);

    float activeFraction = refFreq / foldedFingerFreq;
    activeFraction = clamp(activeFraction, 0.50f, 2.0f);

    float fullPipeDelaySamples = freqToDelaySamples(pipeFreq, v.a_bore, sr);
    float primaryDelaySamples  = fullPipeDelaySamples * activeFraction;
    primaryDelaySamples = clamp(primaryDelaySamples, 2.f, (float)v.rightGoingRail.bufSize - 4.f);

    float overblowAmt    = clamp(v.safetyDecay, 0.f, 1.f);
    float overblowTarget = primaryDelaySamples * ((overblowAmt > 0.5f) ? 0.5f : 1.0f);

    if (v.a_fingerDelay < 2.f) v.a_fingerDelay = overblowTarget;
    v.a_fingerDelay += 0.05f * (overblowTarget - v.a_fingerDelay);
    primaryDelaySamples = v.a_fingerDelay;

    float railDelaySamples = clamp(primaryDelaySamples * 0.5f, 2.f, (float)v.leftGoingRail.bufSize - 4.f);

    const float flareKnee = 0.3f;
    float flare = clamp((v.a_bore - flareKnee) / (1.f - flareKnee), 0.f, 1.f);
    flare = flare * flare;
    float brassZone = clamp((v.a_bore - 0.75f) / 0.25f, 0.f, 1.f);
    brassZone = brassZone * brassZone;

    const float bellOpenWeight   = 0.95f;
    const float bellBodyWeight   = 0.42f;
    const float bellFlaredWeight = 0.3f;
    float bellLowpassWeight = bellOpenWeight + flare * (bellBodyWeight - bellOpenWeight)
                            + brassZone * (bellFlaredWeight - bellBodyWeight)
                            * clamp(flare * 2.f, 0.f, 1.f);
    if (bellLowpassWeight < bellFlaredWeight) bellLowpassWeight = bellFlaredWeight;

    float bellPhaseComp = (1.f - bellLowpassWeight) / bellLowpassWeight;
    float leftRailDelay = clamp(railDelaySamples - bellPhaseComp, 2.f, (float)v.leftGoingRail.bufSize - 4.f);

    const float noiseRegScale = 0.4f;
    float noiseAmp = v.a_noise * v.a_noise * 0.03f * v.breathOut
                   * (1.f + v.registerSmooth * noiseRegScale);
    float noiseVal = lcgRandf() * noiseAmp;

    float toneGain = 1.5f + v.a_tone * 1.7f;

    float overdrive = clamp((v.safetyRMS - 1.2f) * 0.5f, 0.f, 1.f);
    toneGain *= (1.f - overdrive * 0.75f);

    float effectiveDrive = toneGain * (0.3f + v.a_reedMorph * 0.3f) * (1.0f + v.a_bore * 0.3f);

    float rightAtBell      = v.rightGoingRail.readEnd(railDelaySamples);
    float leftAtEmbouchure = v.leftGoingRail.readEnd(leftRailDelay);

    const float jetRegScale = 0.4f;
    float jetBias  = breathLevel * (0.8f - v.a_bore * 0.3f) * (1.f + v.registerSmooth * jetRegScale);
    float jetInput = leftAtEmbouchure + jetBias + noiseVal;
    float jetOut   = aulosJetFunction(clamp(jetInput, -3.f, 3.f));

    float jetDelaySamples = clamp(sr * 0.00107f * (440.f / fingerFreq), 2.f, sr * 0.028f);

    float jetVelocityScale = clamp(sqrtf(0.62f / fmaxf(breathLevel, 0.05f)), 0.75f, 1.5f);
    jetDelaySamples = clamp(jetDelaySamples * jetVelocityScale, 2.f, sr * 0.028f);

    const float jetBreathScale = 1.6f;
    float jetFlowGain = clamp(breathLevel * jetBreathScale, 0.f, 1.f);
    float jetPath = v.jetDelay.process(jetOut, jetDelaySamples) * 1.3f * jetFlowGain;

    const float reedPressureGain = 2.0f;
    float reedDeltaP = breathLevel * reedPressureGain - leftAtEmbouchure;

    const float reedBreathScale = 1.6f;
    float reedFlowGain = clamp(breathLevel * reedBreathScale, 0.f, 1.f);
    float reedPath = aulosReedFunction(reedDeltaP) * reedFlowGain + noiseVal;

    const float embSignSlope = 6.f;
    float embSign = clamp((2.f * v.a_reedMorph - 1.f) * embSignSlope, -1.f, 1.f);

    const float reedMakeupGain = 2.4f;
    float excite = jetPath * sqrtf(1.f - v.a_reedMorph)
                 + reedPath * reedMakeupGain * sqrtf(v.a_reedMorph);

    const float crossoverBoost = 1.2f;
    excite *= 1.f + crossoverBoost * (1.f - embSign * embSign);

    excite += audioIn;
    excite *= waveguideGainV;

    if (v.chiffCounter < (int)v.cachedChiffDuration) {
        float env        = 1.f - (float)v.chiffCounter / v.cachedChiffDuration;
        float chiffNoise = lcgRandf() * v.cachedChiffAmp * env * v.breathOut * 0.1f;
        v.chiffZ1        = 0.92f * v.chiffZ1 + 0.08f * chiffNoise;
        excite          += (chiffNoise - v.chiffZ1);
        v.chiffCounter++;
    }

    float envelopeGate = clamp(v.breathOut * 0.15f, 0.f, 1.f);
    float loopGain = crossfade(cachedDecayGainV * 0.8f, v.cachedFeedback, envelopeGate);

    const float dipFeedbackDepth = 0.18f;
    loopGain *= (1.f - legatoDip * dipFeedbackDepth);

    const float registerFeedbackDrop = 0.06f;
    loopGain *= (1.f - v.registerSmooth * registerFeedbackDrop);

    float boreDamp = (0.35f - v.a_bore * 0.28f) * clamp(261.63f / fingerFreq, 0.25f, 1.0f);

    float fluteDamp = (1.f - v.a_reedMorph) * 0.5f * (1.f - v.a_bore * 0.8f)
                    * clamp(261.63f / fingerFreq, 0.25f, 1.0f);

    const float registerDampScale = 0.04f;
    float registerDampExtra = clamp(v.registerSmooth - 1.f, 0.f, 1.f) * registerDampScale;
    float totalDamp = clamp(v.cachedDampCoeff + fluteDamp + registerDampExtra, 0.f, 0.95f);

    float junctionGain = sqrtf(clamp(loopGain, 0.f, 0.999f));

    float embReflection  = junctionGain * embSign;

    float exciteSat = v.loopSaturator.process(excite, effectiveDrive);

    const float exciteDCCoeff = 0.0026f;
    v.exciteDCBlockZ1 += exciteDCCoeff * (exciteSat - v.exciteDCBlockZ1);
    float exciteAC = exciteSat - v.exciteDCBlockZ1;

    float embCompGain = 1.f / (1.f + fabsf(leftAtEmbouchure) * 0.30f);
    float forwardIn   = exciteAC + embReflection * embCompGain * leftAtEmbouchure;

    const float brassZoneDrive = 1.1f;
    float brassAmt = brassZone * brassZoneDrive;
    forwardIn = forwardIn / (1.f + brassAmt * fabsf(forwardIn));

    const float brassMakeup = 0.75f;
    forwardIn *= 1.f + brassAmt * v.safetyRMS * brassMakeup;

    v.rightGoingRail.process(forwardIn, railDelaySamples, totalDamp, 0.f, boreDamp);

    v.bellLowpassZ1 = bellLowpassWeight * rightAtBell
                    + (1.f - bellLowpassWeight) * v.bellLowpassZ1;
    float bellRadiated = rightAtBell - v.bellLowpassZ1;

    float bellCompGain = 1.f / (1.f + fabsf(rightAtBell) * 0.30f);
    float bellInput    = -junctionGain * bellCompGain * v.bellLowpassZ1;
    v.leftGoingRail.process(bellInput, railDelaySamples, 0.f, 0.f, boreDamp);

    {
        float instEnergy = (rightAtBell * rightAtBell + leftAtEmbouchure * leftAtEmbouchure) * 0.5f;
        float rmsTarget  = sqrtf(instEnergy);
        float rmsCoeff   = rmsTarget > v.safetyRMS ? 0.1f : 0.002f;
        v.safetyRMS     += rmsCoeff * (rmsTarget - v.safetyRMS);

        float overblowThreshold = 1.6f + (activeFraction - 1.f) * 0.4f;
        if (v.safetyRMS > overblowThreshold) {
            v.safetyDecay = fminf(v.safetyDecay + 0.001f, 1.f);
        } else {
            v.safetyDecay *= 0.9995f;
            if (v.safetyDecay < 0.001f) v.safetyDecay = 0.f;
        }
    }

    const float bellRadiateBase  = 0.4f;
    const float bellRadiateFlare = 1.5f;
    const float bellRadiateBrass = 0.9f;
    float voiceOut = rightAtBell * 0.85f
                   + bellRadiated * (bellRadiateBase + flare * bellRadiateFlare
                                     + brassZone * bellRadiateBrass);

    voiceOut = v.dcBlocker.process(voiceOut);
    voiceOut = v.outputCap.process(voiceOut);
    voiceOut = v.outputDCBlocker.process(voiceOut);
    if (!std::isfinite(voiceOut)) voiceOut = 0.f;

    {
        float midBand = v.envHPF.process(voiceOut);
        midBand       = v.envLPF.process(midBand);
        v.dynEnvOut   = v.dynFollower.process(midBand);
    }
    float loudnessTarget = clamp(sqrtf(440.f / fingerFreq), 0.25f, 3.0f);
    if (v.loudnessSmooth <= 0.f) v.loudnessSmooth = loudnessTarget;
    v.loudnessSmooth += 0.05f * (loudnessTarget - v.loudnessSmooth);
    voiceOut *= v.loudnessSmooth;

    v.startupGain += 0.004f * (1.f - v.startupGain);
    voiceOut *= v.startupGain;

    return voiceOut;
}

// ── Main per-sample process ──────────────────────────────────────────────────
AulosEngine::Stereo AulosEngine::process() {
    const float sr = sampleRate;
    moduleTime += 1.f / sr;

    // Drone toggle handling: clear the R bank when drone state flips.
    if (droneActive != prevDrone) {
        for (int vi = 0; vi < AULOS_MAX_POLY; ++vi) voicesR[vi].clear();
        prevDrone = droneActive;
    }

    nVoices = (int)clamp((float)nVoices, 1.f, (float)AULOS_MAX_POLY);
    if (nVoices != prevVoices) {
        for (int vi = nVoices; vi < prevVoices; ++vi) {
            voices[vi].clear();
            voicesR[vi].clear();
        }
        prevVoices = nVoices;
    }

    // ── Sub-rate control block ──
    const bool doSkip = (++skipCounter >= SKIP_MAX);
    if (doSkip) {
        skipCounter = 0;

        float sliderVal[8];
        sliderVal[0] = clamp(reed,  0.f, 1.f);
        sliderVal[1] = clamp(bore,  0.f, 1.f);
        sliderVal[2] = clamp(drive, 0.f, 1.f);
        sliderVal[3] = clamp(res,   0.f, 1.f);
        sliderVal[4] = clamp(air,   0.f, 1.f);
        sliderVal[5] = clamp(chiff, 0.f, 1.f);
        // sliderVal[6] = secondaryPipe — read per-sample below
        sliderVal[7] = 0.6f * clamp(wood, 0.f, 1.f);

        static constexpr float LOG_8000 = 8.98719723f;
        static constexpr float LOG_200  = 5.29831737f;
        static constexpr float LOG_RANGE = LOG_200 - LOG_8000;
        float sharedDampCoeff;
        {
            float d     = sliderVal[7];
            d = d*d;
            float blend = clamp(d / 0.25f, 0.f, 1.f);
            float freqT = clamp((d - 0.25f) / 0.75f, 0.f, 1.f);
            float dFreq = expf(LOG_8000 + freqT * LOG_RANGE);
            sharedDampCoeff = blend * expf(-2.f * float(M_PI) * dFreq / sr);
        }

        float sharedFeedback = 0.75f + sliderVal[3] * 0.18f;
        float sharedDecayGain = 0.80f + decayValue * 0.17f;
        float chiffSlider = sliderVal[5];

        float sharedDynCoeff;
        {
            float bandCenterNorm = sqrtf(150.f * 5000.f) / sr;
            float periodMs = (1.f / (bandCenterNorm * sr)) * 1000.f;
            periodMs = clamp(periodMs, 0.5f, 2000.f);
            float scale = 0.5f + followTime * followTime * 299.5f;
            sharedDynCoeff = 1.f - expf(-1.f / ((periodMs * scale * 0.001f) * sr));
        }

        cachedAttackSamples  = sr * 0.002f * powf(2000.f, clamp(attackValue,  0.f, 1.f));
        cachedReleaseSamples = sr * 0.002f * powf(2000.f, clamp(releaseValue, 0.f, 1.f));

        {
            float legatoSamples = fmaxf(legatoTime * 0.001f * sr, 1.f);
            cachedLegatoStep = (float)PITCH_DECIM / legatoSamples;
        }

        for (int vi = 0; vi < nVoices; ++vi) {
            for (int side = 0; side < 2; ++side) {
                AulosVoice& v = (side == 0) ? voices[vi] : voicesR[vi];
                v.t_reedMorph         = sliderVal[0];
                v.t_bore              = sliderVal[1];
                v.t_tone              = sliderVal[2];
                v.t_lip               = sliderVal[3];
                v.t_noise             = sliderVal[4];
                v.t_chiff             = chiffSlider;
                v.t_damp              = sliderVal[7];
                v.cachedFeedback      = sharedFeedback;
                v.cachedDecayGain     = sharedDecayGain;
                v.cachedDampCoeff     = sharedDampCoeff;
                v.cachedChiffDuration = chiffSlider * chiffSlider * sr * 0.15f;
                v.cachedChiffAmp      = chiffSlider * chiffSlider * 0.5f;
                v.dynFollower.coeff   = sharedDynCoeff;

                if (v.asleep) {
                    v.a_reedMorph = v.t_reedMorph;
                    v.a_bore      = v.t_bore;
                    v.a_tone      = v.t_tone;
                    v.a_lip       = v.t_lip;
                    v.a_noise     = v.t_noise;
                    v.a_decay     = v.t_decay;
                    v.a_damp      = v.t_damp;
                    v.a_chiff     = v.t_chiff;
                }
            }
        }
    }
    static const float lerpCoeff = 1.f - expf(-8.f / (float)SKIP_MAX);

    float fingerTuneV = fingerTune;

    float vibratoAttV   = vibratoAtt;
    bool  vibratoPatchedV = vibratoPatched;

    vibratoPhase += vibratoRate * (1.f / sr);
    if (vibratoPhase >= 1.f) vibratoPhase -= 1.f;
    float vibratoLFO = aulosDspSin(vibratoPhase * 2.f * (float)M_PI);

    float vibratoExternal = vibratoPatchedV ? (vibratoAttV * vibratoCV * 0.0167f) : 0.f;
    float vibratoInternal = vibratoPatchedV ? 0.f : (vibratoAttV * vibratoLFO * 0.5f * 0.0833f);

    float breathRaw = clamp(breath, 0.f, 1.f);
    breathRaw = breathRaw * breathRaw;
    if (breathRaw < 0.05f)
        breathRaw = breathRaw * (0.6f / 0.05f);
    else
        breathRaw = 0.6f + (breathRaw - 0.05f) * (0.4f / 0.95f);

    float attackSamples  = cachedAttackSamples;
    float releaseSamples = cachedReleaseSamples;

    float audioIn = audioInVolts * 0.1f * clamp(audioInGain, 0.f, 1.f);
    float volumeV = clamp(volume, 0.f, 1.f) * 3.f;

    float aulosOffset = clamp(secondaryPipe, -1.f, 1.f);

    float sharedDecayGain = 0.80f + decayValue * 0.17f;

    bool droneEffective = droneActive;
    droneLevelSmooth += 0.002f * ((droneEffective ? 0.6f : 1.0f) - droneLevelSmooth);

    const bool processR = rOutputConnected;
    if (processR && !prevRConnected) {
        for (int vi = 0; vi < AULOS_MAX_POLY; ++vi) voicesR[vi].clear();
    }
    prevRConnected = processR;

    const bool doPitch = (pitchCounter == 0);
    if (++pitchCounter >= PITCH_DECIM) pitchCounter = 0;

    float masterL = 0.f;
    float masterR = 0.f;

    for (int vi = 0; vi < nVoices; ++vi) {
        AulosVoice& v  = voices[vi];
        AulosVoice& vR = voicesR[vi];

        float gateV   = gate[vi];
        bool gateHigh = (gateV > 1.f) || manualGateActive;
        bool gateHighR = droneEffective ? true : gateHigh;

        bool wokeL = v.asleep && gateHigh;
        if (wokeL) v.asleep = false;
        bool wokeR = processR && vR.asleep && gateHighR;
        if (wokeR) vR.asleep = false;

        const bool activeL = !v.asleep;
        const bool activeR = processR && !vR.asleep;

        if (!activeL && !activeR)
            continue;

        if (activeL) {
            v.a_reedMorph += lerpCoeff * (v.t_reedMorph - v.a_reedMorph);
            v.a_bore      += lerpCoeff * (v.t_bore      - v.a_bore);
            v.a_tone      += lerpCoeff * (v.t_tone      - v.a_tone);
            v.a_lip       += lerpCoeff * (v.t_lip       - v.a_lip);
            v.a_noise     += lerpCoeff * (v.t_noise     - v.a_noise);
            v.a_decay     += lerpCoeff * (v.t_decay     - v.a_decay);
            v.a_damp      += lerpCoeff * (v.t_damp      - v.a_damp);
            v.a_chiff     += lerpCoeff * (v.t_chiff     - v.a_chiff);
        }
        if (activeR) {
            vR.a_reedMorph += lerpCoeff * (vR.t_reedMorph - vR.a_reedMorph);
            vR.a_bore      += lerpCoeff * (vR.t_bore      - vR.a_bore);
            vR.a_tone      += lerpCoeff * (vR.t_tone      - vR.a_tone);
            vR.a_lip       += lerpCoeff * (vR.t_lip       - vR.a_lip);
            vR.a_noise     += lerpCoeff * (vR.t_noise     - vR.a_noise);
            vR.a_decay     += lerpCoeff * (vR.t_decay     - vR.a_decay);
            vR.a_damp      += lerpCoeff * (vR.t_damp      - vR.a_damp);
            vR.a_chiff     += lerpCoeff * (vR.t_chiff     - vR.a_chiff);
        }

        if (doPitch || wokeL || wokeR) {
            float vibGate    = clamp(v.breathOut * 0.1f, 0.f, 1.f);
            float fmDepth    = vibratoExternal + vibratoInternal * vibGate;

            float vibGateR   = droneEffective ? 1.f : clamp(vR.breathOut * 0.1f, 0.f, 1.f);
            float fmDepthR   = vibratoExternal + vibratoInternal * vibGateR;

            float pipeVoctV = (pipeVoctConnected ? pipeVoct[vi] : 0.f) + pipeTune;
            cachedPipeFreq[vi] = voct2freq(pipeVoctV);

            float fingerVoctBase = fingerVoctConnected
                                 ? fingerVoct[vi] + fingerTuneV
                                 : pipeVoctV + fingerTuneV;
            float fingerVoctV   = fingerVoctBase + fmDepth;
            float newFingerFreq = voct2freq(fingerVoctV);

            float newFingerFreqR;
            cachedPipeFreqR[vi] = voct2freq(pipeVoctV + aulosOffset + fmDepthR);
            newFingerFreqR = droneEffective ? cachedPipeFreqR[vi]
                           : (aulosTrack ? voct2freq(fingerVoctV + aulosOffset)
                                         : newFingerFreq);

            if (legatoEnabled && !wokeL && gateHigh && legatoProgressL[vi] >= 0.f) {
                static constexpr float LOG2_C4 = 8.03178968f;
                float newLog2Base = LOG2_C4 + fingerVoctBase;
                if (fabsf(newLog2Base - legatoLog2TargetL[vi]) > 0.042f) {
                    float currentLog2 = legatoStartL[vi] +
                        legatoProgressL[vi] * (legatoLog2TargetL[vi] - legatoStartL[vi]);
                    legatoStartL[vi]      = currentLog2;
                    legatoLog2TargetL[vi] = newLog2Base;
                    legatoProgressL[vi]   = 0.f;
                    legatoDipL[vi]        = 1.f;
                }
                legatoProgressL[vi] = fminf(1.f, legatoProgressL[vi] + cachedLegatoStep);
                float glidedLog2    = legatoStartL[vi] +
                    legatoProgressL[vi] * (legatoLog2TargetL[vi] - legatoStartL[vi]);
                cachedFingerFreq[vi] = exp2f(glidedLog2 + fmDepth);
            } else {
                cachedFingerFreq[vi] = newFingerFreq;
                if (wokeL || !legatoEnabled) {
                    static constexpr float LOG2_C4 = 8.03178968f;
                    legatoStartL[vi]      = LOG2_C4 + fingerVoctBase;
                    legatoLog2TargetL[vi] = legatoStartL[vi];
                    legatoProgressL[vi]   = 1.f;
                }
            }

            if (legatoEnabled && !wokeR && gateHighR && legatoProgressR[vi] >= 0.f && !droneEffective) {
                float fingerVoctBaseR = aulosTrack ? fingerVoctBase + aulosOffset : fingerVoctBase;
                static constexpr float LOG2_C4 = 8.03178968f;
                float newLog2BaseR    = LOG2_C4 + fingerVoctBaseR;
                if (fabsf(newLog2BaseR - legatoLog2TargetR[vi]) > 0.042f) {
                    float currentLog2R = legatoStartR[vi] +
                        legatoProgressR[vi] * (legatoLog2TargetR[vi] - legatoStartR[vi]);
                    legatoStartR[vi]      = currentLog2R;
                    legatoLog2TargetR[vi] = newLog2BaseR;
                    legatoProgressR[vi]   = 0.f;
                    legatoDipR[vi]        = 1.f;
                }
                legatoProgressR[vi] = fminf(1.f, legatoProgressR[vi] + cachedLegatoStep);
                float glidedLog2R   = legatoStartR[vi] +
                    legatoProgressR[vi] * (legatoLog2TargetR[vi] - legatoStartR[vi]);
                cachedFingerFreqR[vi] = exp2f(glidedLog2R + fmDepthR);
            } else {
                cachedFingerFreqR[vi] = newFingerFreqR;
                if (wokeR || !legatoEnabled) {
                    float fingerVoctBaseR = aulosTrack ? fingerVoctBase + aulosOffset : fingerVoctBase;
                    static constexpr float LOG2_C4 = 8.03178968f;
                    legatoStartR[vi]      = LOG2_C4 + fingerVoctBaseR;
                    legatoLog2TargetR[vi] = legatoStartR[vi];
                    legatoProgressR[vi]   = 1.f;
                }
            }

            const float dipDecayStep = PITCH_DECIM / (0.020f * sr);
            legatoDipL[vi] = fmaxf(0.f, legatoDipL[vi] - dipDecayStep);
            legatoDipR[vi] = fmaxf(0.f, legatoDipR[vi] - dipDecayStep);
        }
        float pipeFreq    = cachedPipeFreq[vi];
        float fingerFreq  = cachedFingerFreq[vi];
        float pipeFreqR   = cachedPipeFreqR[vi];
        float fingerFreqR = cachedFingerFreqR[vi];

        float breathL = breathRaw;
        float breathR = breathRaw;
        if (!vibratoPatchedV) {
            float vibratoBreathAmt = vibratoAttV * vibratoBreathDepth * 0.12f;
            breathL = breathRaw * (1.f + vibratoLFO * clamp(v.breathOut  * 0.1f, 0.f, 1.f) * vibratoBreathAmt);
            float vibGateRForBreath = droneEffective ? 1.f : clamp(vR.breathOut * 0.1f, 0.f, 1.f);
            breathR = breathRaw * (1.f + vibratoLFO * vibGateRForBreath * vibratoBreathAmt);
        }

        // Per-voice velocity scaling of breath.
        breathL *= voiceBreathScale[vi];
        breathR *= voiceBreathScale[vi];

        float voiceOut = 0.f;
        if (activeL) {
            voiceOut = processVoice(v,
                sr, gateHigh,
                breathL, attackSamples, releaseSamples,
                attackCurve, releaseCurve,
                pipeFreq, fingerFreq,
                audioIn, waveguideGain, sharedDecayGain,
                legatoDipL[vi],
                legatoProgressL[vi] < 1.f);
        }

        float voiceOutR = 0.f;
        if (activeR) {
            voiceOutR = processVoice(vR,
                sr, gateHighR,
                breathR, attackSamples, releaseSamples,
                attackCurve, releaseCurve,
                pipeFreqR, fingerFreqR,
                audioIn, waveguideGain, sharedDecayGain,
                legatoDipR[vi],
                legatoProgressR[vi] < 1.f,
                aulosTrack ? pipeFreqR : pipeFreq);

            voiceOutR *= droneLevelSmooth;
        }

        masterL += voiceOut  * volumeV;
        masterR += voiceOutR * volumeV;
    }

    // ── Display data (voice 0), updated at the pitch-decimation rate ──
    if (doPitch && nVoices > 0) {
        display.pipeFreq = cachedPipeFreq[0];
        display.breath   = voices[0].breathOut * 0.1f;

        float fingerF  = cachedFingerFreq[0];
        float fingerFR = cachedFingerFreqR[0];
        float refL = display.pipeFreq;
        float refR = aulosTrack ? cachedPipeFreqR[0] : display.pipeFreq;

        float ratioL  = fingerF / refL;
        float foldedL = (ratioL >= 3.88f) ? fingerF * 0.25f
                      : (ratioL >= 1.94f) ? fingerF * 0.5f
                      : fingerF;
        display.activeFraction = clamp(refL / foldedL, 0.50f, 1.02f);

        float ratioR  = fingerFR / refR;
        float foldedR = (ratioR >= 3.88f) ? fingerFR * 0.25f
                      : (ratioR >= 1.94f) ? fingerFR * 0.5f
                      : fingerFR;
        display.activeFractionR = clamp(refR / foldedR, 0.50f, 1.02f);

        display.reg  = voices[0].registerSmooth;
        display.regR = processR ? voicesR[0].registerSmooth : 0.f;

        display.breathR     = processR ? voicesR[0].breathOut * 0.1f * droneLevelSmooth : 0.f;
        display.overblow    = voices[0].safetyDecay;
        display.overblowR   = processR ? voicesR[0].safetyDecay : 0.f;
        display.rms         = voices[0].dynEnvOut;
        display.rmsR        = processR ? voicesR[0].dynEnvOut * droneLevelSmooth : 0.f;
        display.air         = voices[0].a_noise;
        display.reed        = voices[0].a_reedMorph;
        display.bore        = voices[0].a_bore;
        display.saturation  = clamp(voices[0].safetyRMS - 0.6f, 0.f, 1.f);
        display.saturationR = processR ? clamp(voicesR[0].safetyRMS - 0.6f, 0.f, 1.f) : 0.f;
        {
            float chiffDur = voices[0].cachedChiffDuration;
            float chiffCnt = (float)voices[0].chiffCounter;
            display.chiff  = (chiffDur > 0.f) ? clamp(1.f - chiffCnt / chiffDur, 0.f, 1.f) : 0.f;
        }
        {
            float chiffDurR = processR ? voicesR[0].cachedChiffDuration : 0.f;
            float chiffCntR = processR ? (float)voicesR[0].chiffCounter  : 0.f;
            display.chiffR  = (chiffDurR > 0.f) ? clamp(1.f - chiffCntR / chiffDurR, 0.f, 1.f) : 0.f;
        }
        display.pipeRatio = clamp(exp2_taylor5(-aulosOffset), 0.25f, 2.0f);
    }

    return { clamp(masterL, -10.f, 10.f), clamp(masterR, -10.f, 10.f) };
}
