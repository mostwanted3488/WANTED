/*
  ==============================================================================
   WANTED — moteur de distorsion (indépendant de JUCE)

   Topologie « Rat » (même circuit que UAD Raw / Pro Co Rat) :
     1) étage op-amp  : gain = 1 + Rdist·(Y1 + Y2)
          Y1 = 47 Ω + 2.2 µF  → passe-haut ~1.54 kHz (boost des aigus avant clip)
          Y2 = 560 Ω + 4.7 µF → passe-haut ~60 Hz
     2) bande passante de l'op-amp (LM308) : plus de gain = plus sombre
     3) rails ±4.5 V puis diodes silicium dures (~±0.6 V), légère asymétrie
     4) filtre de sortie passe-bas + compensation
   Tout l'étage « wet » tourne suréchantillonné (≥ 352 kHz) pour limiter l'aliasing.

   Les 6 étoiles = position du pot DISTORTION (+ poussée au-delà de la pédale à 5★/6★).
  ==============================================================================
*/
#pragma once
#include <cmath>
#include <algorithm>
#include <array>

namespace wanted
{
static constexpr int kMaxStars = 6;
static constexpr double kPi = 3.14159265358979323846;

//==============================================================================
struct StarSettings
{
    double drive, gHi, gLo, gbwHz, toneHz, make;
};

inline StarSettings settingsForStar (int n)
{
    static constexpr double POT  [7] = { 0, .58, .68, .77, .85, .93, 1.0 };
    static constexpr double BOOST[7] = { 1, 1, 1, 1.1, 1.35, 1.8, 3.0 };
    static constexpr double FILT [7] = { 5200, 5200, 5500, 5800, 6100, 6400, 6800 };
    static constexpr double MAKE [7] = { .62, .62, .5, .42, .38, .35, .33 };

    n = std::clamp (n, 0, kMaxStars);
    const int k = std::max (n, 1);                         // à 0★ on garde les réglages de 1★ (wet coupé)
    const double taper = (std::pow (10.0, 2.0 * POT[k]) - 1.0) / 99.0;   // pot log
    const double Rd = 100000.0 * taper;
    const double G  = 1.0 + Rd / 47.0;

    StarSettings s;
    s.drive  = 0.5 * BOOST[k];                             // 1.0 numérique ≈ 0.5 V guitare
    s.gHi    = Rd / 47.0;
    s.gLo    = Rd / 560.0;
    s.gbwHz  = std::clamp (1.6e6 / G, 1900.0, 20000.0);
    s.toneHz = FILT[k];
    s.make   = MAKE[k];
    return s;
}

// Crescendo de volume perçu après auto gain : chaque étoile ≈ +0.5 dB de plus que la précédente
inline double loudnessOffsetDb (int n)
{
    static constexpr double OFF[7] = { 0, -1.25, -0.75, -0.25, 0.25, 0.75, 1.25 };
    return OFF[std::clamp (n, 0, kMaxStars)];
}

//==============================================================================
// Biquad (TDF-II, double) — formules identiques à la Web Audio API
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }

    void set (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    static double clampF (double fs, double f) { return std::clamp (f, 5.0, fs * 0.49); }

    // qDb : Q en dB (convention Web Audio pour lowpass/highpass)
    void setLowpass (double fs, double f, double qDb) noexcept
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * std::pow (10.0, qDb / 20));
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighpass (double fs, double f, double qDb) noexcept
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * std::pow (10.0, qDb / 20));
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighShelf (double fs, double f, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt (2.0), q = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + q), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - q),
             (A + 1) - (A - 1) * c + q, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - q);
    }
    // passe-haut 1er ordre (bilinéaire) = admittance R+C en série
    void setFirstOrderHP (double fs, double fc) noexcept
    {
        const double K = std::tan (kPi * clampF (fs, fc) / fs), n = 1 / (1 + K);
        b0 = n; b1 = -n; b2 = 0; a1 = (K - 1) / (K + 1); a2 = 0;
    }
};

//==============================================================================
// Courbe rails + diodes (table 8192 points, interpolation linéaire — comme un WaveShaper)
class RatShaper
{
public:
    RatShaper()
    {
        const double VR = 4.5, VD = 0.62, k = 2.4, T = std::tanh (1.6);
        auto f = [&] (double x)
        {
            double v = VR * std::tanh (1.6 * x) / T + 0.015 * VR;                 // rails op-amp + asymétrie
            return (v / std::pow (1 + std::pow (std::abs (v) / VD, k), 1 / k)) / VD; // diodes 1N914
        };
        for (int i = 0; i < N; ++i) table[(size_t) i] = (float) f ((double) i / (N - 1) * 2 - 1);
        const float mid = table[(size_t) ((N - 1) >> 1)];
        for (auto& t : table) t -= mid;                                              // recentre le DC
    }
    // u = signal / 4.5 V ; au-delà de ±1 = écrêtage aux rails
    inline float operator() (float u) const noexcept
    {
        const float pos = (std::clamp (u, -1.0f, 1.0f) + 1.0f) * 0.5f * (float) (N - 1);
        const int i = std::min ((int) pos, N - 2);
        const float fr = pos - (float) i;
        return table[(size_t) i] + fr * (table[(size_t) i + 1] - table[(size_t) i]);
    }
private:
    static constexpr int N = 8192;
    std::array<float, N> table {};
};

//==============================================================================
// Une voie « wet » (un canal), à la fréquence suréchantillonnée
struct WetChannel
{
    Biquad hpHi, hpLo, gbw, dc, tone, tone2;

    void prepare (double osRate)
    {
        hpHi.setFirstOrderHP (osRate, 1539.0);
        hpLo.setFirstOrderHP (osRate, 60.5);
        dc.setHighpass (osRate, 32.0, -3.0);
        tone2.setLowpass (osRate, 14000.0, -6.0);
        reset();
    }
    void reset() { hpHi.reset(); hpLo.reset(); gbw.reset(); dc.reset(); tone.reset(); tone2.reset(); }

    void setDynamicFilters (double osRate, double gbwHz, double toneHz)
    {
        gbw.setLowpass  (osRate, gbwHz, -6.0);
        tone.setLowpass (osRate, toneHz, -3.0);
    }

    inline float process (float x, const StarSettings& s, const RatShaper& shaper) noexcept
    {
        const double d   = s.drive * x;
        const double sum = d + s.gHi * hpHi.process (d) + s.gLo * hpLo.process (d);
        const double g   = gbw.process (sum);
        double y = shaper ((float) (g / 4.5));
        y = dc.process (y);
        y = tone.process (y);
        y = tone2.process (y);
        return (float) (y * s.make);
    }
};

//==============================================================================
// Pondération K (volume perçu, comme les LUFS)
struct KWeight
{
    Biquad shelf, hp;
    void prepare (double fs) { shelf.setHighShelf (fs, 1681.0, 4.0); hp.setHighpass (fs, 38.0, -6.0); shelf.reset(); hp.reset(); }
    inline double process (double x) noexcept { return hp.process (shelf.process (x)); }
};

//==============================================================================
// Limiteur (seuil -0.5 dBFS, ratio 20, attaque 1 ms, release 80 ms) + écrêtage doux final (≤ -0.1 dBFS)
struct OutputGuard
{
    double fs = 48000, grDb = 0, aAtt = 0, aRel = 0;
    void prepare (double sampleRate)
    {
        fs = sampleRate; grDb = 0;
        aAtt = 1 - std::exp (-1.0 / (0.001 * fs));
        aRel = 1 - std::exp (-1.0 / (0.080 * fs));
    }
    // peak : max des |x| sur les canaux pour cet échantillon → renvoie le gain linéaire
    inline double gainFor (double peak) noexcept
    {
        const double lvl = 20 * std::log10 (std::max (peak, 1e-12));
        const double over = lvl - (-0.5);
        const double target = over > 0 ? over * (1 - 1.0 / 20) : 0;
        grDb += (target - grDb) * (target > grDb ? aAtt : aRel);
        return std::pow (10.0, -grDb / 20);
    }
    static inline float safeClip (float x) noexcept
    {
        const float T = 0.89f, M = 0.988f, ax = std::min (std::abs (x), 1.5f);
        const float y = ax < T ? ax : T + (M - T) * std::tanh ((ax - T) / (M - T));
        return std::copysign (y, x);
    }
};

//==============================================================================
// Auto gain : la disto garde le volume perçu du son d'entrée
struct AutoGain
{
    double eIn = 0, eOut = 0, gDb = 0, aE = 0, fs = 48000;
    void prepare (double sampleRate) { fs = sampleRate; eIn = eOut = gDb = 0; aE = 1 - std::exp (-1.0 / (0.30 * fs)); }
    inline void push (double kIn2, double kOut2) noexcept { eIn += (kIn2 - eIn) * aE; eOut += (kOut2 - eOut) * aE; }
    // appelé toutes les `n` échantillons ; renvoie le gain linéaire courant
    double update (int n, bool active, double offsetDb = 0) noexcept
    {
        double target = 0;
        if (active && eIn > 1e-6 && eOut > 1e-7)
            target = std::clamp (10 * std::log10 (eIn / eOut) + offsetDb, -30.0, 24.0);
        if (eIn > 1e-6 || ! active)                       // gèle pendant les silences
            gDb += (target - gDb) * (1 - std::exp (-(double) n / (0.20 * fs)));
        return std::pow (10.0, gDb / 20);
    }
};

} // namespace wanted
