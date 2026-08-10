////////////////////////////////////////////////////////////
//
//   PluginProcessor.h
//
//   JUCE AudioProcessor wrapping the Rack-free AulosEngine.
//   Phase 2 scaffold: MIDI-driven synth, core params, generic editor.
//
////////////////////////////////////////////////////////////

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AulosEngine.h"

class AulosAudioProcessor : public juce::AudioProcessor {
public:
    AulosAudioProcessor();
    ~AulosAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Aulos"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // Factory presets — driven from the editor, NOT exposed as host programs.
    // (Host programs call setCurrentProgram during state restore, which would
    // clobber the just-restored APVTS parameter values.)
    int getNumPresets() const;
    juce::String getPresetName(int index) const;
    void applyPreset(int index);
    int getCurrentPreset() const { return currentPreset; }

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // GUI-facing display snapshot, refreshed once per audio block. Atomic
    // fields so the editor's timer can read them off the message thread.
    struct DisplaySnapshot {
        std::atomic<float> pipeFreq{261.63f};
        std::atomic<float> activeFraction{1.f}, activeFractionR{1.f};
        std::atomic<float> bore{0.f};
        std::atomic<float> breath{0.f}, breathR{0.f};
        std::atomic<float> overblow{0.f}, overblowR{0.f};
        std::atomic<float> reg{0.f}, regR{0.f};
        std::atomic<float> rms{0.f}, rmsR{0.f};
        std::atomic<float> air{0.f};
        std::atomic<float> chiff{0.f}, chiffR{0.f};
        std::atomic<float> reed{0.f};
        std::atomic<float> saturation{0.f}, saturationR{0.f};
        std::atomic<float> pipeRatio{1.f};
    };
    DisplaySnapshot displaySnap;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    AulosEngine engine;

    static constexpr int kMaxVoices = AULOS_MAX_POLY;
    int      voiceNote[kMaxVoices] = {};
    bool     voiceGate[kMaxVoices] = {};
    bool     voiceSustained[kMaxVoices] = {};   // held by sustain pedal after note-off
    uint32_t voiceAge[kMaxVoices]  = {};
    float    voiceVelocity[kMaxVoices] = {};   // 0..1, captured at note-on
    uint32_t ageCounter = 0;
    bool     sustainDown = false;

    int      currentPreset = 0;

    void handleNoteOn(int note, float velocity);
    void handleNoteOff(int note);
    void handleAllOff();

    std::atomic<float>* pReed      = nullptr;
    std::atomic<float>* pBore      = nullptr;
    std::atomic<float>* pDrive     = nullptr;
    std::atomic<float>* pRes       = nullptr;
    std::atomic<float>* pAir       = nullptr;
    std::atomic<float>* pChiff     = nullptr;
    std::atomic<float>* pSecondary = nullptr;
    std::atomic<float>* pWood      = nullptr;
    std::atomic<float>* pBreath    = nullptr;
    std::atomic<float>* pVolume    = nullptr;
    std::atomic<float>* pOctave    = nullptr;

    // Advanced / context-menu equivalents.
    std::atomic<float>* pWgGain       = nullptr;
    std::atomic<float>* pDecay        = nullptr;
    std::atomic<float>* pAttack       = nullptr;
    std::atomic<float>* pRelease      = nullptr;
    std::atomic<float>* pAttackCurve  = nullptr;
    std::atomic<float>* pReleaseCurve = nullptr;
    std::atomic<float>* pFollow       = nullptr;
    std::atomic<float>* pVibRate      = nullptr;
    std::atomic<float>* pVibBreath    = nullptr;
    std::atomic<float>* pVibDepth     = nullptr;
    std::atomic<float>* pLegatoTime   = nullptr;
    std::atomic<float>* pLegato       = nullptr;
    std::atomic<float>* pTrack        = nullptr;
    std::atomic<float>* pDrone        = nullptr;
    std::atomic<float>* pManualGate   = nullptr;
    std::atomic<float>* pVelBreath    = nullptr;

    // Live MIDI modulation state (updated from CC / pitch-bend events).
    static constexpr float kBendRangeSemis = 2.f;
    float breathCC = 0.f;   // CC2 breath controller, 0..1
    float modWheel = 0.f;   // CC1 mod wheel -> vibrato depth, 0..1
    float bendOct  = 0.f;   // pitch-bend offset in octaves

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AulosAudioProcessor)
};
