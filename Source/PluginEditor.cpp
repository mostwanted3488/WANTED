#include "PluginEditor.h"

namespace
{
    // shake crescendo par étoile (valeurs de la preview, pour une fenêtre de 120 px de haut)
    constexpr double AMP [7] = { 0, 2, 4, 8, 13, 14, 16 };          // px
    constexpr double ROT [7] = { 0, 0, .4, 1, 2, 1.2, 1.6 };        // degrés
    constexpr double DUR [7] = { 0, .22, .3, .42, .55, .45, .6 };   // secondes
    constexpr double FREQ[7] = { 0, 38, 34, 30, 26, 48, 70 };

    double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    const juce::Colour kRed (0xffff2a2a);
}

WantedEditor::WantedEditor (WantedProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    param = proc.apvts.getParameter ("stars");

    // étoile à 5 branches : rayon ext. 13, int. 5.6, centre (15,16) dans un carré 30×30
    for (int i = 0; i < 10; ++i)
    {
        const float r = (i % 2) ? 5.6f : 13.0f, a = (float) i * juce::MathConstants<float>::pi / 5.0f;
        const juce::Point<float> pt (15.0f + r * std::sin (a), 16.0f - r * std::cos (a));
        if (i == 0) starShape.startNewSubPath (pt); else starShape.lineTo (pt);
    }
    starShape.closeSubPath();

    level = (int) std::round (param->convertFrom0to1 (param->getValue()));
    heat  = level / 6.0 * 100.0;

    setOpaque (true);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (300, 75, 1200, 300);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (4.0);
    const int h = juce::jlimit (75, 300, proc.uiHeight.load());
    setSize (h * 4, h);

    startTimerHz (60);
}

//==============================================================================
juce::Rectangle<float> WantedEditor::starBounds (int i) const
{
    const float H = (float) getHeight(), size = H * 0.47f, gap = H * 0.117f;
    const float total = 6 * size + 5 * gap;
    const float x0 = ((float) getWidth() - total) * 0.5f;
    return { x0 + (float) i * (size + gap), (H - size) * 0.5f, size, size };
}

juce::AffineTransform WantedEditor::shakeTransform() const
{
    if (! shk.run) return {};
    const double t = now() - shk.t0, k = juce::jmax (0.0, 1.0 - t / shk.dur), e = k * k;
    if (k <= 0) return {};
    const double sc = getHeight() / 120.0;
    juce::Random rnd;
    const double p = t * shk.freq + shk.seed;
    const double x = (std::sin (p * 1.7) + std::sin (p * 3.1) * .5 + (rnd.nextDouble() - .5) * .6) * shk.amp * e * sc;
    const double y = (std::cos (p * 2.3) + std::sin (p * 4.7) * .4 + (rnd.nextDouble() - .5) * .6) * shk.amp * e * sc;
    const double r = std::sin (p * 1.3) * shk.rot * e;
    const double s = 1 + shk.amp / 400 * e;
    const float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
    return juce::AffineTransform::scale ((float) s, (float) s, cx, cy)
               .rotated ((float) juce::degreesToRadians (r), cx, cy)
               .translated ((float) x, (float) y);
}

void WantedEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const double t = now();
    const bool blinking = t < blinkUntil;
    const bool blinkOff = blinking && std::fmod (t, 0.25) >= 0.125;
    const bool maxRed = level == 6 && std::fmod (t, 0.3) >= 0.15;
    const float sc = (float) getHeight() / 120.0f;
    const auto shake = shakeTransform();

    for (int i = 0; i < 6; ++i)
    {
        if (blinkOff && i >= blinkFrom && i < blinkTo) continue;

        const auto b = starBounds (i);
        auto path = starShape;
        path.applyTransform (juce::AffineTransform::scale (b.getWidth() / 30.0f)
                                 .translated (b.getX(), b.getY())
                                 .followedBy (shake));
        const float unit = b.getWidth() / 30.0f;

        if (i < level)
        {
            const auto fill = maxRed ? kRed : juce::Colours::white;
            juce::DropShadow (maxRed ? kRed.withAlpha (0.7f) : juce::Colours::white.withAlpha (0.35f),
                              juce::roundToInt ((maxRed ? 10.0f : 6.0f) * sc), {}).drawForPath (g, path);
            g.setColour (fill);
            g.fillPath (path);
            g.setColour (juce::Colours::black);
            g.strokePath (path, juce::PathStrokeType (1.4f * unit, juce::PathStrokeType::curved));
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillPath (path);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.strokePath (path, juce::PathStrokeType (1.2f * unit, juce::PathStrokeType::curved));
        }
    }
}

void WantedEditor::resized()
{
    proc.uiHeight = getHeight();
}

//==============================================================================
void WantedEditor::timerCallback()
{
    // suit l'automation / le DAW
    const int p = (int) std::round (param->convertFrom0to1 (param->getValue()));
    if (p != level && ! dragging)
    {
        blinkFrom = juce::jmin (p, level); blinkTo = juce::jmax (p, level);
        blinkUntil = now() + 1.8;
        level = p; heat = level / 6.0 * 100.0;
    }
    if (shk.run && now() - shk.t0 > shk.dur) shk.run = false;
    repaint();
}

void WantedEditor::setLevel (int n, bool fromUser)
{
    n = juce::jlimit (0, 6, n);
    if (n == level) return;
    blinkFrom = juce::jmin (n, level); blinkTo = juce::jmax (n, level);
    blinkUntil = now() + 1.8;
    level = n;

    const float v = param->convertTo0to1 ((float) n);
    if (fromUser && ! dragging) param->beginChangeGesture();
    param->setValueNotifyingHost (v);
    if (fromUser && ! dragging) param->endChangeGesture();
    repaint();
}

void WantedEditor::startShake (int n)
{
    if (n < 1 || n > 6) return;
    shk = { AMP[n], ROT[n], DUR[n], FREQ[n], now(), juce::Random::getSystemRandom().nextDouble() * 100, true };
}

int WantedEditor::starAt (juce::Point<float> p) const
{
    for (int i = 0; i < 6; ++i)
        if (starBounds (i).contains (p)) return i;
    return -1;
}

//==============================================================================
void WantedEditor::mouseDown (const juce::MouseEvent& e)
{
    const int i = starAt (e.position);
    if (i >= 0)
    {
        setLevel (level == i + 1 ? i : i + 1, true);
        heat = level / 6.0 * 100.0;
        startShake (i + 1);
        return;
    }
    dragging = true;
    dragY = e.position.y;
    param->beginChangeGesture();
}

void WantedEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    heat = juce::jlimit (0.0, 100.0, heat - (e.position.y - dragY) * 0.35);
    dragY = e.position.y;
    const int n = heat <= 0.01 ? 0 : (int) std::ceil (heat / 100.0 * 6.0 - 1e-6);
    setLevel (n, true);
}

void WantedEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging) { dragging = false; param->endChangeGesture(); }
}

void WantedEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (starAt (e.position) < 0) { setLevel (0, true); heat = 0; }
}

void WantedEditor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1e-4f) return;
    setLevel (level + (w.deltaY > 0 ? 1 : -1), true);
    heat = level / 6.0 * 100.0;
}

bool WantedEditor::keyPressed (const juce::KeyPress& k)
{
    const auto c = k.getTextCharacter();
    if (c >= '0' && c <= '6')
    {
        setLevel (c - '0', true);
        heat = level / 6.0 * 100.0;
        startShake (c - '0');
        return true;
    }
    return false;
}
