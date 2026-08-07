////////////////////////////////////////////////////////////
//
//   render_wav.cpp
//
//   Phase 1 offline harness: drive the Rack-free AulosEngine with a short note
//   sequence and write the result to a 16-bit stereo WAV. This proves the
//   extracted DSP compiles and sounds correct with no VCV Rack present.
//
//   Usage:  render_wav [out.wav]
//
////////////////////////////////////////////////////////////

#include "AulosEngine.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Minimal 16-bit PCM stereo WAV writer.
static bool writeWav(const std::string& path, const std::vector<float>& interleaved,
                     int channels, int sampleRate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const uint32_t numSamples = (uint32_t)interleaved.size();
    const uint32_t dataBytes  = numSamples * sizeof(int16_t);
    const uint32_t byteRate   = (uint32_t)sampleRate * channels * sizeof(int16_t);
    const uint16_t blockAlign = (uint16_t)(channels * sizeof(int16_t));

    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);
    w32(36 + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    w32(16);                       // PCM fmt chunk size
    w16(1);                        // PCM
    w16((uint16_t)channels);
    w32((uint32_t)sampleRate);
    w32(byteRate);
    w16(blockAlign);
    w16(16);                       // bits per sample
    std::fwrite("data", 1, 4, f);
    w32(dataBytes);

    for (float s : interleaved) {
        float c = s < -1.f ? -1.f : (s > 1.f ? 1.f : s);
        int16_t v = (int16_t)std::lround(c * 32767.f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
    return true;
}

int main(int argc, char** argv) {
    const std::string outPath = (argc > 1) ? argv[1] : "aulos_test.wav";
    const int   sr = 48000;
    const float dt = 1.f / sr;

    AulosEngine engine;
    engine.setSampleRate((float)sr);

    // Voice defaults matching the module's configParam defaults, nudged for an
    // audible flute-ish tone.
    engine.reed   = 0.1f;
    engine.bore   = 0.35f;
    engine.drive  = 0.6f;
    engine.res    = 0.6f;
    engine.air    = 0.3f;
    engine.chiff  = 0.4f;
    engine.wood   = 0.1f;
    engine.breath = 0.7f;
    engine.volume = 0.5f;
    engine.nVoices = 1;
    engine.rOutputConnected = false;   // single melodic voice for this test

    // Simple note sequence: C4, E4, G4, C5 — each 0.6s on, 0.15s gap.
    const float notesVoct[4] = { 0.f, 4.f / 12.f, 7.f / 12.f, 12.f / 12.f };
    const float onSec  = 0.6f;
    const float gapSec = 0.15f;

    std::vector<float> out;   // interleaved stereo
    out.reserve((size_t)(sr * 4.f));

    auto renderSegment = [&](float voct, bool gateOn, float seconds) {
        const int n = (int)(seconds * sr);
        for (int i = 0; i < n; ++i) {
            engine.fingerVoct[0] = voct;
            engine.pipeVoct[0]   = voct;
            engine.gate[0]       = gateOn ? 10.f : 0.f;
            AulosEngine::Stereo s = engine.process();
            // Rack voltages are ~±10V; scale to ±1.0 for the WAV.
            out.push_back(s.l * 0.1f);
            out.push_back(s.r * 0.1f);
        }
        (void)dt;
    };

    for (int nIdx = 0; nIdx < 4; ++nIdx) {
        renderSegment(notesVoct[nIdx], true,  onSec);
        renderSegment(notesVoct[nIdx], false, gapSec);
    }

    if (!writeWav(outPath, out, 2, sr)) {
        std::fprintf(stderr, "Failed to write %s\n", outPath.c_str());
        return 1;
    }

    // Report peak so we can confirm the engine actually produced signal.
    float peak = 0.f;
    for (float s : out) peak = std::max(peak, std::fabs(s));
    std::printf("Wrote %s: %zu frames, peak %.4f\n",
                outPath.c_str(), out.size() / 2, peak);
    return 0;
}
