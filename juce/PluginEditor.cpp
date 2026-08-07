////////////////////////////////////////////////////////////
//
//   PluginEditor.cpp
//
//   PipeComponent::drawTube is a JUCE port of PipeDisplay::drawTube from the
//   Rack module — same silhouette, standing-wave, air-line and marker math,
//   with NanoVG calls replaced by juce::Graphics.
//
////////////////////////////////////////////////////////////

#include "PluginEditor.h"

namespace {
    inline juce::Colour col(float r, float g, float b, float a) {
        return juce::Colour::fromFloatRGBA(juce::jlimit(0.f, 1.f, r),
                                           juce::jlimit(0.f, 1.f, g),
                                           juce::jlimit(0.f, 1.f, b),
                                           juce::jlimit(0.f, 1.f, a));
    }

    struct SliderSpec { const char* id; const char* label; };
    struct Section { const char* title; std::vector<SliderSpec> items; juce::Colour accent; };

    const std::vector<Section>& sections() {
        static const std::vector<Section> s = {
            {"VOICE",
             {{"reed","Reed"},{"bore","Bore"},{"drive","Drive"},{"res","Res"},
              {"air","Air"},{"chiff","Chiff"},{"secondary","2nd Pipe"},{"wood","Wood"}},
             juce::Colour::fromRGB(90, 150, 235)},
            {"RESONATOR / ENVELOPE",
             {{"wgGain","WG Gain"},{"decay","Decay"},{"attack","Attack"},{"release","Release"},
              {"attackCurve","Atk Crv"},{"releaseCurve","Rel Crv"},{"followTime","Follow"}},
             juce::Colour::fromRGB(90, 205, 180)},
            {"VIBRATO",
             {{"vibRate","Rate"},{"vibBreath","Breath"},{"vibDepth","Depth"}},
             juce::Colour::fromRGB(180, 140, 235)},
            {"MIX",
             {{"breath","Breath"},{"volume","Volume"},{"octave","Octave"}},
             juce::Colour::fromRGB(235, 180, 90)},
        };
        return s;
    }

    const std::vector<SliderSpec> kToggles = {
        {"legato","Legato"}, {"track","2nd Track"}, {"drone","Drone"}, {"manualGate","Man Gate"},
    };

    constexpr int kCellW   = 78;
    constexpr int kKnobH   = 74;   // knob + value textbox
    constexpr int kSecHdr  = 16;
    constexpr int kKnobLbl = 14;
    constexpr int kPipeH   = 108;
    constexpr int kHeaderH = 30;
    constexpr int kMargin  = 10;
    constexpr int kGap     = 8;
    constexpr int kColGap  = 24;   // gap between Vibrato and Mix columns

    const int kBlockH = kSecHdr + kKnobH + kKnobLbl + kGap;
}

// ── PipeComponent ────────────────────────────────────────────────────────────
void PipeComponent::paint(juce::Graphics& g) {
    const float W = (float)getWidth();
    const float H = (float)getHeight();

    g.setColour(juce::Colour::fromRGBA(12, 12, 20, 220));
    g.fillRoundedRectangle(0.f, 0.f, W, H, 4.f);

    const float gap  = 2.f;
    const float rowH = (H - gap) * 0.5f;
    const float airReserve = rowH * 0.7f;
    const float t = (float)(juce::Time::getMillisecondCounterHiRes() * 0.001);

    const float lWidth = W * 0.5f - airReserve;
    const float rWidth = juce::jlimit(0.25f, 1.0f, data.pipeRatio * 0.5f) * W - airReserve;

    drawTube(g, data.activeFraction, data.bore, data.breath, data.overblow,
             data.reg, data.rms, data.air, data.chiff, data.reed, data.saturation,
             3.f, 0.f, lWidth, rowH, t);
    drawTube(g, data.activeFractionR, data.bore, data.breathR, data.overblowR,
             data.regR, data.rmsR, data.air, data.chiffR, data.reed, data.saturationR,
             3.f, rowH + gap, rWidth, rowH, t);
}

void PipeComponent::drawTube(juce::Graphics& g,
        float activeFraction, float bore, float breath, float overblow,
        float registerValue, float rms, float air, float chiff, float edge,
        float saturation, float bx, float by, float bw, float bh, float timeSec) {

    const float margin  = 3.f;
    const float tubeX   = bx + margin;
    const float tubeW   = bw - margin * 2.f;
    const float centerY = by + bh * 0.5f;
    const float baseH   = bh * 0.28f;

    float bodyStage = juce::jlimit(0.f, 1.f, (bore - 0.3f) / 0.7f);
    bodyStage = bodyStage * bodyStage;
    float bellStage = juce::jlimit(0.f, 1.f, (bore - 0.75f) / 0.25f);
    bellStage = bellStage * bellStage;
    const float bodyWidenAmount = 0.2f;
    const float bellFlareAmount = 0.4f;
    float boreShape = juce::jlimit(0.f, 1.f, bodyStage * bodyWidenAmount + bellStage * bellFlareAmount);

    const float mouthHalf = baseH * (0.88f - boreShape * 0.44f);
    const float bellHalf  = baseH * (1.00f + boreShape * 1.00f);
    const float shapeExponent = 1.0f + boreShape * 3.0f;
    auto tubeHalfHeight = [&](float xNorm) {
        return mouthHalf + std::pow(xNorm, shapeExponent) * (bellHalf - mouthHalf);
    };

    const int CURVE_STEPS = 64;

    // ── Tube fill ─────────────────────────────────────────────────────────────
    {
        float tubeAlpha = 0.4f + breath * 0.5f;
        juce::Path p;
        for (int i = 0; i <= CURVE_STEPS; ++i) {
            float xNorm = (float)i / (float)CURVE_STEPS;
            float x = tubeX + xNorm * tubeW;
            float y = centerY - tubeHalfHeight(xNorm);
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        for (int i = CURVE_STEPS; i >= 0; --i) {
            float xNorm = (float)i / (float)CURVE_STEPS;
            float x = tubeX + xNorm * tubeW;
            float y = centerY + tubeHalfHeight(xNorm);
            p.lineTo(x, y);
        }
        p.closeSubPath();
        g.setColour(col(0.06f + saturation * 0.08f, 0.06f, 0.12f, tubeAlpha));
        g.fillPath(p);
    }

    // ── Standing wave ──────────────────────────────────────────────────────────
    float modeNumber = exp2_taylor5(registerValue);
    float energyKick = juce::jlimit(0.f, 1.f, (overblow - 0.35f) / 0.3f);
    modeNumber      *= (1.f + energyKick);

    float boreEnd = juce::jlimit(0.05f, 1.f, activeFraction);
    float brightness = 0.15f + 0.85f * juce::jmax(rms * 4.f, breath);

    const int N = 64;
    for (int i = 0; i < N; ++i) {
        float xNorm = (float)i / (float)(N - 1);
        float xPix  = tubeX + xNorm * tubeW;
        float segW  = tubeW / (float)N + 1.f;
        float halfH = tubeHalfHeight(xNorm);

        float xDist    = xNorm - boreEnd;
        float leakGain = (xDist <= 0.f) ? 1.f : std::exp(-xDist * 6.5f);

        float phase    = modeNumber * juce::MathConstants<float>::pi * xNorm / boreEnd;
        float pressure = aulosDspSin(phase);
        float amp      = pressure * pressure * leakGain;
        float glow     = amp * brightness;

        float warm = juce::jmax(pressure, 0.f);
        float cool = juce::jmax(-pressure, 0.f);

        float r = 0.08f + warm * (0.85f + edge * 0.15f) + saturation * warm * 0.20f;
        float gg = 0.05f + warm * (0.30f + edge * 0.20f) + saturation * warm * 0.10f;
        float b = 0.12f + cool * (0.90f - edge * 0.30f);
        float a = (0.35f + glow * 0.65f) * leakGain;

        g.setColour(col(r, gg, b, a));
        g.fillRect(juce::Rectangle<float>(xPix - segW * 0.5f, centerY - halfH, segW, halfH * 2.f));
    }

    // ── Embouchure dot ─────────────────────────────────────────────────────────
    if (breath > 0.01f || rms > 0.01f) {
        float dotX   = tubeX;
        float dotR   = tubeHalfHeight(0.f) * (0.25f + rms * 0.35f);
        float dotGlow = juce::jmax(rms * 3.f, breath);
        g.setColour(col(0.9f + edge * 0.1f, 0.85f - edge * 0.15f, 0.7f - edge * 0.3f, dotGlow * 0.85f));
        g.fillEllipse(dotX - dotR, centerY - dotR, dotR * 2.f, dotR * 2.f);
    }

    // ── Air lines ──────────────────────────────────────────────────────────────
    {
        float bellX = tubeX + tubeW;
        float bellH = tubeHalfHeight(1.f);
        const int NUM_LINES = 9;

        for (int i = 0; i < NUM_LINES; ++i) {
            float centerIdx = (NUM_LINES - 1) * 0.5f;
            float tAbs = std::fabs((float)i - centerIdx) / centerIdx;

            float lineActive;
            if (i >= 1 && i <= (NUM_LINES - 2))
                lineActive = air * breath;
            else
                lineActive = juce::jmax(chiff * 0.9f, air * breath * 0.15f);
            if (lineActive < 0.02f) continue;

            float ySign   = ((float)i < centerIdx) ? -1.f : (i > centerIdx ? 1.f : 0.f);
            float yOffset = bellH * tAbs * 0.95f * ySign;

            float baseLen = bellH * (1.4f + air * 0.8f + chiff * 1.0f);
            float lineLen = baseLen * (1.f - tAbs * 0.6f);

            float flutter = aulosDspSin(timeSec * (2.5f + i * 1.3f)) * bellH * 0.05f * lineActive;

            float ctrlX = bellX + lineLen * 0.5f;
            float ctrlY = centerY + yOffset * 1.15f + flutter;
            float endX  = bellX + lineLen;
            float endY  = centerY + yOffset * 1.35f + flutter * 1.5f;

            float alpha = juce::jlimit(0.f, 1.f, lineActive * (0.9f - tAbs * 0.3f));
            float cr = 0.25f + chiff * 0.55f;
            float cg = 0.55f + chiff * 0.35f;
            float cb = 1.0f;
            float sw = 1.5f + (1.f - tAbs) * 0.8f + chiff * 1.0f;

            juce::Path line;
            line.startNewSubPath(bellX, centerY + yOffset);
            line.quadraticTo(ctrlX, ctrlY, endX, endY);
            g.setColour(col(cr, cg, cb, alpha));
            g.strokePath(line, juce::PathStrokeType(sw));
        }
    }

    // ── Fingering marker ───────────────────────────────────────────────────────
    float markerNorm = boreEnd;
    if (breath > 0.02f) {
        float markerX = tubeX + markerNorm * tubeW;
        float markerH = tubeHalfHeight(markerNorm);
        float alpha   = 0.5f + breath * 0.5f;
        g.setColour(col(1.f, 0.9f, 0.5f, alpha));
        if (overblow < 0.05f) {
            g.drawLine(markerX, centerY - markerH - 1.f, markerX, centerY + markerH + 1.f, 1.5f);
        } else {
            float ellipseW = 2.5f + overblow * 7.f;
            float ellipseH = markerH * (0.35f + overblow * 0.45f);
            g.drawEllipse(markerX - ellipseW, centerY - ellipseH, ellipseW * 2.f, ellipseH * 2.f, 1.5f);
        }
    }

    // ── Overblow outline ───────────────────────────────────────────────────────
    if (overblow > 0.02f) {
        juce::Path p;
        for (int i = 0; i <= CURVE_STEPS; ++i) {
            float xNorm = (float)i / (float)CURVE_STEPS;
            float x = tubeX + xNorm * tubeW;
            float y = centerY - tubeHalfHeight(xNorm) - 1.5f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        for (int i = CURVE_STEPS; i >= 0; --i) {
            float xNorm = (float)i / (float)CURVE_STEPS;
            float x = tubeX + xNorm * tubeW;
            float y = centerY + tubeHalfHeight(xNorm) + 1.5f;
            p.lineTo(x, y);
        }
        p.closeSubPath();
        g.setColour(col(1.f, 0.85f, 0.3f, overblow * 0.9f));
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }
}

// ── LookAndFeel ──────────────────────────────────────────────────────────────
AulosLookAndFeel::AulosLookAndFeel() {
    setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(210, 210, 220));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(180, 140, 235));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(40, 41, 50));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(70, 72, 84));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour::fromRGB(34, 35, 42));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour::fromRGB(70, 90, 140));
}

void AulosLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
        float sliderPos, float startAngle, float endAngle, juce::Slider& s) {
    auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(5.f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float toAngle = startAngle + sliderPos * (endAngle - startAngle);
    const float lineW = juce::jmax(2.5f, radius * 0.14f);
    const float arcR  = radius - lineW * 0.5f;

    juce::Path back;
    back.addCentredArc(cx, cy, arcR, arcR, 0.f, startAngle, endAngle, true);
    g.setColour(juce::Colour::fromRGB(50, 50, 60));
    g.strokePath(back, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const juce::Colour accent = s.findColour(juce::Slider::rotarySliderFillColourId);
    juce::Path val;
    val.addCentredArc(cx, cy, arcR, arcR, 0.f, startAngle, toAngle, true);
    g.setColour(accent);
    g.strokePath(val, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float kr = arcR * 0.6f;
    g.setColour(juce::Colour::fromRGB(28, 28, 36));
    g.fillEllipse(cx - kr, cy - kr, kr * 2.f, kr * 2.f);
    g.setColour(accent.withAlpha(0.4f));
    g.drawEllipse(cx - kr, cy - kr, kr * 2.f, kr * 2.f, 1.f);

    const float ca = std::cos(toAngle - juce::MathConstants<float>::halfPi);
    const float sa = std::sin(toAngle - juce::MathConstants<float>::halfPi);
    juce::Point<float> tip (cx + arcR * ca,        cy + arcR * sa);
    juce::Point<float> base(cx + kr * 0.25f * ca,  cy + kr * 0.25f * sa);
    g.setColour(accent);
    g.drawLine(juce::Line<float>(base, tip), juce::jmax(2.f, lineW * 0.7f));
}

// ── Editor ───────────────────────────────────────────────────────────────────
AulosAudioProcessorEditor::AulosAudioProcessorEditor(AulosAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p) {

    setLookAndFeel(&lnf);
    addAndMakeVisible(pipe);

    presetBox.setTextWhenNothingSelected("Presets");
    for (int i = 0; i < proc.getNumPresets(); ++i)
        presetBox.addItem(proc.getPresetName(i), i + 1);
    presetBox.setSelectedId(proc.getCurrentPreset() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        if (id > 0) proc.applyPreset(id - 1);
    };
    addAndMakeVisible(presetBox);

    // Section rotary knobs.
    for (int sIdx = 0; sIdx < (int)sections().size(); ++sIdx) {
        const auto& sec = sections()[sIdx];
        for (const auto& item : sec.items) {
            auto* c = new SliderCtl();
            c->section = sIdx;
            c->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            c->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kCellW - 6, 14);
            c->slider.setColour(juce::Slider::rotarySliderFillColourId, sec.accent);
            addAndMakeVisible(c->slider);

            c->label.setText(item.label, juce::dontSendNotification);
            c->label.setJustificationType(juce::Justification::centred);
            c->label.setFont(juce::FontOptions(11.f));
            addAndMakeVisible(c->label);

            c->attach = std::make_unique<SliderAttachment>(proc.apvts, item.id, c->slider);
            sliders.add(c);
        }
        auto* lbl = new juce::Label();
        lbl->setText(sec.title, juce::dontSendNotification);
        lbl->setFont(juce::FontOptions(11.f, juce::Font::bold));
        lbl->setColour(juce::Label::textColourId, sec.accent);
        addAndMakeVisible(lbl);
        sectionLabels.add(lbl);
    }

    // Legato time — a horizontal slider in the articulation row (section 99).
    {
        auto* c = new SliderCtl();
        c->section = 99;
        c->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        c->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
        addAndMakeVisible(c->slider);
        c->label.setText("Legato ms", juce::dontSendNotification);
        c->label.setJustificationType(juce::Justification::centredLeft);
        c->label.setFont(juce::FontOptions(11.f));
        addAndMakeVisible(c->label);
        c->attach = std::make_unique<SliderAttachment>(proc.apvts, "legatoTime", c->slider);
        sliders.add(c);
    }

    // Toggle buttons.
    for (const auto& t : kToggles) {
        auto* c = new ToggleCtl();
        c->button.setButtonText(t.label);
        addAndMakeVisible(c->button);
        c->attach = std::make_unique<ButtonAttachment>(proc.apvts, t.id, c->button);
        toggles.add(c);
    }

    setSize(kMargin * 2 + 8 * kCellW, 540);
    startTimerHz(30);
}

AulosAudioProcessorEditor::~AulosAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

void AulosAudioProcessorEditor::paint(juce::Graphics& g) {
    juce::ColourGradient grad(juce::Colour::fromRGB(30, 31, 38), 0.f, 0.f,
                              juce::Colour::fromRGB(18, 18, 24), 0.f, (float)getHeight(), false);
    g.setGradientFill(grad);
    g.fillAll();

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.f, juce::Font::bold));
    g.drawText("AULOS", kMargin, kMargin, 160, kHeaderH, juce::Justification::centredLeft);
    g.setColour(juce::Colour::fromRGB(150, 150, 165));
    g.setFont(juce::FontOptions(11.f));
    g.drawText("CVfunk - physical-model wind synth", kMargin + 96, kMargin, 280, kHeaderH,
               juce::Justification::centredLeft);

    for (int i = 0; i < sectionRects.size(); ++i) {
        auto r = sectionRects[i].toFloat();
        g.setColour(juce::Colour::fromRGB(26, 27, 34));
        g.fillRoundedRectangle(r, 6.f);
        g.setColour(sections()[i].accent.withAlpha(0.35f));
        g.drawRoundedRectangle(r, 6.f, 1.f);
    }
}

void AulosAudioProcessorEditor::resized() {
    const int W = getWidth();
    int y = kMargin;

    auto header = juce::Rectangle<int>(kMargin, y, W - 2 * kMargin, kHeaderH);
    presetBox.setBounds(header.removeFromRight(210).withSizeKeepingCentre(210, 24));
    y += kHeaderH + 6;

    pipe.setBounds(kMargin, y, W - 2 * kMargin, kPipeH);
    y += kPipeH + kMargin;

    int start[4]; int acc = 0;
    for (int s = 0; s < 4; ++s) { start[s] = acc; acc += (int)sections()[s].items.size(); }

    auto place = [&](int s, int x, int yTop) {
        const int n = (int)sections()[s].items.size();
        sectionLabels[s]->setBounds(x, yTop, n * kCellW, kSecHdr);
        const int ky = yTop + kSecHdr;
        for (int i = 0; i < n; ++i) {
            auto* c = sliders[start[s] + i];
            const int kx = x + i * kCellW;
            c->slider.setBounds(kx, ky, kCellW, kKnobH);
            c->label.setBounds(kx, ky + kKnobH, kCellW, kKnobLbl);
        }
    };
    auto rectFor = [&](int s, int x, int yTop) {
        const int n = (int)sections()[s].items.size();
        return juce::Rectangle<int>(x - 4, yTop - 2, n * kCellW + 8, kSecHdr + kKnobH + kKnobLbl + 4);
    };

    sectionRects.clearQuick();
    int yb = y;
    place(0, kMargin, yb); sectionRects.add(rectFor(0, kMargin, yb)); yb += kBlockH;
    place(1, kMargin, yb); sectionRects.add(rectFor(1, kMargin, yb)); yb += kBlockH;

    const int vibX = kMargin;
    const int mixX = kMargin + 3 * kCellW + kColGap;
    place(2, vibX, yb); sectionRects.add(rectFor(2, vibX, yb));
    place(3, mixX, yb); sectionRects.add(rectFor(3, mixX, yb));
    yb += kBlockH;

    // Articulation row: legato-time slider (last slider) + toggles.
    const int rowY = yb;
    auto* lt = sliders.getLast();
    lt->label.setBounds(kMargin, rowY + 3, 64, 20);
    lt->slider.setBounds(kMargin + 66, rowY, 200, 26);

    const int togStart = kMargin + 66 + 200 + 16;
    const int tw = (W - kMargin - togStart) / (int)toggles.size();
    for (int i = 0; i < toggles.size(); ++i)
        toggles[i]->button.setBounds(togStart + i * tw, rowY, tw, 26);
}

void AulosAudioProcessorEditor::timerCallback() {
    auto ld = [](std::atomic<float>& a) { return a.load(std::memory_order_relaxed); };
    auto& s = proc.displaySnap;
    pipe.data.pipeFreq        = ld(s.pipeFreq);
    pipe.data.activeFraction  = ld(s.activeFraction);
    pipe.data.activeFractionR = ld(s.activeFractionR);
    pipe.data.bore            = ld(s.bore);
    pipe.data.breath          = ld(s.breath);
    pipe.data.breathR         = ld(s.breathR);
    pipe.data.overblow        = ld(s.overblow);
    pipe.data.overblowR       = ld(s.overblowR);
    pipe.data.reg             = ld(s.reg);
    pipe.data.regR            = ld(s.regR);
    pipe.data.rms             = ld(s.rms);
    pipe.data.rmsR            = ld(s.rmsR);
    pipe.data.air             = ld(s.air);
    pipe.data.chiff           = ld(s.chiff);
    pipe.data.chiffR          = ld(s.chiffR);
    pipe.data.reed            = ld(s.reed);
    pipe.data.saturation      = ld(s.saturation);
    pipe.data.saturationR     = ld(s.saturationR);
    pipe.data.pipeRatio       = ld(s.pipeRatio);
    pipe.repaint();
}
