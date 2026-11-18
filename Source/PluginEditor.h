#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Interface : 6 étoiles sur fond noir, bandeau 4:1 redimensionnable (300×75 → 1200×300)
class WantedEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WantedEditor (WantedProcessor&);
    ~WantedEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void setLevel (int n, bool fromUser);
    void startShake (int n);
    int  starAt (juce::Point<float> p) const;
    juce::Rectangle<float> starBounds (int i) const;
    juce::AffineTransform shakeTransform() const;

    WantedProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    juce::Path starShape;              // étoile unitaire (viewBox 30×30)

    int level = 0;                     // niveau affiché
    double heat = 0;                   // 0..100 pour le glisser vertical
    float dragY = 0; bool dragging = false;

    // clignotement des étoiles gagnées / perdues
    int blinkFrom = 0, blinkTo = 0; double blinkUntil = 0;

    // shake
    struct Shake { double amp = 0, rot = 0, dur = 0, freq = 0, t0 = 0, seed = 0; bool run = false; } shk;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WantedEditor)
};
