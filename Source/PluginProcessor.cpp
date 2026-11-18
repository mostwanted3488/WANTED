#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kSub = 32;            // granularité du lissage des réglages (échantillons hôte)
    constexpr double kSmoothSec = 0.03; // 30 ms, comme la preview
}

juce::AudioProcessorValueTreeState::ParameterLayout WantedProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "stars", 1 }, "Wanted Level", 0, wanted::kMaxStars, 0,
        juce::AudioParameterIntAttributes().withLabel ("stars")));
    return layout;
}

WantedProcessor::WantedProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WANTED", createLayout())
{
    starsParam = apvts.getRawParameterValue ("stars");
}

bool WantedProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void WantedProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = juce::jmax (64, samplesPerBlock);

    // suréchantillonnage : ≥ ~700 kHz à 44.1/48 kHz
    osFactor = fs <= 50000 ? 16 : fs <= 100000 ? 8 : fs <= 200000 ? 4 : 2;
    const int stages = (int) std::round (std::log2 ((double) osFactor));
    osRate = fs * osFactor;

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, (size_t) stages, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) maxBlock);

    const int latency = (int) std::round (oversampler->getLatencyInSamples());
    setLatencySamples (latency);

    dryBuffer.setSize (2, maxBlock, false, true, true);
    wmBuf.assign ((size_t) (maxBlock / kSub + 2), 0.0);
    dryDelay.prepare ({ fs, (juce::uint32) maxBlock, 2 });
    dryDelay.setMaximumDelayInSamples (latency + 8);
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();

    for (auto& w : wet) w.prepare (osRate);
    for (auto& k : kIn)  k.prepare (fs);
    for (auto& k : kOut) k.prepare (fs);
    autoGain.prepare (fs);
    guard.prepare (fs);

    const int stars = (int) std::round (starsParam->load());
    cur = tgt = wanted::settingsForStar (stars);
    wetMix = wetTarget = stars > 0 ? 1.0 : 0.0;
    autoLin = 1.0;
    lastGbw = lastTone = -1;
}

void WantedProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    for (int pos = 0; pos < buffer.getNumSamples(); pos += maxBlock)
        processChunk (buffer, pos, juce::jmin (maxBlock, buffer.getNumSamples() - pos));
}

void WantedProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int num)
{
    const int nc = juce::jlimit (1, 2, juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()));
    const int stars = juce::jlimit (0, wanted::kMaxStars, (int) std::round (starsParam->load()));
    const bool active = stars > 0;
    if (active) tgt = wanted::settingsForStar (stars);   // à 0★ on garde le dernier réglage (wet coupé)
    wetTarget = active ? 1.0 : 0.0;

    // 1) copie du signal sec
    for (int c = 0; c < nc; ++c)
        dryBuffer.copyFrom (c, 0, buffer, c, start, num);

    // 2) voie disto suréchantillonnée
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) nc, (size_t) start, (size_t) num);
    auto osBlock = oversampler->processSamplesUp (block);

    const int nSub = (num + kSub - 1) / kSub;
    double* wm = wmBuf.data();                       // pré-alloué dans prepareToPlay

    wm[0] = wetMix;
    for (int s = 0; s < nSub; ++s)
    {
        const int b0 = s * kSub, bn = juce::jmin (kSub, num - b0);
        const double a = 1.0 - std::exp (-(double) bn / (kSmoothSec * fs));
        cur.drive  += (tgt.drive  - cur.drive)  * a;
        cur.gHi    += (tgt.gHi    - cur.gHi)    * a;
        cur.gLo    += (tgt.gLo    - cur.gLo)    * a;
        cur.gbwHz  += (tgt.gbwHz  - cur.gbwHz)  * a;
        cur.toneHz += (tgt.toneHz - cur.toneHz) * a;
        cur.make   += (tgt.make   - cur.make)   * a;
        wetMix     += (wetTarget  - wetMix)     * a;
        if (std::abs (wetMix - wetTarget) < 1e-5) wetMix = wetTarget;
        wm[s + 1] = wetMix;

        if (std::abs (cur.gbwHz - lastGbw) > 0.05 || std::abs (cur.toneHz - lastTone) > 0.05)
        {
            for (int c = 0; c < nc; ++c) wet[(size_t) c].setDynamicFilters (osRate, cur.gbwHz, cur.toneHz);
            lastGbw = cur.gbwHz; lastTone = cur.toneHz;
        }

        const int o0 = b0 * osFactor, on = bn * osFactor;
        for (int c = 0; c < nc; ++c)
        {
            auto* d = osBlock.getChannelPointer ((size_t) c);
            auto& w = wet[(size_t) c];
            for (int i = o0; i < o0 + on; ++i)
                d[i] = w.process (d[i], cur, shaper);
        }
    }

    oversampler->processSamplesDown (block);

    // 3) auto gain + garde de sortie + mélange sec / disto (sec retardé de la latence)
    float* out[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (nc > 1 ? 1 : 0, start) };
    const float* dry[2] = { dryBuffer.getReadPointer (0), dryBuffer.getReadPointer (nc > 1 ? 1 : 0) };

    double autoFrom = autoLin;
    for (int s = 0; s < nSub; ++s)
    {
        const int b0 = s * kSub, bn = juce::jmin (kSub, num - b0);

        // mesure du volume perçu (entrée vs disto avant auto gain)
        for (int i = b0; i < b0 + bn; ++i)
        {
            double ein = 0, eout = 0;
            for (int c = 0; c < nc; ++c)
            {
                const double ki = kIn[(size_t) c].process (dry[c][i]);
                const double ko = kOut[(size_t) c].process (out[c][i]);
                ein += ki * ki; eout += ko * ko;
            }
            autoGain.push (ein / nc, eout / nc);
        }
        const double autoTo = autoGain.update (bn, active, wanted::loudnessOffsetDb (stars));

        for (int j = 0; j < bn; ++j)
        {
            const int i = b0 + j;
            const double t = (double) (j + 1) / bn;
            const double ag = autoFrom + (autoTo - autoFrom) * t;
            const double mix = wm[s] + (wm[s + 1] - wm[s]) * t;

            double w[2] = { 0, 0 }, peak = 0;
            for (int c = 0; c < nc; ++c) { w[c] = out[c][i] * ag; peak = std::max (peak, std::abs (w[c])); }
            const double lim = guard.gainFor (peak);

            for (int c = 0; c < nc; ++c)
            {
                dryDelay.pushSample (c, dry[c][i]);
                const float dd = dryDelay.popSample (c);
                const float ws = wanted::OutputGuard::safeClip ((float) (w[c] * lim));
                out[c][i] = (float) (dd * (1.0 - mix) + ws * mix);
            }
        }
        autoFrom = autoTo;
    }
    autoLin = autoFrom;
}

//==============================================================================
void WantedProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("uiHeight", uiHeight.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void WantedProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            uiHeight = juce::jlimit (75, 300, (int) state.getProperty ("uiHeight", 120));
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* WantedProcessor::createEditor() { return new WantedEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WantedProcessor(); }
