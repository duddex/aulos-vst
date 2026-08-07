////////////////////////////////////////////////////////////
//
//   PluginProcessor.cpp
//
////////////////////////////////////////////////////////////

#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ── Factory presets ──────────────────────────────────────────────────────────
namespace {
    struct Preset {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<Preset>& getPresets() {
        // Each preset sets the sound-shaping params; volume/octave and the
        // advanced envelope curves are left under user control.
        static const std::vector<Preset> presets = {
            {"Init",            {{"reed",0.10f},{"bore",0.00f},{"drive",0.60f},{"res",0.60f},{"air",0.30f},{"chiff",0.40f},{"secondary",0.00f},{"wood",0.10f},{"breath",0.70f},{"vibDepth",0.30f},{"legato",1.f},{"track",0.f},{"drone",0.f}}},
            {"Flute",           {{"reed",0.08f},{"bore",0.38f},{"drive",0.50f},{"res",0.62f},{"air",0.35f},{"chiff",0.45f},{"secondary",0.00f},{"wood",0.14f},{"breath",0.72f},{"vibDepth",0.35f},{"legato",1.f},{"track",0.f},{"drone",0.f}}},
            {"Recorder",        {{"reed",0.04f},{"bore",0.16f},{"drive",0.42f},{"res",0.55f},{"air",0.50f},{"chiff",0.62f},{"secondary",0.00f},{"wood",0.10f},{"breath",0.66f},{"vibDepth",0.20f},{"legato",0.f},{"track",0.f},{"drone",0.f}}},
            {"Clarinet",        {{"reed",0.85f},{"bore",0.24f},{"drive",0.62f},{"res",0.72f},{"air",0.20f},{"chiff",0.30f},{"secondary",0.00f},{"wood",0.20f},{"breath",0.76f},{"vibDepth",0.18f},{"legato",1.f},{"track",0.f},{"drone",0.f}}},
            {"Oboe",            {{"reed",0.95f},{"bore",0.55f},{"drive",0.76f},{"res",0.73f},{"air",0.24f},{"chiff",0.34f},{"secondary",0.00f},{"wood",0.16f},{"breath",0.80f},{"vibDepth",0.28f},{"legato",1.f},{"track",0.f},{"drone",0.f}}},
            {"Brass Bore",      {{"reed",0.55f},{"bore",0.92f},{"drive",0.82f},{"res",0.78f},{"air",0.20f},{"chiff",0.30f},{"secondary",0.00f},{"wood",0.24f},{"breath",0.85f},{"vibDepth",0.22f},{"legato",1.f},{"track",0.f},{"drone",0.f}}},
            {"Breathy Whistle", {{"reed",0.00f},{"bore",0.50f},{"drive",0.40f},{"res",0.58f},{"air",0.70f},{"chiff",0.70f},{"secondary",0.00f},{"wood",0.10f},{"breath",0.60f},{"vibDepth",0.40f},{"legato",1.f},{"track",0.f},{"drone",0.f}}},
            {"Pan Flute Drone", {{"reed",0.10f},{"bore",0.30f},{"drive",0.50f},{"res",0.60f},{"air",0.42f},{"chiff",0.55f},{"secondary",0.583f},{"wood",0.12f},{"breath",0.70f},{"vibDepth",0.30f},{"legato",0.f},{"track",0.f},{"drone",1.f}}},
            {"Two Pipes (5th)", {{"reed",0.12f},{"bore",0.35f},{"drive",0.55f},{"res",0.62f},{"air",0.35f},{"chiff",0.45f},{"secondary",0.583f},{"wood",0.14f},{"breath",0.72f},{"vibDepth",0.30f},{"legato",1.f},{"track",1.f},{"drone",0.f}}},
        };
        return presets;
    }
}

int AulosAudioProcessor::getNumPresets() const { return (int)getPresets().size(); }

juce::String AulosAudioProcessor::getPresetName(int index) const {
    const auto& ps = getPresets();
    return juce::isPositiveAndBelow(index, (int)ps.size()) ? juce::String(ps[index].name)
                                                           : juce::String();
}

void AulosAudioProcessor::applyPreset(int index) {
    const auto& ps = getPresets();
    if (!juce::isPositiveAndBelow(index, (int)ps.size())) return;
    currentPreset = index;
    for (const auto& kv : ps[index].values)
        if (auto* p = apvts.getParameter(kv.first))
            p->setValueNotifyingHost(p->convertTo0to1(kv.second));
}

APVTS::ParameterLayout AulosAudioProcessor::makeLayout() {
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto add = [&](const char* id, const char* name, float lo, float hi, float def) {
        params.push_back(std::make_unique<P>(
            juce::ParameterID(id, 1), name, juce::NormalisableRange<float>(lo, hi), def));
    };
    auto addBool = [&](const char* id, const char* name, bool def) {
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(id, 1), name, def));
    };

    // Eight panel sliders.
    add("reed",      "Reed",           0.f, 1.f, 0.1f);
    add("bore",      "Bore",           0.f, 1.f, 0.0f);
    add("drive",     "Drive",          0.f, 1.f, 0.6f);
    add("res",       "Resonance",      0.f, 1.f, 0.6f);
    add("air",       "Air",            0.f, 1.f, 0.3f);
    add("chiff",     "Chiff",          0.f, 1.f, 0.4f);
    add("secondary", "Secondary Pipe",-1.f, 1.f, 0.0f);
    add("wood",      "Wood",           0.f, 1.f, 0.1f);
    // Global controls.
    add("breath",    "Breath",         0.f, 1.f, 0.7f);
    add("volume",    "Volume",         0.f, 1.f, 0.5f);
    add("octave",    "Octave",        -2.f, 2.f, 0.0f);

    // Resonator / envelope (was the context menu).
    add("wgGain",       "Waveguide Gain", 0.f,  1.75f, 1.4f);
    add("decay",        "Decay",          0.f,  1.f,   0.9f);
    add("attack",       "Attack",         0.f,  1.f,   0.3f);
    add("release",      "Release",        0.f,  1.f,   0.5f);
    add("attackCurve",  "Attack Curve",  -1.f,  1.f,   0.3f);
    add("releaseCurve", "Release Curve", -1.f,  1.f,  -0.5f);
    add("followTime",   "Follow Time",    0.f,  1.f,   0.25f);

    // Vibrato.
    add("vibRate",   "Vibrato Rate",    3.f, 12.f, 5.0f);
    add("vibBreath", "Vibrato Breath",  0.f,  1.f, 0.5f);
    add("vibDepth",  "Vibrato Depth",   0.f,  1.f, 0.3f);

    // Articulation.
    add("legatoTime", "Legato Time (ms)", 5.f, 80.f, 20.f);
    addBool("legato",     "Legato",       true);
    addBool("track",      "Secondary Tracks", false);
    addBool("drone",      "Drone",        false);
    addBool("manualGate", "Manual Gate",  false);

    return { params.begin(), params.end() };
}

AulosAudioProcessor::AulosAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", makeLayout()) {

    pReed      = apvts.getRawParameterValue("reed");
    pBore      = apvts.getRawParameterValue("bore");
    pDrive     = apvts.getRawParameterValue("drive");
    pRes       = apvts.getRawParameterValue("res");
    pAir       = apvts.getRawParameterValue("air");
    pChiff     = apvts.getRawParameterValue("chiff");
    pSecondary = apvts.getRawParameterValue("secondary");
    pWood      = apvts.getRawParameterValue("wood");
    pBreath    = apvts.getRawParameterValue("breath");
    pVolume    = apvts.getRawParameterValue("volume");
    pOctave    = apvts.getRawParameterValue("octave");

    pWgGain       = apvts.getRawParameterValue("wgGain");
    pDecay        = apvts.getRawParameterValue("decay");
    pAttack       = apvts.getRawParameterValue("attack");
    pRelease      = apvts.getRawParameterValue("release");
    pAttackCurve  = apvts.getRawParameterValue("attackCurve");
    pReleaseCurve = apvts.getRawParameterValue("releaseCurve");
    pFollow       = apvts.getRawParameterValue("followTime");
    pVibRate      = apvts.getRawParameterValue("vibRate");
    pVibBreath    = apvts.getRawParameterValue("vibBreath");
    pVibDepth     = apvts.getRawParameterValue("vibDepth");
    pLegatoTime   = apvts.getRawParameterValue("legatoTime");
    pLegato       = apvts.getRawParameterValue("legato");
    pTrack        = apvts.getRawParameterValue("track");
    pDrone        = apvts.getRawParameterValue("drone");
    pManualGate   = apvts.getRawParameterValue("manualGate");

    engine.pipeVoctConnected   = true;
    engine.fingerVoctConnected = true;
    engine.rOutputConnected    = true;
    engine.nVoices             = kMaxVoices;
}

void AulosAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine.setSampleRate((float)sampleRate);
    engine.reset();
    handleAllOff();
}

bool AulosAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

// ── Voice allocation ─────────────────────────────────────────────────────────
void AulosAudioProcessor::handleNoteOn(int note, float velocity) {
    int slot = -1;
    for (int v = 0; v < kMaxVoices; ++v)
        if (!voiceGate[v]) { slot = v; break; }
    if (slot < 0) {  // steal the oldest sounding voice
        uint32_t best = 0xFFFFFFFFu;
        for (int v = 0; v < kMaxVoices; ++v)
            if (voiceAge[v] < best) { best = voiceAge[v]; slot = v; }
    }
    voiceNote[slot]      = note;
    voiceGate[slot]      = true;
    voiceSustained[slot] = false;
    voiceAge[slot]       = ++ageCounter;

    // Velocity scales breath: soft keys speak airy and quiet, hard keys full.
    engine.voiceBreathScale[slot] = 0.3f + 0.7f * velocity;

    const float voct = (note - 60) / 12.f;   // MIDI 60 = C4 = engine voct 0
    engine.fingerVoct[slot] = voct;
    engine.pipeVoct[slot]   = voct;
}

void AulosAudioProcessor::handleNoteOff(int note) {
    for (int v = 0; v < kMaxVoices; ++v)
        if (voiceGate[v] && voiceNote[v] == note) {
            if (sustainDown) voiceSustained[v] = true;   // defer until pedal up
            else             voiceGate[v] = false;
        }
}

void AulosAudioProcessor::handleAllOff() {
    for (int v = 0; v < kMaxVoices; ++v) {
        voiceGate[v]      = false;
        voiceSustained[v] = false;
        voiceNote[v]      = -1;
        voiceAge[v]       = 0;
    }
    ageCounter  = 0;
    sustainDown = false;
}

// ── Audio ────────────────────────────────────────────────────────────────────
void AulosAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    auto* outL = buffer.getWritePointer(0);
    auto* outR = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    // Snapshot params for this block.
    engine.reed          = pReed->load();
    engine.bore          = pBore->load();
    engine.drive         = pDrive->load();
    engine.res           = pRes->load();
    engine.air           = pAir->load();
    engine.chiff         = pChiff->load();
    engine.secondaryPipe = pSecondary->load();
    engine.wood          = pWood->load();
    engine.volume        = pVolume->load();

    engine.waveguideGain     = pWgGain->load();
    engine.decayValue        = pDecay->load();
    engine.attackValue       = pAttack->load();
    engine.releaseValue      = pRelease->load();
    engine.attackCurve       = pAttackCurve->load();
    engine.releaseCurve      = pReleaseCurve->load();
    engine.followTime        = pFollow->load();
    engine.vibratoRate       = pVibRate->load();
    engine.vibratoBreathDepth= pVibBreath->load();
    engine.legatoTime        = pLegatoTime->load();
    engine.legatoEnabled     = pLegato->load()     > 0.5f;
    engine.aulosTrack        = pTrack->load()      > 0.5f;
    engine.droneActive       = pDrone->load()      > 0.5f;
    engine.manualGateActive  = pManualGate->load() > 0.5f;

    const float breathBase = pBreath->load();
    const float vibBase    = pVibDepth->load();
    const float octave     = pOctave->load();

    auto ev = midi.cbegin();
    const auto evEnd = midi.cend();

    for (int i = 0; i < numSamples; ++i) {
        // Apply MIDI events scheduled at or before this sample.
        while (ev != evEnd && (*ev).samplePosition <= i) {
            const auto msg = (*ev).getMessage();
            if (msg.isNoteOn())            handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())      handleNoteOff(msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) handleAllOff();
            else if (msg.isPitchWheel())
                bendOct = ((msg.getPitchWheelValue() - 8192) / 8192.f)
                        * (kBendRangeSemis / 12.f);
            else if (msg.isController()) {
                const int   ccNum = msg.getControllerNumber();
                const float cc    = msg.getControllerValue() / 127.f;
                if (ccNum == 2)      breathCC = cc;  // breath
                else if (ccNum == 1) modWheel = cc;  // mod wheel
                else if (ccNum == 64) {              // sustain pedal
                    sustainDown = msg.getControllerValue() >= 64;
                    if (!sustainDown)
                        for (int v = 0; v < kMaxVoices; ++v)
                            if (voiceSustained[v]) { voiceGate[v] = false; voiceSustained[v] = false; }
                }
            }
            ++ev;
        }

        // CC2 breath and CC1 mod wheel add to the panel values (clamped);
        // with no controller connected these default to the params alone.
        engine.breath      = juce::jlimit(0.f, 1.f, breathBase + breathCC);
        engine.vibratoAtt  = juce::jlimit(0.f, 1.f, vibBase + modWheel);
        engine.pipeTune    = octave + bendOct;
        engine.fingerTune  = octave + bendOct;

        for (int v = 0; v < kMaxVoices; ++v)
            engine.gate[v] = voiceGate[v] ? 10.f : 0.f;

        const AulosEngine::Stereo s = engine.process();
        outL[i] = s.l * 0.1f;
        if (outR) outR[i] = s.r * 0.1f;
    }

    // Publish the engine's display state for the editor (relaxed is fine — a
    // visualizer tolerates a one-frame stale value).
    const auto& d = engine.getDisplay();
    auto st = [](std::atomic<float>& a, float v) { a.store(v, std::memory_order_relaxed); };
    st(displaySnap.pipeFreq, d.pipeFreq);
    st(displaySnap.activeFraction, d.activeFraction);
    st(displaySnap.activeFractionR, d.activeFractionR);
    st(displaySnap.bore, d.bore);
    st(displaySnap.breath, d.breath);
    st(displaySnap.breathR, d.breathR);
    st(displaySnap.overblow, d.overblow);
    st(displaySnap.overblowR, d.overblowR);
    st(displaySnap.reg, d.reg);
    st(displaySnap.regR, d.regR);
    st(displaySnap.rms, d.rms);
    st(displaySnap.rmsR, d.rmsR);
    st(displaySnap.air, d.air);
    st(displaySnap.chiff, d.chiff);
    st(displaySnap.chiffR, d.chiffR);
    st(displaySnap.reed, d.reed);
    st(displaySnap.saturation, d.saturation);
    st(displaySnap.saturationR, d.saturationR);
    st(displaySnap.pipeRatio, d.pipeRatio);
}

// ── State ────────────────────────────────────────────────────────────────────
void AulosAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void AulosAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* AulosAudioProcessor::createEditor() {
    return new AulosAudioProcessorEditor(*this);
}

// ── Plugin entry point ───────────────────────────────────────────────────────
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AulosAudioProcessor();
}
