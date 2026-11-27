#pragma once
#include <JuceHeader.h>
#include "../Blocks.h"

// Turns a SpectrumTap into a smoothed spectrum (in dB) that's quick to draw at any width.
// 4096-point FFT, Hann window, a 3 dB/octave tilt (so a guitar reads flat-ish, like most analyzers),
// fast rise and slow fall, plus a slowly falling peak line.
class SpectrumMeter
{
public:
    static constexpr int order = 12, N = 1 << order, bins = N / 2 + 1;
    SpectrumMeter() : fft (order)
    {
        window.resize (N);
        float sum = 0;
        for (int i = 0; i < N; ++i) { window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (float) N); sum += window[(size_t) i]; }
        norm = 2.0f / sum;
        work.assign (2 * N, 0.0f);
        smooth.assign (bins, -140.0f);
        peak.assign (bins, -140.0f);
    }

    void update (const SpectrumTap& tap, float sampleRate)
    {
        rate = sampleRate;
        tap.read (work.data(), N);
        for (int i = 0; i < N; ++i) work[(size_t) i] *= window[(size_t) i];
        std::fill (work.begin() + N, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        for (int k = 1; k < bins; ++k)
        {
            const float f = (float) k * rate / (float) N;
            float db = juce::Decibels::gainToDecibels (work[(size_t) k] * norm, -140.0f);
            db += 3.0f * std::log2 (std::max (20.0f, f) / 1000.0f);          // tilt
            auto& s = smooth[(size_t) k];
            s = db > s ? s + 0.6f * (db - s) : std::max (db, s - 1.6f);     // rise fast, fall ~50 dB/s
            auto& pk = peak[(size_t) k];
            pk = s > pk ? s : pk - 0.35f;
        }
    }

    // loudest bin in a frequency range (so low notes don't vanish between pixels)
    float dbIn (const std::vector<float>& v, float f0, float f1) const
    {
        const int k0 = juce::jlimit (1, bins - 1, (int) std::floor (f0 * N / rate));
        const int k1 = juce::jlimit (k0, bins - 1, (int) std::ceil (f1 * N / rate));
        float m = -140.0f;
        for (int k = k0; k <= k1; ++k) m = std::max (m, v[(size_t) k]);
        return m;
    }

    // a path across 'area'; xToFreq maps an x position to Hz, dbToY maps dB to y
    juce::Path makePath (juce::Rectangle<float> area, std::function<float (float)> xToFreq,
                         std::function<float (float)> dbToY, bool peaks, bool closed) const
    {
        juce::Path p;
        const auto& v = peaks ? peak : smooth;
        bool first = true;
        for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
        {
            const float f0 = xToFreq (x - 1.0f), f1 = xToFreq (x + 1.0f);
            const float y = juce::jlimit (area.getY(), area.getBottom(), dbToY (dbIn (v, f0, f1)));
            if (first) { p.startNewSubPath (x, closed ? area.getBottom() : y); if (closed) p.lineTo (x, y); first = false; }
            else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (area.getRight(), area.getBottom()); p.closeSubPath(); }
        return p;
    }

private:
    juce::dsp::FFT fft;
    std::vector<float> window, work, smooth, peak;
    float norm = 1, rate = 48000;
};

// The slide-up analyzer strip between the chain and the scene bar.
class AnalyzerStrip : public juce::Component, private juce::Timer
{
public:
    std::function<int()> selectedBlockId;   // the block selected in the chain (for the block taps)
    std::function<float()> sampleRate;

    AnalyzerStrip()
    {
        tapBox.addItem ("Output", 1);
        tapBox.addItem ("Input", 2);
        tapBox.addItem ("Into the selected block", 3);
        tapBox.addItem ("Out of the selected block", 4);
        tapBox.setSelectedId (1, juce::dontSendNotification);
        tapBox.onChange = [this] { apply(); };
        addAndMakeVisible (tapBox);
    }
    ~AnalyzerStrip() override { SpectrumTap::get (0).on = false; }

    void setActive (bool a)
    {
        active = a;
        apply();
        if (a) startTimerHz (30); else stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff000000));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (juce::Colour (0xff2a2c31));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        const auto a = plot();
        g.setFont (juce::FontOptions (10.5f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xFor (f, a);
            g.setColour (juce::Colour (0xff1f2125)); g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (juce::Colour (0xff5d6068));
            g.drawText (f >= 1000 ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f), (int) x + 3, (int) a.getBottom() - 14, 34, 12, juce::Justification::left);
        }
        for (float d : { -24.0f, -48.0f, -72.0f })
        {
            g.setColour (juce::Colour (0xff1f2125)); g.drawHorizontalLine ((int) yFor (d, a), a.getX(), a.getRight());
        }
        auto toF = [this, a] (float x) { return freqFor (x, a); };
        auto toY = [this, a] (float db) { return yFor (db, a); };
        const auto c = juce::Colour (0xff00f0d0);
        auto fill = meter.makePath (a, toF, toY, false, true);
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.45f), 0, a.getY(), c.withAlpha (0.05f), 0, a.getBottom(), false));
        g.fillPath (fill);
        g.setColour (c);
        g.strokePath (meter.makePath (a, toF, toY, false, false), juce::PathStrokeType (1.4f));
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (meter.makePath (a, toF, toY, true, false), juce::PathStrokeType (1.0f));
        if (needsBlock() && (! selectedBlockId || selectedBlockId() < 0))
        {
            g.setColour (juce::Colour (0xff8a8f98));
            g.setFont (juce::FontOptions (12.5f));
            g.drawText ("Select a block in the chain", a, juce::Justification::centred);
        }
    }

    void resized() override { tapBox.setBounds (getWidth() - 222, 8, 210, 26); }

private:
    bool needsBlock() const { return tapBox.getSelectedId() >= 3; }
    void apply()
    {
        auto& t = SpectrumTap::get (0);
        const int id = tapBox.getSelectedId();
        t.where = id == 2 ? SpectrumTap::Input : id == 3 ? SpectrumTap::BeforeBlock : id == 4 ? SpectrumTap::AfterBlock : SpectrumTap::Output;
        t.blockId = selectedBlockId ? selectedBlockId() : -1;
        t.on = active;
    }
    void timerCallback() override
    {
        auto& t = SpectrumTap::get (0);
        const int id = selectedBlockId ? selectedBlockId() : -1;
        if (t.blockId.load() != id) t.blockId = id;
        meter.update (t, sampleRate ? sampleRate() : 48000.0f);
        repaint();
    }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedTop (2.0f); }
    float xFor (float f, juce::Rectangle<float> a) const { return a.getX() + a.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
    float freqFor (float x, juce::Rectangle<float> a) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth())); }
    float yFor (float db, juce::Rectangle<float> a) const { return a.getY() + a.getHeight() * juce::jlimit (0.0f, 1.0f, (6.0f - db) / 96.0f); }

    juce::ComboBox tapBox;
    SpectrumMeter meter;
    bool active = false;
};
