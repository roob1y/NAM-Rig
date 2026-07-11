#pragma once
// PedalboardPanel — the front-of-amp PEDALBOARD editor (full redesign, 2026-07-10;
// Env/Comp folded into the generic pool, 2026-07-11). Drives the Stage-B pool on
// rig/PedalboardBlock.h (pbS{i}* params + the shared envfilter*/comp* pair for the
// two singleton types). Three zones, AmpliTube-style:
//
//   DECK (middle)   : EVERY placed pedal at once, left-to-right in processing order,
//                     each drawn as the tall stomp enclosure (drives host the REAL
//                     DrivePedal widget; Env/Comp/Mod/Delay get the matching
//                     StompPedal face). All knobs live — edit in place, scroll when
//                     the board outgrows the view.
//   RACK (right)    : the pedal palette — category headers with the ACTUAL pedals
//                     underneath (Green Drive, Gold Horse, Digi Delay, Envelope
//                     Filter, Compressor, ...). DRAG one onto the deck or the chain
//                     to place it; CLICK adds to the trunk end. Env/Comp are
//                     SINGLETONS — their row greys out once already on the board.
//   CHAIN (bottom)  : the signal-flow graph — IN -> trunk pedals -> split -> Amp A /
//                     Amp B lanes. DRAG nodes to reorder and to move between
//                     trunk/lanes; every pedal (including Env/Comp) is a full
//                     citizen here. Right-click a node for Route / Remove.
//
// Removed on purpose (board is now ALWAYS the front section — the processor forces
// the board path on): the ENABLE toggle, the Env 1st/Comp 1st buttons (Env/Comp are
// independently placeable/reorderable now, no fixed relative order) and the IMPORT
// LEGACY button. pbEnabled/pbFrontOrder stay registered (automation-index
// stability) but neither is read or written anymore.
//
// Reordering: the pool processes free slots in INDEX order per lane, so a reorder
// REPACKS the pbS{i}* unions — snapshot all 8 slots, rearrange, write back only the
// params that changed (message thread, full change gestures). Removed slots reset
// to defaults so the next add starts clean — for Env/Comp that also resets their
// shared envfilter*/comp* params (outside the per-slot union), see resetSlotToDefaults.

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>
#include "RigLookAndFeel.h"
#include "Panels.h" // BlockPanel, LabeledKnob, SegmentedControl, Footswitch, DrivePedal,
                    // MenuSectionHeader, paintDriveGlyph, DriveBlock/CompBlock statics

namespace nam_rig::ui
{

class PedalboardPanel : public BlockPanel,
                        public juce::DragAndDropContainer,
                        private juce::Timer
{
public:
    // ================================================================ constants
    static constexpr int kSlots = 8;          // free pool slots (pbS0..7)
    static constexpr int kPalW = 190;         // palette column width
    static constexpr int kChainH = 88;        // chain strip height
    static constexpr int kCellW = 220;        // pedal DESIGN width (faces are laid out at this)
    static constexpr int kPedalDesignH = 366; // pedal DESIGN height (the tuned face skeleton)
    static constexpr float kPedalScale = 0.85f; // deck render scale — same face, smaller
    static constexpr int kCellGap = 16;       // deck cell gap (patch cable lives here)

    // "nothing selected" sentinel. Every placed pedal (including Env/Comp, now
    // poolable) has a real slot index >= 0 — there are no other sentinel selections.
    static constexpr int kSelNone = -9;

    // Set by the editor: live compressor gain reduction (dB >= 0) for the comp
    // pedal's GR meter (reads the ACTIVE board comp via the processor).
    std::function<float()> compGrDbProvider;

private:
    // ================================================== palette catalogue (static)
    struct Item
    {
        int family;   // 1 drive, 2 mod, 3 delay
        int cat;      // drives: DriveBlock::Kind 1..4 (== pbS dCat); else 0
        int model;    // drives: bModel; mod: mType; delay: pModel
        juce::String name, sub;
        colors::AccentPair ap;
    };

    // Per-MODEL liveries (accent / tint / LED) like colors::driveModelAccent — each
    // real circuit owns its enclosure colour, so pedals read at a glance on the deck.
    static colors::AccentPair modAccent(int t)
    {
        using C = juce::Colour;
        switch (juce::jlimit(0, 4, t))
        {
        case 0:  return {C(0xff8ec9ea), C(0xff3f7ba3)}; // Chorus  — pale sky blue (the classic BBD chorus box)
        case 1:  return {C(0xffff9a45), C(0xffb85f1f)}; // Phaser  — script orange
        case 2:  return {C(0xff9fb6d4), C(0xff4d6079)}; // Flanger — jet steel
        case 3:  return {C(0xff46b878), C(0xff256b47)}; // Tremolo — deep leaf green (darker than Green Drive)
        default: return {C(0xffc9a2f2), C(0xff7e58b8)}; // Vibe    — swirl violet
        }
    }
    static colors::AccentPair delayAccent(int m)
    {
        using C = juce::Colour;
        switch (juce::jlimit(0, 2, m))
        {
        case 0:  return {C(0xffe6ecf2), C(0xff77828f), C(0xff6fd08a)}; // Digi Delay    — pearl body, GREEN LED
        case 1:  return {C(0xffbfe3cf), C(0xff1f4f3c), C(0xff58d98f)}; // Carbon Echo   — dark-sparkle body, mint print
        default: return {C(0xffe8dcc0), C(0xff77694f), C(0xffe8705a)}; // Memory Deluxe — cream chassis, warm red LED
        }
    }
    static colors::AccentPair envAccent()
    {
        const juce::Colour a(0xff6fd0c9);
        return colors::AccentPair{a, a.darker(0.72f)};
    }
    static colors::AccentPair compAccent()
    {
        const juce::Colour a(0xff8fb3ff);
        return colors::AccentPair{a, a.darker(0.72f)};
    }

    // User-facing names/subs are BRAND-FREE (like the drive catalogue: evocative
    // homages, plain circuit descriptors). The circuits they're voiced from stay
    // in code comments only.
    static const char *modName(int t)
    {
        static const char *n[5] = {"Chorus", "Phaser", "Flanger", "Tremolo", "Vibe"};
        return n[juce::jlimit(0, 4, t)];
    }
    static const char *modSub(int t)
    {
        static const char *n[5] = {"bucket-brigade chorus", "4-stage analog phaser", "analog jet flange",
                                   "opto volume tremolo", "photocell chorus-vibe"}; // phaser sub stays voice-neutral (Script/Block pill)
        return n[juce::jlimit(0, 4, t)];
    }
    static const char *delayName(int m)
    {
        static const char *n[3] = {"Digi Delay", "Carbon Echo", "Memory Deluxe"};
        return n[juce::jlimit(0, 2, m)];
    }
    static const char *delaySub(int m)
    {
        static const char *n[3] = {"pristine digital echo", "dark analog BBD", "warm bucket echo"};
        return n[juce::jlimit(0, 2, m)];
    }
    static const char *compModeName(int m)
    {
        static const char *n[4] = {"Clean", "OTA", "Opto", "FET"};
        return n[juce::jlimit(0, 3, m)];
    }
    static const char *compSub(int m)
    {
        static const char *n[4] = {"transparent studio VCA", "classic OTA squeeze",
                                   "smooth optical sustain", "'76-style FET, parallel"};
        return n[juce::jlimit(0, 3, m)];
    }

    std::vector<Item> buildCatalogue() const
    {
        using DB = nam_rig::DriveBlock;
        std::vector<Item> v;
        for (int cat = 1; cat <= 4; ++cat)
        {
            const auto k = (DB::Kind)cat;
            for (int m = 0; m < DB::modelCount(k); ++m)
                v.push_back({1, cat, m, juce::String(DB::modelName(k, m)),
                             juce::String(DB::modelSub(k, m)), colors::driveModelAccent(cat, m)});
        }
        for (int m = 0; m < 5; ++m)
            v.push_back({2, 0, m, modName(m), modSub(m), modAccent(m)});
        for (int m = 0; m < 3; ++m)
            v.push_back({3, 0, m, delayName(m), delaySub(m), delayAccent(m)});
        // Env/Comp are SINGLETONS (one physical engine each) — a single palette row per
        // type, not one per voice/mode (those are chosen via the pedal's own model pill
        // once it's placed). family 4 = Env, 5 = Comp; cat/model unused for both.
        v.push_back({4, 0, 0, "Envelope Filter", "FX25 / Q-Tron auto-wah", envAccent()});
        v.push_back({5, 0, 0, "Compressor", "Clean / OTA / Opto / FET", compAccent()});
        return v;
    }
    static const char *paletteHeaderFor(const Item &it)
    {
        if (it.family == 2) return "MODULATION";
        if (it.family == 3) return "DELAY";
        if (it.family == 4) return "ENV FILTER";
        if (it.family == 5) return "DYNAMICS";
        static const char *cn[5] = {"", "BOOST", "OVERDRIVE", "DISTORTION", "FUZZ"};
        return cn[juce::jlimit(0, 4, it.cat)];
    }

    // ==================================================== shared art (glyphs, body)
    static juce::PathStrokeType glyphStroke(float w)
    {
        return {w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded};
    }
    // Per-MODEL mod/delay silkscreens, drawn exactly like paintDriveGlyph: a fixed
    // 64x48 design space scaled-to-fit (uniform s, centred), minimal line art, the
    // house stroke (2*s, curved/rounded). One motif per real circuit.
    static void paintModGlyph(juce::Graphics &g, int type, juce::Rectangle<float> b, juce::Colour c)
    {
        const float s = juce::jmin(b.getWidth() / 64.0f, b.getHeight() / 48.0f);
        const auto xf = juce::AffineTransform::scale(s)
                            .translated(b.getCentreX() - 32.0f * s, b.getCentreY() - 24.0f * s);
        const float w = 2.0f * s + 0.4f;
        g.setColour(c);
        auto sine = [](float x0, float x1, float yC, float amp, float cycles, int n)
        {
            juce::Path sp;
            for (int k = 0; k <= n; ++k)
            {
                const float t = (float)k / (float)n;
                const float x = x0 + (x1 - x0) * t;
                const float y = yC - amp * std::sin(cycles * juce::MathConstants<float>::twoPi * t);
                if (k == 0) sp.startNewSubPath(x, y); else sp.lineTo(x, y);
            }
            return sp;
        };
        switch (juce::jlimit(0, 4, type))
        {
        case 0: // Chorus — a voice and its shimmering double
        {
            g.strokePath(sine(6.0f, 54.0f, 22.0f, 9.0f, 1.25f, 40), glyphStroke(w), xf);
            g.strokePath(sine(10.0f, 58.0f, 28.0f, 9.0f, 1.25f, 40), glyphStroke(w * 0.55f), xf);
            break;
        }
        case 1: // Phaser — the sweep with its four all-pass stages riding it
        {
            g.strokePath(sine(8.0f, 56.0f, 24.0f, 8.0f, 1.25f, 48), glyphStroke(w), xf);
            juce::Path dots;
            for (int k = 0; k < 4; ++k)
            {
                const float t = 0.125f + 0.25f * (float)k;
                const float x = 8.0f + 48.0f * t;
                const float y = 24.0f - 8.0f * std::sin(1.25f * juce::MathConstants<float>::twoPi * t);
                dots.addEllipse(x - 2.6f, y - 2.6f, 5.2f, 5.2f);
            }
            g.fillPath(dots, xf);
            break;
        }
        case 2: // Flanger — jet dart + swept contrails
        {
            juce::Path jet;
            jet.startNewSubPath(58.0f, 22.0f);
            jet.lineTo(38.0f, 13.0f); jet.lineTo(43.5f, 22.0f); jet.lineTo(38.0f, 31.0f);
            jet.closeSubPath();
            g.fillPath(jet, xf);
            juce::Path trail;
            trail.startNewSubPath(8.0f, 36.0f);  trail.quadraticTo(22.0f, 35.0f, 34.0f, 28.0f);
            trail.startNewSubPath(6.0f, 28.0f);  trail.quadraticTo(20.0f, 27.0f, 33.0f, 23.0f);
            trail.startNewSubPath(10.0f, 20.0f); trail.quadraticTo(22.0f, 19.0f, 33.0f, 17.5f);
            g.strokePath(trail, glyphStroke(w * 0.8f), xf);
            break;
        }
        case 3: // Tremolo — the signal chopped on/off (dashed wave)
        {
            juce::Path p;
            const int n = 55;
            bool pen = false;
            for (int k = 0; k <= n; ++k)
            {
                if ((k / 7) % 2 == 1) { pen = false; continue; } // 7-on / 7-off chop
                const float t = (float)k / (float)n;
                const float x = 6.0f + 52.0f * t;
                const float y = 24.0f - 9.0f * std::sin(1.5f * juce::MathConstants<float>::twoPi * t);
                if (!pen) { p.startNewSubPath(x, y); pen = true; } else p.lineTo(x, y);
            }
            g.strokePath(p, glyphStroke(w), xf);
            break;
        }
        default: // Vibe — the pulsing lamp at the heart of the photocell circuit
        {
            juce::Path lamp;
            lamp.addEllipse(32.0f - 7.5f, 24.0f - 7.5f, 15.0f, 15.0f);
            g.strokePath(lamp, glyphStroke(w), xf);
            juce::Path core;
            core.addEllipse(32.0f - 2.4f, 24.0f - 2.4f, 4.8f, 4.8f);
            g.fillPath(core, xf);
            juce::Path rays;
            for (int k = 0; k < 6; ++k)
            {
                const float a = juce::MathConstants<float>::pi * ((float)k / 3.0f + 1.0f / 6.0f);
                const float ca = std::cos(a), sa = std::sin(a);
                rays.startNewSubPath(32.0f + ca * 11.0f, 24.0f + sa * 11.0f);
                rays.lineTo(32.0f + ca * 15.5f, 24.0f + sa * 15.5f);
            }
            g.strokePath(rays, glyphStroke(w * 0.8f), xf);
            break;
        }
        }
    }
    static void paintDelayGlyph(juce::Graphics &g, int model, juce::Rectangle<float> b, juce::Colour c)
    {
        const float s = juce::jmin(b.getWidth() / 64.0f, b.getHeight() / 48.0f);
        const auto xf = juce::AffineTransform::scale(s)
                            .translated(b.getCentreX() - 32.0f * s, b.getCentreY() - 24.0f * s);
        const float w = 2.0f * s + 0.4f;
        switch (juce::jlimit(0, 2, model))
        {
        case 0: // Digi Delay — crisp square repeats, barely decaying (pristine digital)
            for (int k = 0; k < 4; ++k)
            {
                const float h = 34.0f - 6.5f * (float)k;
                juce::Path bar;
                bar.addRoundedRectangle(11.0f + 12.0f * (float)k, 24.0f - h * 0.5f, 6.5f, h, 1.8f);
                g.setColour(c.withMultipliedAlpha(1.0f - 0.15f * (float)k));
                g.fillPath(bar, xf);
            }
            break;
        case 1: // Carbon Echo — the bucket brigade, passed hand to hand
        {
            for (int k = 0; k < 3; ++k)
            {
                const float cx = 13.0f + 19.0f * (float)k;
                const float half = 6.5f - 1.1f * (float)k;   // shrinking buckets
                const float depth = 13.0f - 1.8f * (float)k;
                juce::Path bk;
                bk.startNewSubPath(cx - half, 17.0f);
                bk.lineTo(cx - half + 1.8f, 17.0f + depth - 2.5f);
                bk.quadraticTo(cx, 17.0f + depth + 2.0f, cx + half - 1.8f, 17.0f + depth - 2.5f);
                bk.lineTo(cx + half, 17.0f);
                g.setColour(c.withMultipliedAlpha(1.0f - 0.22f * (float)k));
                g.strokePath(bk, glyphStroke(w * 0.9f), xf);
            }
            juce::Path drops; // the signal mid-pass between buckets
            drops.addEllipse(21.5f, 9.5f, 3.4f, 3.4f);
            drops.addEllipse(40.5f, 11.0f, 3.4f, 3.4f);
            g.setColour(c.withMultipliedAlpha(0.85f));
            g.fillPath(drops, xf);
            break;
        }
        default: // Memory Deluxe — a note and its warm ripples
        {
            juce::Path dot;
            dot.addEllipse(13.0f, 21.0f, 6.0f, 6.0f);
            g.setColour(c);
            g.fillPath(dot, xf);
            for (int k = 0; k < 3; ++k)
            {
                const float r = 12.0f + 12.0f * (float)k;
                juce::Path arc; // opens rightward (JUCE angles: 0 = 12 o'clock, clockwise)
                arc.addCentredArc(16.0f, 24.0f, r, r, 0.0f, 0.873f, 2.269f, true);
                g.setColour(c.withMultipliedAlpha(1.0f - 0.26f * (float)k));
                g.strokePath(arc, glyphStroke(w * 0.9f), xf);
            }
            break;
        }
        }
    }
    static void paintCompGlyph(juce::Graphics &g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c); // classic diamond
        juce::Path d;
        d.startNewSubPath(b.getCentreX(), b.getY());
        d.lineTo(b.getRight(), b.getCentreY());
        d.lineTo(b.getCentreX(), b.getBottom());
        d.lineTo(b.getX(), b.getCentreY());
        d.closeSubPath();
        g.strokePath(d, glyphStroke(juce::jmax(1.4f, b.getHeight() * 0.06f)));
    }
    static void paintEnvGlyph(juce::Graphics &g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c); // SYMMETRIC resonant peak (wah band-pass) — mirrored control
        juce::Path p;    // points, so the watermark sits dead-centre on the face
        p.startNewSubPath(b.getX(), b.getBottom());
        p.cubicTo(b.getX() + b.getWidth() * 0.30f, b.getBottom(),
                  b.getCentreX() - b.getWidth() * 0.12f, b.getY() + b.getHeight() * 0.55f,
                  b.getCentreX(), b.getY());
        p.cubicTo(b.getCentreX() + b.getWidth() * 0.12f, b.getY() + b.getHeight() * 0.55f,
                  b.getRight() - b.getWidth() * 0.30f, b.getBottom(),
                  b.getRight(), b.getBottom());
        g.strokePath(p, glyphStroke(juce::jmax(1.4f, b.getHeight() * 0.06f)));
    }
    // Family glyph dispatch by the slot's own Type.
    void paintPedalGlyph(juce::Graphics &g, int sel, juce::Rectangle<float> box, juce::Colour col) const
    {
        const int t = slotType(sel);
        if (t == 1) paintDriveGlyph(g, paramC(sid(sel, "dCat")), paramC(sid(sel, "bModel")), box, col);
        else if (t == 2) paintModGlyph(g, paramC(sid(sel, "mType")), box, col);
        else if (t == 3) paintDelayGlyph(g, paramC(sid(sel, "pModel")), box, col);
        else if (t == 4) paintEnvGlyph(g, box, col);
        else if (t == 5) paintCompGlyph(g, box, col);
    }

    // The DrivePedal enclosure body recipe (neutral base + tint wash, dithered).
    // The wash is STRONG — the body takes the model's colour, like real enclosures,
    // so pedals (and chain nodes) read by colour at a glance.
    static void paintEnclosure(juce::Graphics &g, juce::Rectangle<float> r, juce::Colour tint,
                               bool on, bool selected, float radius)
    {
        const juce::Colour t = on ? tint : juce::Colour(0xff343a43);
        const juce::Colour encTop = juce::Colour(0xff262b33).overlaidWith(t.withAlpha(0.40f));
        const juce::Colour encBot = juce::Colour(0xff15181d).overlaidWith(t.withAlpha(0.12f));
        juce::ColourGradient base(encTop, 0.0f, r.getY(), encBot, 0.0f, r.getBottom(), false);
        dither::fillRoundedRectangle(g, base, r, radius);
        g.setColour((selected ? colors::accent : t).withAlpha(selected ? 1.0f : (on ? 0.55f : 0.34f)));
        g.drawRoundedRectangle(r.reduced(0.5f), radius, selected ? 1.8f : 1.4f);
    }

    // ================================================================ param access
    float paramF(const juce::String &id) const
    {
        if (auto *a = mApvts.getRawParameterValue(id)) return a->load();
        return 0.0f;
    }
    int paramC(const juce::String &id) const { return (int)paramF(id); }
    juce::String sid(int slot, const char *suf) const { return "pbS" + juce::String(slot) + suf; }

    int slotType(int i) const { return paramC(sid(i, "Type")); } // 0 Off,1 Drive,2 Mod,3 Delay,4 Env,5 Comp
    int slotLane(int i) const { return paramC(sid(i, "Lane")); } // 0 Both, 1 A, 2 B
    bool slotOn(int i) const { return paramF(sid(i, "On")) >= 0.5f; }
    bool slotUsed(int i) const { return slotType(i) != 0; }
    bool slotCanStereo(int i) const { const int t = slotType(i); return t == 2 || t == 3 || t == 4 || t == 5; } // Mod/Delay/Env/Comp
    bool slotStereo(int i) const { return slotCanStereo(i) && paramF(sid(i, "Stereo")) >= 0.5f; } // spans both amps
    // Env(4)/Comp(5) are SINGLETONS — only one physical engine each — so at most one
    // slot may hold each type. Used to gate the palette row and reject a second add.
    bool typePlaced(int type) const
    {
        for (int i = 0; i < kSlots; ++i) if (slotType(i) == type) return true;
        return false;
    }

    void writeNorm(const juce::String &id, float norm)
    {
        if (auto *pr = mApvts.getParameter(id))
        {
            if (std::abs(pr->getValue() - norm) <= 1.0e-6f) return; // no-op writes skipped
            pr->beginChangeGesture();
            pr->setValueNotifyingHost(norm);
            pr->endChangeGesture();
        }
    }
    void writeNat(const juce::String &id, float natural)
    {
        if (auto *pr = mApvts.getParameter(id)) writeNorm(id, pr->convertTo0to1(natural));
    }
    void writeChoice(const juce::String &id, int idx) { writeNat(id, (float)juce::jmax(0, idx)); }
    void writeBool(const juce::String &id, bool on) { writeNorm(id, on ? 1.0f : 0.0f); }

    // The per-slot param union (everything a slot owns; order irrelevant, Lane at [1]).
    static constexpr int kU = 37;
    static const char *const *unionSuffixes()
    {
        static const char *const u[kU] = {
            "Type", "Lane", "On", "Stereo",
            "dCat", "bModel", "bDrive", "bRange", "oDrive", "oTone", "oLevel",
            "dDrive", "dTone", "dLevel", "dMigrate", "fDrive", "fTone", "fLevel", "fGate",
            "mType", "mRate", "mSync", "mDepth", "mMix", "mFeedback", "mWave", "mPhaserVoice",
            "pModel", "pMode", "pTime", "pSync", "pFeedback", "pMix", "pMod", "pTone", "pLevel", "pChorusVib"};
        return u;
    }

    // ============================================================== pool structure
    struct Lanes { std::vector<int> l[3]; }; // slot indices per lane, in index (= process) order
    Lanes lanesNow() const
    {
        Lanes ln;
        for (int lane = 0; lane < 3; ++lane)
            for (int i = 0; i < kSlots; ++i)
                if (slotUsed(i) && slotLane(i) == lane) ln.l[lane].push_back(i);
        return ln;
    }
    int usedCount() const
    {
        int n = 0;
        for (int i = 0; i < kSlots; ++i) if (slotUsed(i)) ++n;
        return n;
    }
    bool boardFull() const { return usedCount() >= kSlots; }

    juce::String slotModelName(int i) const
    {
        const int t = slotType(i);
        if (t == 1)
        {
            const auto k = (nam_rig::DriveBlock::Kind)paramC(sid(i, "dCat"));
            return nam_rig::DriveBlock::modelName(k, paramC(sid(i, "bModel")));
        }
        if (t == 2) return modName(paramC(sid(i, "mType")));
        if (t == 3) return delayName(paramC(sid(i, "pModel")));
        if (t == 4) return paramC("envfilterVoice") == 1 ? "Q-Tron" : "FX25"; // current voice
        if (t == 5) return compModeName(paramC("compMode")); // current mode
        return {};
    }
    colors::AccentPair slotAccent(int i) const
    {
        const int t = slotType(i);
        if (t == 1) return colors::driveModelAccent(paramC(sid(i, "dCat")), paramC(sid(i, "bModel")));
        if (t == 2) return modAccent(paramC(sid(i, "mType")));
        if (t == 3) return delayAccent(paramC(sid(i, "pModel")));
        if (t == 4) return envAccent();
        if (t == 5) return compAccent();
        return colors::AccentPair{colors::outline, colors::outline.darker(0.4f)};
    }
    static const char *typeShort(int t)
    {
        switch (t)
        {
        case 1: return "DRIVE"; case 2: return "MOD"; case 3: return "DELAY";
        case 4: return "ENV";   case 5: return "COMP"; default: return "";
        }
    }

    // ============================================== repack (reorder / add / remove)
    // The pool runs free slots in INDEX order per lane, so structure edits rewrite the
    // slot unions: snapshot -> rearrange -> write back (only changed params). Vacated
    // slots reset to defaults so the next add starts from a clean pedal.
    struct Entry
    {
        int src;                      // >=0: existing slot; -1: new pedal from the palette
        int family = 0, cat = 0, model = 0; // used when src < 0
    };

    void commitArrangement(const std::vector<Entry> (&lanes)[3])
    {
        // snapshot every slot's natural values first (sources may be overwritten)
        float snap[kSlots][kU];
        auto *const *u = unionSuffixes();
        for (int i = 0; i < kSlots; ++i)
            for (int j = 0; j < kU; ++j)
                snap[i][j] = paramF(sid(i, u[j]));

        int t = 0;
        for (int lane = 0; lane < 3; ++lane)
            for (const auto &e : lanes[lane])
            {
                if (t >= kSlots) break;
                if (e.src >= 0)
                {
                    for (int j = 0; j < kU; ++j)
                    {
                        float v = snap[e.src][j];
                        if (j == 1) v = (float)lane; // union[1] == "Lane"
                        writeNat(sid(t, u[j]), v);
                    }
                }
                else // new pedal: defaults, then the identity fields
                {
                    resetSlotToDefaults(t);
                    writeChoice(sid(t, "Type"), e.family);
                    writeChoice(sid(t, "Lane"), lane);
                    writeBool(sid(t, "On"), true);
                    if (e.family == 1)
                    {
                        writeChoice(sid(t, "dCat"), e.cat);
                        writeNat(sid(t, "bModel"), (float)e.model);
                    }
                    else if (e.family == 2) writeChoice(sid(t, "mType"), e.model);
                    else if (e.family == 3) writeChoice(sid(t, "pModel"), e.model);
                    // family 4 (Env) / 5 (Comp): singleton — no per-slot sub-params to
                    // seed, the shared envfilter*/comp* params are already at whatever
                    // resetSlotToDefaults left them (or the user's prior settings, if
                    // this is a re-add without an intervening remove).
                }
                ++t;
            }
        for (; t < kSlots; ++t)
            resetSlotToDefaults(t); // Type back to Off + clean knobs
    }
    void resetSlotToDefaults(int i)
    {
        const int wasType = slotType(i); // capture before the union reset clears "Type"
        auto *const *u = unionSuffixes();
        for (int j = 0; j < kU; ++j)
            if (auto *pr = mApvts.getParameter(sid(i, u[j])))
                writeNorm(sid(i, u[j]), pr->getDefaultValue());
        // Env/Comp are SINGLETONS whose real params (envfilter*/comp*) live OUTSIDE
        // the per-slot union — vacating their slot must reset those too, or the next
        // add would inherit whatever tone was last dialed in instead of starting
        // clean like every other pedal type does.
        if (wasType == 4) resetParamGroup(envParamIds());
        else if (wasType == 5) resetParamGroup(compParamIds());
    }
    static const std::vector<juce::String> &envParamIds()
    {
        static const std::vector<juce::String> ids{
            "envfilterOn", "envfilterVoice", "envfilterSens", "envfilterRange", "envfilterMix",
            "envfilterReso", "envfilterMode", "envfilterDir", "envfilterQRange", "envfilterBoost",
            "envfilterResponse", "envfilterPos"};
        return ids;
    }
    static const std::vector<juce::String> &compParamIds()
    {
        static const std::vector<juce::String> ids{
            "compOn", "compSustain", "compAttack", "compLevel", "compRatio",
            "compRelease", "compMode", "compDry"};
        return ids;
    }
    void resetParamGroup(const std::vector<juce::String> &ids)
    {
        for (const auto &pid : ids)
            if (auto *pr = mApvts.getParameter(pid))
                writeNorm(pid, pr->getDefaultValue());
    }

    // Remove slot s from the lists (helper for the ops below).
    static void eraseSlot(std::vector<Entry> (&lanes)[3], int s)
    {
        for (auto &l : lanes)
            l.erase(std::remove_if(l.begin(), l.end(),
                                   [s](const Entry &e) { return e.src == s; }),
                    l.end());
    }
    void entriesNow(std::vector<Entry> (&lanes)[3]) const
    {
        const Lanes ln = lanesNow();
        for (int lane = 0; lane < 3; ++lane)
            for (int s : ln.l[lane]) lanes[lane].push_back({s});
    }

public:
    // ---- structural ops (called by the deck / chain / palette) ----
    void movePedal(int slot, int lane, int pos)
    {
        std::vector<Entry> lanes[3];
        entriesNow(lanes);
        eraseSlot(lanes, slot);
        lane = juce::jlimit(0, 2, lane);
        auto &dst = lanes[lane];
        pos = juce::jlimit(0, (int)dst.size(), pos);
        dst.insert(dst.begin() + pos, Entry{slot});
        commitArrangement(lanes);
        // selection follows the moved pedal to its packed index
        int idx = 0;
        for (int l = 0; l < lane; ++l) idx += (int)lanes[l].size();
        mSelSlot = idx + pos;
        structureChanged();
    }
    // Returns the packed slot index the new pedal landed on (-1 when full, or when
    // family is the Env/Comp singleton and it's already placed elsewhere).
    int addPedal(int family, int cat, int model, int lane, int pos)
    {
        if (boardFull()) return -1;
        if ((family == 4 || family == 5) && typePlaced(family)) return -1;
        std::vector<Entry> lanes[3];
        entriesNow(lanes);
        lane = juce::jlimit(0, 2, lane);
        auto &dst = lanes[lane];
        pos = juce::jlimit(0, (int)dst.size(), pos);
        dst.insert(dst.begin() + pos, Entry{-1, family, cat, model});
        commitArrangement(lanes);
        int idx = 0; // packed index = entries before it (trunk, then A, then B)
        for (int l = 0; l < lane; ++l) idx += (int)lanes[l].size();
        idx += pos;
        structureChanged();
        return idx;
    }
    int addPedalToTrunkEnd(int family, int cat, int model)
    {
        return addPedal(family, cat, model, 0, (int)lanesNow().l[0].size());
    }
    void removePedal(int slot)
    {
        std::vector<Entry> lanes[3];
        entriesNow(lanes);
        eraseSlot(lanes, slot);
        commitArrangement(lanes);
        if (mSelSlot == slot) mSelSlot = kSelNone;
        structureChanged();
    }
    void cycleLane(int slot) // Both -> Amp A -> Amp B -> Both (order kept: index untouched)
    {
        writeChoice(sid(slot, "Lane"), (slotLane(slot) + 1) % 3);
        structureChanged();
    }
    void routePedal(int slot, int lane)
    {
        writeChoice(sid(slot, "Lane"), juce::jlimit(0, 2, lane));
        structureChanged();
    }
    // Toggle a Mod/Delay slot's stereo span (L->Amp A, R->Amp B). No-op on Drive/Off.
    void setPedalStereo(int slot, bool on)
    {
        if (!slotCanStereo(slot)) return;
        writeBool(sid(slot, "Stereo"), on);
        structureChanged();
    }
    void togglePedalStereo(int slot) { setPedalStereo(slot, !slotStereo(slot)); }
    // Bypass/activate any placed pedal: Env/Comp map to their shared envfilterOn/
    // compOn params (the LED's actual authority even though they're now a normal
    // slot), everything else to its own pbS{i}On.
    void togglePedalOn(int sel)
    {
        if (sel < 0 || !slotUsed(sel)) return;
        const int t = slotType(sel);
        const juce::String id = t == 4 ? "envfilterOn"
                              : t == 5 ? "compOn"
                                       : sid(sel, "On");
        writeBool(id, paramF(id) < 0.5f);
    }
    void selectPedal(int sel, bool scrollTo)
    {
        mSelSlot = sel;
        // Scroll is deferred to the next timer tick: structural edits rebuild the deck
        // asynchronously, so the target cell may not exist yet.
        if (scrollTo) mScrollPending = true;
        if (isShowing()) grabKeyboardFocus(); // so Delete lands on the panel right away
        repaintZones();
    }
    int selectedPedal() const { return mSelSlot; }

    // ---- bin dropzone (appears while a free pedal is dragged on the deck; drop a
    //      pedal on it to remove it from the chain) ----
    void deckDragBegan(bool showBin)
    {
        mBinVisible = showBin;
        mBinHot = false;
        if (showBin) layoutBin();
        repaint();
    }
    void deckDragMoved(juce::Point<int> panelPos)
    {
        if (!mBinVisible) return;
        const bool hot = mBinRect.expanded(10).contains(panelPos);
        if (hot != mBinHot) { mBinHot = hot; repaint(mBinRect.expanded(16)); }
    }
    bool deckDragFinished(juce::Point<int> panelPos) // true = dropped on the bin
    {
        const bool hit = mBinVisible && mBinRect.expanded(10).contains(panelPos);
        mBinVisible = mBinHot = false;
        repaint();
        return hit;
    }

private:
    // Deferred so widget teardown never happens inside a child's own callback.
    void structureChanged()
    {
        juce::Component::SafePointer<PedalboardPanel> sp(this);
        juce::MessageManager::callAsync([sp] { if (sp != nullptr) sp->syncStructure(true); });
    }

    // (The compact pedal-face value pill lives in Panels.h now — StompPill — shared
    // with DrivePedal's per-model toggles so every switch matches across the deck.)

    // ================================================================= StompPedal
    // The tall stomp enclosure for the NON-drive pedals (Env / Comp / Mod / Delay),
    // mirroring the DrivePedal face: kind header + jewel LED, the authentic knob
    // set, a model-name pill (click -> styled picker), circuit subtitle, silkscreen
    // glyph in the slack above the footswitch. Knobs bind to the pool union
    // (pbS{i}m*/p*) for Mod/Delay, or to the shared envfilter*/comp* params for
    // Env/Comp (singleton engines — the slot index they happen to occupy is only
    // used for placement/selection, never for param IDs).
    class StompPedal : public juce::Component
    {
    public:
        enum Kind { Env, Comp, Mod, Delay };

        StompPedal(PedalboardPanel &b, Kind k, int slot) // slot ignored for Env/Comp
            : mBoard(b), mKind(k), mSlot(slot)
        {
            mOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                b.mApvts, onParamId(), mOn);
            mOn.onClick = [this] { refresh(); };
            addAndMakeVisible(mOn);
            rebuild();
        }

        // Param-authoritative refresh, driven by the panel timer (20 Hz).
        void refresh()
        {
            const int key = modelKey();
            if (key != mLastKey) { rebuild(); return; } // control set follows the model
            const bool on = mBoard.paramF(onParamId()) >= 0.5f;
            // tempo-sync owns the time/rate knob (always the first knob) when engaged
            if ((mKind == Mod || mKind == Delay) && !mRows.empty() && !mRows[0].empty())
                mRows[0][0]->setEnabled(
                    mBoard.paramC(pid(mKind == Mod ? "mSync" : "pSync")) == 0);
            mOn.setLit(on);
            for (auto *p : mPills) p->repaint(); // live value readouts
            if (mKind == Comp && mBoard.compGrDbProvider) // live GR meter (aux band, see resized())
            {
                const float gr = juce::jmax(0.0f, mBoard.compGrDbProvider());
                mGrSm += (gr > mGrSm ? 0.55f : 0.25f) * (gr - mGrSm); // grab fast, fall gently
                if (std::abs(mGrSm - mGrPainted) > 0.05f)
                {
                    mGrPainted = mGrSm;
                    repaint(mGrRect.expanded(2));
                }
            }
            if (on != mLastOn)
            {
                mLastOn = on;
                applyAccents(on);
                repaint();
            }
        }

        void paint(juce::Graphics &g) override
        {
            auto b = getLocalBounds().toFloat().reduced(1.0f);
            const bool on = mLastOn;
            paintEnclosure(g, b, mAp.tint, on, false, 16.0f);

            // header: kind (left) + STEREO chip (Mod/Delay/Env/Comp) + jewel LED (right)
            g.setColour(on ? mAp.accent : colors::caption);
            g.setFont(fonts::archivo(10.0f, fonts::Bold, 0.13f));
            g.drawText(mKindStr, mHeaderRect, juce::Justification::centredLeft);
            // MONO/STEREO state chip (Mod/Delay/Env/Comp). Lit accent = STEREO (spans
            // both amps); dim = MONO. Click toggles it (handled in mouseUp).
            if (!mStereoTagRect.isEmpty())
            {
                const bool st = mBoard.slotStereo(mSlot);
                auto tr = mStereoTagRect.toFloat();
                g.setColour(st ? mAp.led.withAlpha(0.20f) : juce::Colours::black.withAlpha(0.18f));
                g.fillRoundedRectangle(tr, 6.0f);
                g.setColour((st ? mAp.led : colors::caption).withAlpha(mStereoHover ? 0.95f : 0.7f));
                g.drawRoundedRectangle(tr.reduced(0.5f), 6.0f, 1.0f);
                g.setFont(fonts::archivo(7.5f, fonts::Bold, 0.10f));
                g.drawText(st ? "STEREO" : "MONO", mStereoTagRect, juce::Justification::centred);
            }
            auto jewel = juce::Rectangle<float>(13.0f, 13.0f).withCentre(
                {(float)mHeaderRect.getRight() - 6.5f, (float)mHeaderRect.getCentreY()});
            if (on) fx::glowEllipse(g, jewel, mAp.led, 13, 0.6f, 6, 0.55f);
            g.setColour(on ? mAp.led : juce::Colour(0xff2a2f37));
            g.fillEllipse(jewel);
            if (!mGrRect.isEmpty()) // live gain-reduction bar — lives in Comp's aux band,
                                    // not the header (see resized())
            {
                auto r = mGrRect;
                auto labelR = r.removeFromLeft(22);
                g.setColour(colors::caption);
                g.setFont(fonts::archivo(8.0f, fonts::SemiBold, 0.08f));
                g.drawText("GR", labelR, juce::Justification::centredLeft);
                auto bar = r.withSizeKeepingCentre(r.getWidth() - 4, 6).toFloat();
                g.setColour(colors::track);
                g.fillRoundedRectangle(bar, 3.0f);
                const float grNorm = juce::jlimit(0.0f, 1.0f, mGrSm / 18.0f); // 18 dB span
                if (grNorm > 0.01f)
                {
                    g.setColour(on ? mAp.led : colors::textDim);
                    g.fillRoundedRectangle(bar.withWidth(juce::jmax(3.0f, bar.getWidth() * grNorm)), 3.0f);
                }
            }

            // model name PRINTED on the enclosure — a GHOST BUTTON: pure silkscreen
            // at rest, but on hover it grows button chrome (fill + tint border), the
            // ▾ brightens and the cursor points, so switching the voice/model is
            // obviously clickable (it's the only way to change Env's FX25/Q-Tron
            // voice or Comp's Clean/OTA/Opto/FET mode).
            if (mPillHover)
            {
                auto pr = mPillRect.toFloat();
                g.setColour(juce::Colours::white.withAlpha(0.06f));
                g.fillRoundedRectangle(pr, 11.0f);
                g.setColour(mAp.tint.withAlpha(0.5f));
                g.drawRoundedRectangle(pr.reduced(0.5f), 11.0f, 1.0f);
            }
            g.setColour(on ? colors::textBright : colors::textDim);
            g.setFont(fonts::archivo(19.0f, fonts::Bold));
            g.drawText(mModelStr, mPillRect, juce::Justification::centred);
            g.setColour(mPillHover ? colors::text : colors::caption);
            g.setFont(fonts::mono(mPillHover ? 9.0f : 8.0f));
            g.drawText(juce::String::fromUTF8("\xE2\x96\xBE"),
                       juce::Rectangle<int>(mPillRect.getCentreX() + mNameW / 2 + 4,
                                            mPillRect.getY() + 1, 12, mPillRect.getHeight()),
                       juce::Justification::centredLeft);

            // silkscreen art. MOD/DELAY faces get the DRIVE treatment — the model's
            // full-size solid silkscreen on the free face below the knobs (same box
            // + colour recipe as DrivePedal::paint), so their art takes the same
            // space as the drive pedals'. Faces with no free face (the two 2-knob-row
            // delays) fall back to the zone-spanning watermark, as do Env/Comp
            // (Robbie's locked 5-knob-comp reference look).
            if ((mKind == Mod || mKind == Delay) && mArtRect.getHeight() >= 40)
                paintGlyph(g, mArtRect.toFloat().reduced(mArtRect.getWidth() * 0.16f, 0.0f),
                           on ? mAp.led.withAlpha(0.95f) : juce::Colour(0xff5a616b));
            else if (!mZoneRect.isEmpty())
            {
                const float rx = mKind == Comp ? 0.26f : 0.13f; // wide box: fixed-aspect art reads better
                paintGlyph(g, mZoneRect.toFloat().reduced(mZoneRect.getWidth() * rx, 6.0f),
                           (on ? mAp.led : juce::Colour(0xff5a616b)).withAlpha(0.10f));
            }

            // stomp-plate rule: a hairline seam setting the footswitch zone apart
            if (mPlateY > 0)
            {
                g.setColour(juce::Colours::black.withAlpha(0.35f));
                g.fillRect(14, mPlateY, getWidth() - 28, 1);
                g.setColour(juce::Colours::white.withAlpha(0.04f));
                g.fillRect(14, mPlateY + 1, getWidth() - 28, 1);
            }
        }

        void resized() override
        {
            // FIXED vertical rhythm, shared with DrivePedal: header / constant-height
            // zone (knobs + aux pills, centred) / model pill / aux band / footswitch.
            // The pill, GR bar and footswitch sit at the SAME y on every pedal, and the
            // art zone is one constant rect so the watermark scale never changes
            // (Robbie: the 5-knob comp face is the reference — lock everything to it).
            auto a = getLocalBounds().reduced(10, 8);
            mHeaderRect = a.removeFromTop(15);
            // MONO/STEREO tag: a small clickable chip, left of the jewel LED — same
            // spot in the header for every stereo-capable kind (Mod/Delay/Env/Comp).
            const bool canStereo = (mKind == Mod || mKind == Delay || mKind == Env || mKind == Comp);
            mStereoTagRect = canStereo
                ? juce::Rectangle<int>(mHeaderRect.getRight() - 16 - 54, mHeaderRect.getY() - 1,
                                        54, mHeaderRect.getHeight() + 2)
                : juce::Rectangle<int>();
            a.removeFromTop(6);
            mZoneRect = a.removeFromTop(kZoneH);
            a.removeFromTop(6);
            auto pillRow = a.removeFromTop(28);
            const int pw = juce::jlimit(80, pillRow.getWidth(),
                (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                    fonts::archivo(19.0f, fonts::Bold), mModelStr.isEmpty() ? "-" : mModelStr)) + 34);
            mPillRect = pillRow.withSizeKeepingCentre(pw, 28);
            a.removeFromTop(4);
            auto auxBand = a.removeFromTop(26); // post-pill aux row (2-knob-row faces)
            // Comp has no aux pills to fill this band (see rebuild()) — its live GR
            // meter lives here instead, freeing the header for the STEREO chip above.
            mGrRect = (mKind == Comp && mBoard.compGrDbProvider) ? auxBand : juce::Rectangle<int>();
            a.removeFromTop(8);                 // breathing room over the stomp plate
            mPlateY = a.getY();                 // the seam line
            // The WHOLE zone under the seam is the footswitch (stomp anywhere below
            // the line) — and nothing above it, so the switch can't steal clicks
            // from the aux pills like an oversized centred button would.
            mOn.setBounds(a.withTrimmedTop(3));

            // --- fill the zone TOP-DOWN like a real pedal: knob rows first. A single
            //     aux row (sync / voice / LFO) sits in the post-pill band; a multi-row
            //     toggle bank (Q-Tron) groups as ONE block right under the knobs.
            //     Whatever zone is left below belongs to the full-size silkscreen. ---
            const bool auxAllInZone = (int)mAuxRows.size() >= 2;
            auto zone = mZoneRect;
            for (auto &ks : mRows)
            {
                auto row = zone.removeFromTop(84);
                const int n = juce::jmax(1, (int)ks.size());
                const int kw = juce::jmin(70, row.getWidth() / n);
                auto grp = row.withSizeKeepingCentre(kw * n, row.getHeight());
                for (auto *k : ks) k->setBounds(grp.removeFromLeft(kw).reduced(2, 0));
                zone.removeFromTop(4);
            }
            // Whatever zone is left below the knobs = the full-size silkscreen face
            // for Mod/Delay (mirrors DrivePedal's mArtRect). Empty on 2-row faces.
            mArtRect = (mKind == Mod || mKind == Delay) ? zone : juce::Rectangle<int>();
            auto layAux = [](juce::Rectangle<int> ar, std::vector<juce::Component *> &row)
            {
                const int n = (int)row.size();
                const int total = n * kPillW + (n - 1) * 6;
                auto grp = ar.withSizeKeepingCentre(juce::jmin(total, ar.getWidth()), 20);
                for (int ci = 0; ci < n; ++ci)
                {
                    row[(size_t)ci]->setBounds(grp.removeFromLeft(juce::jmin(kPillW, grp.getWidth())));
                    if (ci + 1 < n) grp.removeFromLeft(6);
                }
            };
            if (auxAllInZone)
                for (auto &row : mAuxRows) layAux(zone.removeFromTop(26), row);
            else if (!mAuxRows.empty())
                layAux(auxBand, mAuxRows[0]);
        }

        void mouseUp(const juce::MouseEvent &e) override
        {
            if (e.getDistanceFromDragStart() >= 5) return; // a deck drag, not a click
            // click the MONO/STEREO chip toggles the span
            if (!mStereoTagRect.isEmpty() && mStereoTagRect.contains(e.getPosition()))
                { mBoard.togglePedalStereo(mSlot); return; }
            // click the printed name opens the model/voice menu
            if (mPillRect.contains(e.getPosition()))
                showModelMenu();
        }

        void mouseMove(const juce::MouseEvent &e) override
        {
            const bool over = mPillRect.contains(e.getPosition());
            const bool overTag = !mStereoTagRect.isEmpty() && mStereoTagRect.contains(e.getPosition());
            if (overTag != mStereoHover)
            {
                mStereoHover = overTag;
                repaint(mStereoTagRect.expanded(4));
            }
            if (over != mPillHover)
            {
                mPillHover = over;
                repaint(mPillRect.expanded(6));
            }
            setMouseCursor((over || overTag) ? juce::MouseCursor::PointingHandCursor
                                             : juce::MouseCursor::NormalCursor);
        }
        void mouseExit(const juce::MouseEvent &) override
        {
            if (mPillHover || mStereoHover)
            {
                mPillHover = mStereoHover = false;
                setMouseCursor(juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

    private:
        static constexpr int kPillW = 96;  // aux StompPill width (two fit a 220px face)
        static constexpr int kZoneH = 172; // the constant knob/art zone (2 knob rows) —
                                           // DrivePedal::resized mirrors this number
        juce::String pid(const char *suf) const { return mBoard.sid(mSlot, suf); }
        juce::String onParamId() const
        {
            if (mKind == Env) return "envfilterOn";
            if (mKind == Comp) return "compOn";
            return pid("On");
        }
        int modelKey() const
        {
            if (mKind == Env) return mBoard.paramC("envfilterVoice");
            if (mKind == Comp) return mBoard.paramC("compMode");
            if (mKind == Mod) return mBoard.paramC(pid("mType"));
            return mBoard.paramC(pid("pModel"));
        }
        void paintGlyph(juce::Graphics &g, juce::Rectangle<float> box, juce::Colour c)
        {
            if (mKind == Env) paintEnvGlyph(g, box, c);
            else if (mKind == Comp) paintCompGlyph(g, box, c);
            else if (mKind == Mod) paintModGlyph(g, mLastKey, box, c);   // per-model silkscreen
            else paintDelayGlyph(g, mLastKey, box, c);
        }

        LabeledKnob *addKnob(int row, const juce::String &paramId, const juce::String &cap)
        {
            auto k = std::make_unique<LabeledKnob>(mBoard.mApvts, paramId, cap);
            k->setCaptionHeight(13);
            k->setValueOnDragOnly(); // real pedals don't display numbers at rest
            auto *raw = k.get();
            addAndMakeVisible(*raw);
            while ((int)mRows.size() <= row) mRows.emplace_back();
            mRows[(size_t)row].push_back(raw);
            mOwned.push_back(std::move(k));
            return raw;
        }
        StompPill *addPill(int auxRow, const juce::String &paramId, const juce::String &caption,
                           juce::StringArray items)
        {
            auto p = std::make_unique<StompPill>(mBoard.mApvts, paramId, caption, std::move(items));
            auto *raw = p.get();
            addAndMakeVisible(*raw);
            while ((int)mAuxRows.size() <= auxRow) mAuxRows.emplace_back();
            mAuxRows[(size_t)auxRow].push_back(raw);
            mPills.push_back(raw);
            mOwned.push_back(std::move(p));
            return raw;
        }
        StompPill *addSync(int auxRow, const juce::String &paramId)
        {
            return addPill(auxRow, paramId, "SYNC",
                           {"Off", "1/1", "1/2", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16"});
        }

        void rebuild()
        {
            mRows.clear();
            mAuxRows.clear();
            mPills.clear();
            mOwned.clear();

            const int key = modelKey();
            mLastKey = key;

            switch (mKind)
            {
            case Env:
            {
                mKindStr = "ENV FILTER";
                mModelStr = key == 1 ? "Q-Tron" : "FX25";
                mAp = envAccent();
                if (key == 1) // Q-Tron+: Gain/Peak + the real toggle bank (as mini pills)
                {
                    addKnob(0, "envfilterSens", "Gain");
                    addKnob(0, "envfilterReso", "Peak");
                    addPill(0, "envfilterMode", "MODE", {"LP", "BP", "HP", "MIX"});
                    addPill(0, "envfilterDir", "DRIVE", {"Up", "Down"});
                    addPill(1, "envfilterQRange", "RANGE", {"Lo", "Hi"});
                    addPill(1, "envfilterBoost", "BOOST", {"Norm", "Boost"});
                    addPill(2, "envfilterResponse", "RESP", {"Fast", "Slow"});
                }
                else // FX25B: Sensitivity/Range/Blend
                {
                    addKnob(0, "envfilterSens", "Sens");
                    addKnob(0, "envfilterRange", "Range");
                    addKnob(0, "envfilterMix", "Blend");
                }
                break;
            }
            case Comp:
            {
                using CB = nam_rig::CompBlock;
                const auto mode = (CB::Mode)juce::jlimit(0, 3, key);
                mKindStr = "COMPRESSOR";
                mModelStr = compModeName(key);
                mAp = compAccent();
                int row = 0, inRow = 0;
                auto place = [&](const char *id, const char *cap) -> LabeledKnob * {
                    auto *k = addKnob(row, id, cap);
                    if (++inRow == 3) { ++row; inRow = 0; }
                    return k;
                };
                place("compSustain", "Sustain");
                LabeledKnob *att = CB::attackExposed(mode) ? place("compAttack", "Attack") : nullptr;
                LabeledKnob *rat = CB::ratioExposed(mode) ? place("compRatio", "Ratio") : nullptr;
                LabeledKnob *rel = CB::releaseExposed(mode) ? place("compRelease", "Release") : nullptr;
                place("compLevel", "Level");
                if (CB::dryBlendExposed(mode)) place("compDry", "Dry");
                // true effective readouts per voicing (single source of truth = CompBlock)
                if (att != nullptr)
                    att->setReadoutFn([mode](double v) {
                        const float ms = CB::effectiveAttackMs(mode, (float)v);
                        return ms < 1.0f ? juce::String(ms * 1000.0f, 0) + juce::String::fromUTF8(" \xC2\xB5s")
                                         : juce::String(ms, ms < 10.0f ? 2 : 1) + " ms";
                    });
                if (rel != nullptr)
                    rel->setReadoutFn([mode](double v) {
                        const float ms = CB::effectiveReleaseMs(mode, (float)v);
                        return ms >= 1000.0f ? juce::String(ms / 1000.0f, 2) + " s"
                                             : juce::String(ms, 0) + " ms";
                    });
                if (rat != nullptr)
                    rat->setReadoutFn([mode](double v) {
                        const float r = (mode == CB::Mode::FET) ? CB::snapRatioFet((float)v) : (float)v;
                        return juce::String(r, (r == (float)(int)r) ? 0 : 1) + ":1";
                    });
                break;
            }
            case Mod:
            {
                const int t = juce::jlimit(0, 4, key);
                mKindStr = "MOD";
                mModelStr = modName(t);
                mAp = modAccent(t);
                addKnob(0, pid("mRate"), "Rate");
                if (t != 1) addKnob(0, pid("mDepth"), "Depth");
                if (t == 2) addKnob(0, pid("mFeedback"), "Regen");
                if (t == 3) addKnob(0, pid("mWave"), "Wave");
                if (t == 4) addKnob(0, pid("mMix"), "Mix");
                addSync(0, pid("mSync"));
                if (t == 1) addPill(0, pid("mPhaserVoice"), "VOICE", {"Script", "Block"});
                break;
            }
            case Delay:
            {
                const int m = juce::jlimit(0, 2, key);
                mKindStr = "DELAY";
                mModelStr = delayName(m);
                mAp = delayAccent(m);
                if (m == 0) // Boss DD-7: D.TIME / F.BACK / E.LEVEL + the MODE rotary
                {
                    addKnob(0, pid("pTime"), "D.Time");
                    addKnob(0, pid("pFeedback"), "F.Back");
                    addKnob(0, pid("pMix"), "E.Level");
                    auto *mode = addKnob(1, pid("pMode"), "Mode");
                    mode->setValueOnDragOnly(false); // the mode NAME is the display — keep it
                    mode->setReadoutFn([](double v) {
                        static const char *const n[8] = {"50 ms", "200 ms", "800 ms", "3200 ms",
                                                         "Hold", "Modulate", "Analog", "Reverse"};
                        return juce::String(n[juce::jlimit(0, 7, (int)std::lround(v))]);
                    });
                    mode->setValueMenu({"50 ms", "200 ms", "800 ms", "3200 ms",
                                        "Hold", "Modulate", "Analog", "Reverse"}); // no header: the knob is labelled MODE
                }
                else if (m == 1) // MXR Carbon Copy: Delay / Regen / Mix
                {
                    addKnob(0, pid("pTime"), "Delay");
                    addKnob(0, pid("pFeedback"), "Regen");
                    addKnob(0, pid("pMix"), "Mix");
                }
                else // EHX Deluxe Memory Man: Delay / Feedback / Blend / Depth / Level
                {
                    addKnob(0, pid("pTime"), "Delay");
                    addKnob(0, pid("pFeedback"), "Feedback");
                    addKnob(0, pid("pMix"), "Blend");
                    addKnob(1, pid("pMod"), "Depth");
                    addKnob(1, pid("pLevel"), "Level");
                }
                addSync(0, pid("pSync"));
                if (m == 2) addPill(0, pid("pChorusVib"), "LFO", {"Chorus", "Vibrato"});
                break;
            }
            }

            mNameW = (int)std::ceil(juce::GlyphArrangement::getStringWidth(
                fonts::archivo(19.0f, fonts::Bold), mModelStr)); // for the ▾ position
            mLastOn = mBoard.paramF(onParamId()) >= 0.5f;
            applyAccents(mLastOn);
            mOn.setLit(mLastOn);
            resized();
            repaint();
        }

        void applyAccents(bool on)
        {
            const juce::Colour knobAcc = on ? mAp.led : juce::Colour(0xff5a616b);
            for (auto &row : mRows)
                for (auto *k : row) k->setAccent(knobAcc);
            for (auto *p : mPills) p->setAccent(on ? mAp.led : juce::Colour(0xff5a616b));
            mOn.setAccent(mAp.led);
        }

        void showModelMenu()
        {
            juce::PopupMenu m;
            m.setLookAndFeel(&getLookAndFeel());
            juce::String paramId;
            juce::StringArray items;
            juce::String header;
            switch (mKind)
            {
            case Env:   paramId = "envfilterVoice"; header = "CHOOSE VOICE";
                        items = {"FX25", "Q-Tron"}; break;
            case Comp:  paramId = "compMode"; header = "CHOOSE COMP";
                        items = {"Clean", "OTA", "Opto", "FET"}; break;
            case Mod:   paramId = pid("mType"); header = "CHOOSE MOD";
                        for (int i = 0; i < 5; ++i) items.add(modName(i)); break;
            case Delay: paramId = pid("pModel"); header = "CHOOSE DELAY";
                        for (int i = 0; i < 3; ++i) items.add(delayName(i)); break;
            }
            m.addCustomItem(-1, std::make_unique<MenuSectionHeader>(header), nullptr, {});
            const int cur = mBoard.paramC(paramId);
            for (int i = 0; i < items.size(); ++i)
                m.addItem(i + 1, items[i], true, i == cur);
            juce::Component::SafePointer<StompPedal> sp(this);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(mPillRect)),
                            [sp, paramId](int r)
                            {
                                if (sp != nullptr && r > 0)
                                    sp->mBoard.writeChoice(paramId, r - 1);
                                // the control set follows on the next refresh tick
                            });
        }

        PedalboardPanel &mBoard;
        Kind mKind;
        int mSlot;
        Footswitch mOn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
        std::vector<std::unique_ptr<juce::Component>> mOwned;
        std::vector<std::vector<LabeledKnob *>> mRows;
        std::vector<std::vector<juce::Component *>> mAuxRows;
        std::vector<StompPill *> mPills;
        colors::AccentPair mAp;
        juce::String mKindStr, mModelStr;
        juce::Rectangle<int> mHeaderRect, mPillRect, mZoneRect, mStereoTagRect, mGrRect;
        juce::Rectangle<int> mArtRect; // Mod/Delay: zone slack below the knobs (solid silkscreen)
        int mPlateY = 0;  // stomp-plate seam y (0 until first layout)
        int mNameW = 0;   // printed-name text width (positions the ▾)
        int mLastKey = -1;
        bool mLastOn = true, mPillHover = false, mStereoHover = false;
        float mGrSm = 0.0f, mGrPainted = -1.0f; // comp GR meter smoothing / dirty check
    };

    // ====================================================================== Deck
    // Every placed pedal, side by side in processing order: trunk -> Amp A lane ->
    // Amp B lane. Cells are PURE pedal faces (no header band): grab a pedal anywhere
    // on its enclosure and drag to reorder / regroup (ghost + caret, edge auto-scroll;
    // drop on the BIN that appears to remove). Env/Comp are full citizens here too —
    // just two more pedal kinds in the same cell list. Faces render through a
    // uniform scale-down transform, so the tuned 220px layout just draws smaller.
    // Patch cables join same-lane neighbours; the split dot marks trunk -> lanes.
    // Also the palette's drop target.
    class Deck : public juce::Component, public juce::DragAndDropTarget
    {
    public:
        explicit Deck(PedalboardPanel &b) : mBoard(b) {}

        struct Cell
        {
            int sel;  // pool slot index — every pedal, including Env/Comp, is a real slot
            int lane; // 0/1/2
            juce::Component *widget = nullptr;
            juce::Rectangle<int> bounds; // deck-space (post-scale) cell rect
        };

        static int cellW() { return (int)std::lround(kCellW * kPedalScale); }
        static int cellH() { return (int)std::lround(kPedalDesignH * kPedalScale); }

        int preferredWidth() const
        {
            const int n = mBoard.usedCount();
            const bool ghost = n == 0;
            return kCellGap + n * (cellW() + kCellGap) + (ghost ? 150 + kCellGap : 0);
        }

        void rebuild()
        {
            mCells.clear();
            mWidgets.clear();

            const Lanes ln = mBoard.lanesNow();
            for (int lane = 0; lane < 3; ++lane)
                for (int s : ln.l[lane])
                {
                    Cell c;
                    c.sel = s;
                    c.lane = lane;
                    if (mBoard.slotType(s) == 1) // the REAL drive pedal widget
                    {
                        auto w = std::make_unique<DrivePedal>(mBoard.mApvts,
                                                              "pbS" + juce::String(s),
                                                              mBoard.sid(s, "dCat"));
                        c.widget = w.get();
                        addAndMakeVisible(*c.widget);
                        mWidgets.push_back(std::move(w));
                    }
                    else
                    {
                        const int t = mBoard.slotType(s);
                        const auto kind = t == 2 ? StompPedal::Mod : t == 3 ? StompPedal::Delay
                                        : t == 4 ? StompPedal::Env : StompPedal::Comp;
                        auto w = std::make_unique<StompPedal>(mBoard, kind, s);
                        c.widget = w.get();
                        addAndMakeVisible(*c.widget);
                        mWidgets.push_back(std::move(w));
                    }
                    mCells.push_back(c);
                }
            // Grab-anywhere dragging: the deck listens to each pedal face's OWN mouse
            // events (children — knobs, pills, footswitch — keep theirs), so a click-
            // hold on the enclosure drags the pedal.
            for (auto &w : mWidgets)
                w->addMouseListener(this, false);
            layoutCells();
            repaint();
        }

        void refreshPedals()
        {
            for (auto &w : mWidgets)
            {
                if (auto *d = dynamic_cast<DrivePedal *>(w.get())) d->refresh();
                else if (auto *s = dynamic_cast<StompPedal *>(w.get())) s->refresh();
            }
        }

        void resized() override { layoutCells(); }

        void paint(juce::Graphics &g) override
        {
            // patch cables between same-lane neighbours + the split after the trunk
            for (size_t i = 0; i + 1 < mCells.size(); ++i)
            {
                const auto &a = mCells[i];
                const auto &b = mCells[i + 1];
                const int cableY = a.bounds.getCentreY();
                const int x0 = a.bounds.getRight(), x1 = b.bounds.getX();
                if (a.lane == b.lane)
                {
                    g.setColour(laneCol(b.lane).withAlpha(0.75f));
                    g.fillRect(x0 + 4, cableY - 1, x1 - x0 - 8, 2);
                }
                else if (a.lane == 0) // trunk ends -> split dot in the gap
                {
                    const float cx = (float)(x0 + x1) * 0.5f;
                    g.setColour(colors::titleAccent.withAlpha(0.8f));
                    g.fillRect(x0 + 4, cableY - 1, (int)cx - x0 - 8, 2);
                    g.setColour(colors::accent);
                    g.fillEllipse(cx - 3.5f, (float)cableY - 3.5f, 7.0f, 7.0f);
                }
                else // Amp A group -> Amp B group: different buses, draw a divider
                {
                    g.setColour(colors::divider);
                    g.fillRect((x0 + x1) / 2, a.bounds.getY() + 6, 1, a.bounds.getHeight() - 12);
                }
            }

            if (!mGhost.isEmpty()) // empty board hint
            {
                juce::Path outline;
                outline.addRoundedRectangle(mGhost.toFloat().reduced(1.0f), 14.0f);
                juce::Path dashed;
                const float d[2] = {5.0f, 4.0f};
                juce::PathStrokeType(1.2f).createDashedStroke(dashed, outline, d, 2);
                g.setColour(colors::outline);
                g.fillPath(dashed);
                g.setColour(colors::textDim);
                g.setFont(fonts::archivo(11.0f, fonts::SemiBold, 0.04f));
                g.drawText("drag a pedal here", mGhost, juce::Justification::centred);
            }
        }

        // Drag + selection visuals sit ABOVE the pedal widgets.
        void paintOverChildren(juce::Graphics &g) override
        {
            if (!mDragging && mBoard.selectedPedal() != kSelNone) // selection ring
                for (const auto &c : mCells)
                    if (c.sel == mBoard.selectedPedal())
                    {
                        g.setColour(colors::accent.withAlpha(0.55f));
                        g.drawRoundedRectangle(c.bounds.toFloat().expanded(2.0f), 15.0f, 1.4f);
                    }
            const int caret = mDragging && mPressSel >= 0 ? mDragCaretX : mCaretX;
            if (caret >= 0) // insertion caret (cell move or palette drop)
            {
                g.setColour(colors::accent);
                g.fillRoundedRectangle((float)caret - 1.5f, 4.0f, 3.0f,
                                       (float)getHeight() - 8.0f, 1.5f);
            }
            if (!mDragging) return;
            for (const auto &c : mCells) // dim the cell being moved (fade into the dark well)
                if (c.sel == mPressSel)
                {
                    g.setColour(juce::Colour(0xff14171d).withAlpha(0.6f));
                    g.fillRoundedRectangle(c.bounds.toFloat(), 10.0f);
                }
            if (mDragImg.isValid()) // floating half-size ghost under the cursor
            {
                auto dst = juce::Rectangle<float>((float)mDragImg.getWidth() * 0.5f,
                                                  (float)mDragImg.getHeight() * 0.5f)
                               .withCentre(mDragPos.toFloat());
                g.setOpacity(0.88f);
                g.drawImage(mDragImg, dst);
                g.setOpacity(1.0f);
            }
        }

        // NB: these also receive the pedal widgets' own mouse events (the deck is a
        // MouseListener on every face), so everything works in DECK space via
        // getEventRelativeTo — grab a pedal anywhere on its enclosure to drag it.
        void mouseDown(const juce::MouseEvent &e) override
        {
            mPressSel = kSelNone;
            if (e.mods.isPopupMenu()) return; // right-click never arms a drag
            const auto p = e.getEventRelativeTo(this).getPosition();
            for (const auto &c : mCells)
                if (c.bounds.contains(p)) { mPressSel = c.sel; return; }
        }

        void mouseDrag(const juce::MouseEvent &e) override
        {
            if (mPressSel == kSelNone) return;
            if (!mDragging && e.getDistanceFromDragStart() < 5) return;
            const auto p = e.getEventRelativeTo(this).getPosition();
            if (!mDragging)
            {
                mDragging = true;
                mDragImg = juce::Image();
                for (const auto &c : mCells) // one-shot ghost snapshot of the pedal face
                    if (c.sel == mPressSel && c.widget != nullptr)
                    {
                        mDragImg = c.widget->createComponentSnapshot(c.widget->getLocalBounds());
                        break;
                    }
                juce::Component::beginDragAutoRepeat(60); // keeps drags ticking for edge-scroll
                mBoard.deckDragBegan(mPressSel >= 0);     // the BIN appears for free pedals
            }
            mDragPos = p;
            mDragCaretX = mPressSel >= 0 ? caretXFor(mDragPos, mPressSel) : -1;
            mBoard.deckDragMoved(mBoard.getLocalPoint(this, p));
            autoScroll(p);
            repaint();
        }

        void mouseUp(const juce::MouseEvent &e) override
        {
            const auto p = e.getEventRelativeTo(this).getPosition();
            if (mDragging) // finish a cell move / bin drop
            {
                juce::Component::beginDragAutoRepeat(0);
                const int dragged = mPressSel;
                mDragging = false;
                mPressSel = kSelNone;
                mDragCaretX = -1;
                mDragImg = juce::Image();
                const bool binHit = mBoard.deckDragFinished(mBoard.getLocalPoint(this, p));
                if (dragged >= 0 && binHit)
                {
                    mBoard.removePedal(dragged); // dropped on the bin
                }
                else
                {
                    const auto tgt = moveTarget(p, dragged);
                    mBoard.movePedal(dragged, tgt.first, tgt.second);
                }
                repaint();
                return;
            }
            mPressSel = kSelNone;
            for (const auto &c : mCells) // plain click on a pedal = select it
                if (c.bounds.contains(p))
                {
                    mBoard.selectPedal(c.sel, false);
                    return;
                }
        }

        // ---- palette drop target ----
        bool isInterestedInDragSource(const SourceDetails &d) override
        {
            if (!d.description.toString().startsWith("pbadd:") || mBoard.boardFull()) return false;
            int f = 0, c = 0, m = 0;
            if (!parseAdd(d.description.toString(), f, c, m)) return false;
            return !((f == 4 || f == 5) && mBoard.typePlaced(f)); // Env/Comp singleton
        }
        void itemDragMove(const SourceDetails &d) override { updateCaret(d.localPosition); }
        void itemDragExit(const SourceDetails &) override { mCaretX = -1; repaint(); }
        void itemDropped(const SourceDetails &d) override
        {
            const auto pos = moveTarget(d.localPosition, kSelNone);
            mCaretX = -1;
            repaint();
            int f = 0, c = 0, m = 0;
            if (!parseAdd(d.description.toString(), f, c, m)) return;
            const int slot = mBoard.addPedal(f, c, m, pos.first, pos.second);
            if (slot >= 0) mBoard.selectPedal(slot, true);
        }

        const std::vector<Cell> &cells() const { return mCells; }

    private:
        static juce::Colour laneCol(int lane)
        {
            return lane == 0 ? colors::titleAccent : colors::laneColour(lane - 1);
        }

        void layoutCells()
        {
            const int cw = cellW(), ch = cellH();
            const int yTop = juce::jmax(0, (getHeight() - ch) / 2);
            int x = kCellGap;
            for (auto &c : mCells)
            {
                c.bounds = {x, yTop, cw, ch};
                if (c.widget != nullptr)
                {
                    // faces are laid out at DESIGN size and drawn through a uniform
                    // scale-down — the tuned 220px skeleton, just smaller
                    c.widget->setTransform(juce::AffineTransform::scale(kPedalScale)
                                               .translated((float)x, (float)yTop));
                    c.widget->setBounds(0, 0, kCellW, kPedalDesignH);
                }
                x += cw + kCellGap;
            }
            // "drag a pedal here" hint fills the whole deck once it's truly empty
            // (a fresh board has no cells at all now that Env/Comp aren't pre-placed).
            mGhost = mBoard.usedCount() == 0
                         ? juce::Rectangle<int>(x, yTop, 150, ch)
                         : juce::Rectangle<int>();
        }

        static bool parseAdd(const juce::String &desc, int &f, int &c, int &m)
        {
            auto t = juce::StringArray::fromTokens(desc, ":", {});
            if (t.size() != 4 || t[0] != "pbadd") return false;
            f = t[1].getIntValue();
            c = t[2].getIntValue();
            m = t[3].getIntValue();
            return f >= 1 && f <= 5;
        }

        // Map a point to (lane, position within that lane), ignoring `excludeSel`
        // (the cell being moved; kSelNone for palette adds).
        std::pair<int, int> moveTarget(juce::Point<int> p, int excludeSel) const
        {
            // Default lane = the first (non-excluded) cell's OWN lane. Cells render
            // Trunk-then-A-then-B, so this is Trunk in the common case -- but when
            // Trunk is empty and the deck starts with a Lane-A/B pedal (e.g. a stereo
            // bridge with nothing before it), dropping BEFORE everything must still
            // target THAT pedal's lane, not silently fall back to Trunk just because
            // the scan below never passes a cell to update it from.
            int lane = 0;
            for (const auto &c : mCells)
                if (c.sel != excludeSel) { lane = c.lane; break; }
            int pos = 0, cnt[3] = {0, 0, 0};
            for (const auto &c : mCells)
            {
                if (c.sel == excludeSel) continue;
                if (p.x > c.bounds.getCentreX()) { lane = c.lane; pos = cnt[c.lane] + 1; }
                ++cnt[c.lane];
            }
            return {lane, pos};
        }
        // Insertion caret x for a point, ignoring `excludeSel`.
        int caretXFor(juce::Point<int> p, int excludeSel) const
        {
            int x = kCellGap / 2;
            for (const auto &c : mCells)
                if (c.sel != excludeSel && p.x > c.bounds.getCentreX())
                    x = c.bounds.getRight() + kCellGap / 2;
            return x;
        }
        void updateCaret(juce::Point<int> p)
        {
            const int x = caretXFor(p, kSelNone);
            if (x != mCaretX) { mCaretX = x; repaint(); }
        }
        // Nudge the deck viewport while dragging near its edges (beginDragAutoRepeat
        // keeps mouseDrag ticking so the scroll continues while the mouse is held).
        void autoScroll(juce::Point<int> p)
        {
            auto *vp = findParentComponentOfClass<juce::Viewport>();
            if (vp == nullptr) return;
            const auto vpt = vp->getLocalPoint(this, p);
            const int edge = 48;
            int dx = 0;
            if (vpt.x < edge) dx = -14;
            else if (vpt.x > vp->getWidth() - edge) dx = 14;
            if (dx != 0)
                vp->setViewPosition(juce::jmax(0, vp->getViewPositionX() + dx),
                                    vp->getViewPositionY());
        }

        PedalboardPanel &mBoard;
        std::vector<Cell> mCells;
        std::vector<std::unique_ptr<juce::Component>> mWidgets;
        juce::Rectangle<int> mGhost;
        int mCaretX = -1;      // palette-drop caret
        int mPressSel = kSelNone; // band pressed (potential cell drag)
        bool mDragging = false;
        juce::Point<int> mDragPos;
        juce::Image mDragImg;  // ghost snapshot of the dragged pedal face
        int mDragCaretX = -1;  // insertion caret while moving a cell
    };

    // ================================================================= ChainStrip
    // The signal-flow footer: IN -> trunk nodes -> split -> lane A (top) / lane B
    // (bottom) -> AMP A/B. Nodes are mini enclosures, one per placed pedal (Env/Comp
    // included). Drag a node to reorder / change lane; right-click for Route / Remove.
    // Also a palette drop target.
    class ChainStrip : public juce::Component, public juce::DragAndDropTarget
    {
    public:
        explicit ChainStrip(PedalboardPanel &b) : mBoard(b) {}

        struct Node
        {
            int sel;  // pool slot index — every pedal, including Env/Comp, is a real slot
            int lane; // 0/1/2
            juce::Rectangle<int> rect;
        };

        void refreshLayout()
        {
            layoutNodes();
            repaint();
        }

        void resized() override { layoutNodes(); }

        void paint(juce::Graphics &g) override
        {
            const int cy = trunkY();
            // trunk cable + split
            g.setColour(colors::titleAccent.withAlpha(0.9f));
            g.drawLine((float)mIn.getRight(), (float)cy, (float)mSplitX, (float)cy, 2.0f);
            drawLaneCable(g, mAmpA.getCentreY(), colors::laneColour(0));
            drawLaneCable(g, mAmpB.getCentreY(), colors::laneColour(1));
            g.setColour(colors::accent);
            g.fillEllipse((float)mSplitX - 3.0f, (float)cy - 3.0f, 6.0f, 6.0f);

            drawPill(g, mIn, "IN", colors::textDim, colors::outline);
            drawPill(g, mAmpA, "A", colors::laneColour(0), colors::laneColour(0));
            drawPill(g, mAmpB, "B", colors::laneColour(1), colors::laneColour(1));

            for (const auto &n : mNodes)
                if (!(mDragging && n.sel == mDragSel))
                    drawNode(g, n, false);

            if (mDragging)
            {
                if (mCaret.x >= 0) // insertion caret
                {
                    g.setColour(colors::accent);
                    g.fillRoundedRectangle((float)mCaret.x - 1.5f, (float)mCaret.y - 16.0f, 3.0f, 32.0f, 1.5f);
                }
                for (const auto &n : mNodes) // ghost follows the cursor
                    if (n.sel == mDragSel)
                    {
                        auto gh = n;
                        gh.rect = n.rect.withCentre(mDragPos);
                        drawNode(g, gh, true);
                    }
            }
            else if (mDropCaret.x >= 0) // palette drop caret
            {
                g.setColour(colors::accent);
                g.fillRoundedRectangle((float)mDropCaret.x - 1.5f, (float)mDropCaret.y - 16.0f, 3.0f, 32.0f, 1.5f);
            }
        }

        // The LED dot doubles as the bypass switch — a padded hit zone around the
        // 5px dot (top-right of the node), so it's actually clickable.
        static juce::Rectangle<int> ledZone(const juce::Rectangle<int> &nr)
        {
            return {nr.getRight() - 16, nr.getY(), 16, 16};
        }

        void mouseDown(const juce::MouseEvent &e) override
        {
            mDragging = false;
            mDragSel = mLedSel = kSelNone;
            const auto *n = nodeAt(e.getPosition());
            if (n == nullptr) return;
            if (e.mods.isPopupMenu()) { showNodeMenu(*n); return; }
            if (ledZone(n->rect).contains(e.getPosition()))
            {
                mLedSel = n->sel; // bypass click — never arms a drag or selects
                return;
            }
            mDragSel = n->sel;
        }
        void mouseDrag(const juce::MouseEvent &e) override
        {
            if (mDragSel == kSelNone) return;
            if (!mDragging && e.getDistanceFromDragStart() < 5) return;
            mDragging = true;
            mDragPos = e.getPosition();
            if (mDragSel >= 0) mCaret = caretFor(mDragPos).first;
            repaint();
        }
        void mouseMove(const juce::MouseEvent &e) override
        {
            const auto *n = nodeAt(e.getPosition()); // hand over the LED = it's a switch
            setMouseCursor(n != nullptr && ledZone(n->rect).contains(e.getPosition())
                               ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::NormalCursor);
        }
        void mouseUp(const juce::MouseEvent &e) override
        {
            if (mLedSel != kSelNone) // LED click = toggle bypass (release must stay on the dot)
            {
                if (const auto *n = nodeAt(e.getPosition());
                    n != nullptr && n->sel == mLedSel && ledZone(n->rect).contains(e.getPosition()))
                    mBoard.togglePedalOn(mLedSel);
                mLedSel = kSelNone;
                repaint();
                return;
            }
            if (!mDragging)
            {
                if (const auto *n = nodeAt(e.getPosition()); n != nullptr && mDragSel == n->sel)
                    mBoard.selectPedal(n->sel, true); // click = select + scroll the deck there
                mDragSel = kSelNone;
                repaint();
                return;
            }
            mDragging = false;
            const auto p = e.getPosition();
            if (!getLocalBounds().expanded(24).contains(p)) // released way outside = cancel
            {
                mDragSel = kSelNone;
                mCaret = {-1, 0};
                repaint();
                return;
            }
            {
                const auto tgt = caretFor(p).second;
                mBoard.movePedal(mDragSel, tgt.first, tgt.second);
            }
            mDragSel = kSelNone;
            mCaret = {-1, 0};
            repaint();
        }

        // ---- palette drop target ----
        bool isInterestedInDragSource(const SourceDetails &d) override
        {
            const auto desc = d.description.toString();
            if (!desc.startsWith("pbadd:") || mBoard.boardFull()) return false;
            auto t = juce::StringArray::fromTokens(desc, ":", {});
            if (t.size() != 4) return false;
            const int f = t[1].getIntValue();
            return !((f == 4 || f == 5) && mBoard.typePlaced(f)); // Env/Comp singleton
        }
        void itemDragMove(const SourceDetails &d) override
        {
            mDropCaret = caretFor(d.localPosition).first;
            repaint();
        }
        void itemDragExit(const SourceDetails &) override
        {
            mDropCaret = {-1, 0};
            repaint();
        }
        void itemDropped(const SourceDetails &d) override
        {
            const auto tgt = caretFor(d.localPosition).second;
            mDropCaret = {-1, 0};
            auto t = juce::StringArray::fromTokens(d.description.toString(), ":", {});
            if (t.size() == 4 && t[0] == "pbadd")
            {
                const int slot = mBoard.addPedal(t[1].getIntValue(), t[2].getIntValue(),
                                                 t[3].getIntValue(), tgt.first, tgt.second);
                if (slot >= 0) mBoard.selectPedal(slot, true);
            }
            repaint();
        }

    private:
        int trunkY() const { return getHeight() / 2 + 4; }
        int laneAY() const { return getHeight() / 4 + 6; }
        int laneBY() const { return (3 * getHeight()) / 4 + 2; }

        void layoutNodes()
        {
            mNodes.clear();
            if (getWidth() <= 0) return;
            const Lanes ln = mBoard.lanesNow();
            // A STEREO pedal is a SHARED column both lanes flow through, so it counts on
            // BOTH rows. Width the rows for the longer path (mono-on-that-row + every span).
            auto spanCount = [&](const std::vector<int> &v) {
                int s = 0; for (int i : v) if (mBoard.slotStereo(i)) ++s; return s; };
            const int spansA = spanCount(ln.l[1]), spansB = spanCount(ln.l[2]);
            const int allSpans = spansA + spansB;
            const int rowA = (int)ln.l[1].size() - spansA + allSpans;
            const int rowB = (int)ln.l[2].size() - spansB + allSpans;
            const int nTrunk = (int)ln.l[0].size(); // Env/Comp are regular Trunk-lane entries now
            const int nLane = juce::jmax(1, juce::jmax(rowA, rowB));
            const int gap = 8, inW = 26, ampW = 40, splitPad = 24;
            // Fixed node size — the chain's total width grows/shrinks with the pedal
            // count instead of stretching nodes to fill the strip; the whole block is
            // then centred in whatever width the strip has.
            const int nodeW = 60;
            const int nodeH = 34;
            const int contentW = inW + gap + nTrunk * (nodeW + gap) + splitPad
                                + nLane * (nodeW + gap) + ampW;
            const int startX = juce::jmax(0, (getWidth() - contentW) / 2);

            const int cy = trunkY();
            mIn = {startX, cy - 12, inW, 24};
            int x = startX + inW + gap;

            for (int s : ln.l[0])
            {
                mNodes.push_back({s, 0, {x, cy - nodeH / 2, nodeW, nodeH}});
                x += nodeW + gap;
            }
            mSplitX = x + splitPad / 2 - gap / 2;

            // Post-split walk in INDEX order (l[1] then l[2] == index order, since packing
            // keeps all Amp-A indices below all Amp-B). Amp-A pedals advance the xA cursor,
            // Amp-B the xB cursor; a STEREO pedal is a tall SHARED column placed at max(xA,xB)
            // that advances BOTH — so anything after it on EITHER lane is drawn to its right
            // (you can place a pedal after the stereo on the lane that was empty before it).
            const int top = laneAY() - nodeH / 2, bot = laneBY() + nodeH / 2;
            int xa = mSplitX + splitPad / 2, xb = xa;
            auto placePost = [&](int s, int home)
            {
                if (mBoard.slotStereo(s))
                {
                    const int sx = juce::jmax(xa, xb);
                    mNodes.push_back({s, home, {sx, top, nodeW, bot - top}});
                    xa = xb = sx + nodeW + gap;
                }
                else if (home == 1)
                {
                    mNodes.push_back({s, 1, {xa, laneAY() - nodeH / 2, nodeW, nodeH}});
                    xa += nodeW + gap;
                }
                else
                {
                    mNodes.push_back({s, 2, {xb, laneBY() - nodeH / 2, nodeW, nodeH}});
                    xb += nodeW + gap;
                }
            };
            for (int s : ln.l[1]) placePost(s, 1);
            for (int s : ln.l[2]) placePost(s, 2);

            const int ampX = juce::jmax(juce::jmax(xa, xb) + gap, mSplitX + splitPad);
            mAmpA = {ampX, laneAY() - 12, ampW, 24};
            mAmpB = {ampX, laneBY() - 12, ampW, 24};
        }

        void drawLaneCable(juce::Graphics &g, int laneY, juce::Colour c)
        {
            g.setColour(c.withAlpha(0.9f));
            juce::Path p;
            p.startNewSubPath((float)mSplitX, (float)trunkY());
            p.lineTo((float)mSplitX, (float)laneY);
            p.lineTo((float)mAmpA.getX(), (float)laneY);
            g.strokePath(p, juce::PathStrokeType(2.0f));
        }
        static void drawPill(juce::Graphics &g, juce::Rectangle<int> ri, const juce::String &txt,
                             juce::Colour txtCol, juce::Colour border)
        {
            auto r = ri.toFloat();
            g.setColour(colors::tile);
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour(border);
            g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.2f);
            g.setColour(txtCol);
            g.setFont(fonts::archivo(10.0f, fonts::Bold, 0.06f));
            g.drawText(txt, ri, juce::Justification::centred);
        }

        void drawNode(juce::Graphics &g, const Node &n, bool ghost)
        {
            const int t = mBoard.slotType(n.sel);
            const colors::AccentPair ap = mBoard.slotAccent(n.sel);
            const juce::String name = typeShort(t);
            // Env/Comp's bypass authority is their own shared envfilterOn/compOn (the
            // LED still toggles it via togglePedalOn); every other type uses pbS{i}On.
            const bool on = t == 4 ? mBoard.paramF("envfilterOn") >= 0.5f
                          : t == 5 ? mBoard.paramF("compOn") >= 0.5f
                                   : mBoard.slotOn(n.sel);
            const bool sel = !ghost && mBoard.selectedPedal() == n.sel && n.sel != kSelNone;
            auto r = n.rect.toFloat();
            if (ghost) g.setOpacity(0.85f);
            paintEnclosure(g, r, ap.tint, on, sel, 7.0f);
            g.setColour(on ? ap.accent : colors::textDim);
            g.setFont(fonts::archivo(7.5f, fonts::Bold, 0.08f));
            g.drawText(name, n.rect.withHeight(11).translated(0, 2), juce::Justification::centred);
            auto gbox = r.withTrimmedTop(13.0f).withTrimmedBottom(4.0f).reduced(r.getWidth() * 0.22f, 0.0f);
            mBoard.paintPedalGlyph(g, n.sel, gbox, (on ? ap.led : juce::Colour(0xff5a616b)).withAlpha(0.9f));
            // LED = the node's bypass switch (click toggles; hit zone in ledZone()).
            auto led = juce::Rectangle<float>(5.0f, 5.0f).withPosition(r.getRight() - 9.0f, r.getY() + 4.0f);
            g.setColour(on ? ap.led : colors::ledOff);
            g.fillEllipse(led);
            // STEREO span: an "ST" tag in the BOTTOM-RIGHT corner (clear of the
            // glyph and the LED) so the tall node reads as feeding both amps.
            if (n.sel >= 0 && mBoard.slotStereo(n.sel))
            {
                g.setColour(on ? ap.accent : colors::textDim);
                g.setFont(fonts::archivo(7.0f, fonts::Bold, 0.08f));
                g.drawText("ST", n.rect.reduced(6, 3), juce::Justification::bottomRight);
            }
            if (ghost) g.setOpacity(1.0f);
        }

        const Node *nodeAt(juce::Point<int> p) const
        {
            for (const auto &n : mNodes)
                if (n.rect.expanded(2).contains(p)) return &n;
            return nullptr;
        }

        // Insertion caret for a point: returns {caret pixel pos, {lane, posInLane}}.
        std::pair<juce::Point<int>, std::pair<int, int>> caretFor(juce::Point<int> p) const
        {
            const int lane = p.x <= mSplitX ? 0 : (p.y <= getHeight() / 2 ? 1 : 2);
            const int laneY = lane == 0 ? trunkY() : (lane == 1 ? laneAY() : laneBY());
            int pos = 0, caretX = 0;
            const Node *last = nullptr;
            for (const auto &n : mNodes)
            {
                if (mDragging && n.sel == mDragSel) continue;
                const bool span = mBoard.slotStereo(n.sel); // shared column: sits on both rows
                if (n.lane != lane && !span) continue;
                if (p.x > n.rect.getCentreX())
                {
                    last = &n;                 // caret x may land AFTER a stereo pedal on either row
                    if (n.lane == lane) ++pos; // but position counts only this lane's own list
                }
            }
            if (last != nullptr)
                caretX = last->rect.getRight() + 4;
            else if (lane == 0)
                caretX = mIn.getRight() + 4;
            else
                caretX = mSplitX + 14;
            return {{caretX, laneY}, {lane, pos}};
        }

        void showNodeMenu(const Node &n)
        {
            juce::PopupMenu m;
            m.setLookAndFeel(&getLookAndFeel());
            const int slot = n.sel;
            m.addCustomItem(-1, std::make_unique<MenuSectionHeader>(mBoard.slotModelName(slot).toUpperCase()),
                            nullptr, {});
            const int lane = mBoard.slotLane(slot);
            m.addItem(1, "Route: Both amps", true, lane == 0);
            m.addItem(2, "Route: Amp A", true, lane == 1);
            m.addItem(3, "Route: Amp B", true, lane == 2);
            if (mBoard.slotCanStereo(slot))
            {
                m.addSeparator();
                // Stereo span: L -> Amp A, R -> Amp B (splitter on Both, bridge on a lane).
                m.addItem(5, "Stereo (span both amps)", true, mBoard.slotStereo(slot));
            }
            m.addSeparator();
            m.addItem(4, "Remove pedal");
            juce::Component::SafePointer<ChainStrip> sp(this);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(n.rect)),
                            [sp, slot](int r)
                            {
                                if (sp == nullptr || r <= 0) return;
                                if (r >= 1 && r <= 3) sp->mBoard.routePedal(slot, r - 1);
                                else if (r == 5) sp->mBoard.togglePedalStereo(slot);
                                else if (r == 4) sp->mBoard.removePedal(slot);
                            });
        }

        PedalboardPanel &mBoard;
        std::vector<Node> mNodes;
        juce::Rectangle<int> mIn, mAmpA, mAmpB;
        int mSplitX = 0;
        bool mDragging = false;
        int mDragSel = kSelNone;
        int mLedSel = kSelNone; // armed by a press on a node's LED (bypass click)
        juce::Point<int> mDragPos, mCaret{-1, 0}, mDropCaret{-1, 0};
    };

    // ================================================================ PaletteList
    // The pedal rack: category headers with the individual pedals underneath. Drag a
    // row onto the deck/chain (payload "pbadd:family:cat:model"), or click to add it
    // to the end of the trunk. Lives inside a vertical Viewport.
    class PaletteList : public juce::Component
    {
    public:
        explicit PaletteList(PedalboardPanel &b) : mBoard(b), mItems(b.buildCatalogue())
        {
            buildRows();
        }

        static constexpr int kHeaderRowH = 20, kItemRowH = 38;

        int contentHeight() const { return mRows.empty() ? 0 : mRows.back().rect.getBottom() + 6; }

        // Env/Comp rows are SINGLETON — unavailable once already on the board, even
        // when the board isn't full (which only blocks EVERY row).
        bool rowDisabled(const Item &it) const
        {
            return mBoard.boardFull() || ((it.family == 4 || it.family == 5) && mBoard.typePlaced(it.family));
        }

        void paint(juce::Graphics &g) override
        {
            const bool full = mBoard.boardFull();
            for (const auto &r : mRows)
            {
                if (r.item < 0) // category header
                {
                    g.setColour(colors::caption);
                    g.setFont(fonts::archivo(9.0f, fonts::SemiBold, 0.14f));
                    g.drawText(r.header, r.rect.reduced(10, 0), juce::Justification::bottomLeft);
                    g.setColour(colors::divider);
                    g.fillRect(r.rect.getX() + 8, r.rect.getBottom() - 1, r.rect.getWidth() - 16, 1);
                    continue;
                }
                const auto &it = mItems[(size_t)r.item];
                const bool disabled = rowDisabled(it);
                const bool hov = mHover == r.item && !disabled;
                if (hov)
                {
                    g.setColour(colors::tileSel);
                    g.fillRoundedRectangle(r.rect.toFloat().reduced(4.0f, 1.0f), 7.0f);
                }
                // livery bar
                g.setColour(it.ap.accent.withAlpha(disabled ? 0.35f : 0.95f));
                g.fillRoundedRectangle((float)r.rect.getX() + 8.0f, (float)r.rect.getY() + 7.0f,
                                       3.0f, (float)r.rect.getHeight() - 14.0f, 1.5f);
                auto tx = r.rect.reduced(19, 2);
                // mini glyph, right
                auto gb = tx.removeFromRight(30).toFloat().reduced(2.0f, 7.0f);
                const juce::Colour gc = it.ap.led.withAlpha(disabled ? 0.3f : (hov ? 0.95f : 0.6f));
                if (it.family == 1) paintDriveGlyph(g, it.cat, it.model, gb, gc);
                else if (it.family == 2) paintModGlyph(g, it.model, gb, gc);
                else if (it.family == 3) paintDelayGlyph(g, it.model, gb, gc);
                else if (it.family == 4) paintEnvGlyph(g, gb, gc);
                else paintCompGlyph(g, gb, gc);
                g.setColour(disabled ? colors::captionDim : colors::text);
                g.setFont(fonts::archivo(12.0f, fonts::SemiBold));
                g.drawText(it.name, tx.removeFromTop(tx.getHeight() / 2 + 2), juce::Justification::bottomLeft);
                g.setColour(disabled ? colors::captionDim : colors::caption);
                g.setFont(fonts::mono(9.0f));
                g.drawText(it.sub, tx, juce::Justification::topLeft);
            }
            if (full)
            {
                g.setColour(colors::textDim);
                g.setFont(fonts::mono(8.5f, fonts::Medium));
                g.drawText("board full (8)", getLocalBounds().removeFromTop(14).reduced(10, 0),
                           juce::Justification::centredRight);
            }
        }

        void resized() override { buildRows(); }

        void mouseMove(const juce::MouseEvent &e) override { setHover(itemAt(e.getPosition())); }
        void mouseExit(const juce::MouseEvent &) override { setHover(-1); }

        void mouseDrag(const juce::MouseEvent &e) override
        {
            if (mDragItem >= 0) return;
            if (e.getDistanceFromDragStart() < 6) return;
            const int it = itemAt(e.getMouseDownPosition());
            if (it < 0) return;
            const auto &item = mItems[(size_t)it];
            if (rowDisabled(item)) return;
            mDragItem = it;
            const juce::String desc = "pbadd:" + juce::String(item.family) + ":"
                                    + juce::String(item.cat) + ":" + juce::String(item.model);
            if (auto *dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                // drag image = a snapshot of just this row
                for (const auto &r : mRows)
                    if (r.item == it)
                    {
                        auto img = createComponentSnapshot(r.rect, true);
                        dnd->startDragging(desc, this, juce::ScaledImage(img), false);
                        break;
                    }
            }
        }
        void mouseUp(const juce::MouseEvent &e) override
        {
            const bool dragged = mDragItem >= 0;
            mDragItem = -1;
            if (dragged) return;
            const int it = itemAt(e.getPosition());
            if (it < 0 || it != itemAt(e.getMouseDownPosition())) return;
            const auto &item = mItems[(size_t)it]; // plain click: append to the trunk
            if (rowDisabled(item)) return;
            const int slot = mBoard.addPedalToTrunkEnd(item.family, item.cat, item.model);
            if (slot >= 0) mBoard.selectPedal(slot, true);
        }

    private:
        struct Row
        {
            int item; // -1 = header
            juce::String header;
            juce::Rectangle<int> rect;
        };

        void buildRows()
        {
            mRows.clear();
            int y = 2;
            juce::String lastHeader;
            for (int i = 0; i < (int)mItems.size(); ++i)
            {
                const juce::String h = paletteHeaderFor(mItems[(size_t)i]);
                if (h != lastHeader)
                {
                    lastHeader = h;
                    mRows.push_back({-1, h, {0, y, getWidth(), kHeaderRowH}});
                    y += kHeaderRowH + 2;
                }
                mRows.push_back({i, {}, {0, y, getWidth(), kItemRowH}});
                y += kItemRowH;
            }
        }
        int itemAt(juce::Point<int> p) const
        {
            for (const auto &r : mRows)
                if (r.item >= 0 && r.rect.contains(p)) return r.item;
            return -1;
        }
        void setHover(int h)
        {
            if (h != mHover) { mHover = h; repaint(); }
        }

        PedalboardPanel &mBoard;
        std::vector<Item> mItems;
        std::vector<Row> mRows;
        int mHover = -1, mDragItem = -1;
    };

public:
    // ================================================================== the panel
    explicit PedalboardPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("PEDALBOARD"), mApvts(apvts)
    {
        mDeck = std::make_unique<Deck>(*this);
        mDeckView = std::make_unique<juce::Viewport>();
        mDeckView->setViewedComponent(mDeck.get(), false);
        mDeckView->setScrollBarsShown(false, true);
        mDeckView->setScrollBarThickness(8);
        addAndMakeVisible(*mDeckView);

        mChain = std::make_unique<ChainStrip>(*this);
        addAndMakeVisible(*mChain);

        mPalette = std::make_unique<PaletteList>(*this);
        mPaletteView = std::make_unique<juce::Viewport>();
        mPaletteView->setViewedComponent(mPalette.get(), false);
        mPaletteView->setScrollBarsShown(true, false);
        mPaletteView->setScrollBarThickness(8);
        addAndMakeVisible(*mPaletteView);

        setWantsKeyboardFocus(true); // Delete/Backspace removes the selected pedal
        syncStructure(true);
        startTimerHz(20);
    }

    void refresh() { repaint(); } // editor-compat hook (the panel self-times)

    // Delete (or Backspace) removes the selected FREE pedal immediately. The locked
    // ENV/COMP pair (sel < 0) can't be deleted — the keypress just falls through.
    bool keyPressed(const juce::KeyPress &k) override
    {
        if (k.getKeyCode() != juce::KeyPress::deleteKey
            && k.getKeyCode() != juce::KeyPress::backspaceKey)
            return false;
        if (mSelSlot < 0 || !slotUsed(mSelSlot)) return false;
        removePedal(mSelSlot);
        return true;
    }

    void resized() override
    {
        auto body = bodyArea().reduced(14, 8);

        // Palette spans the FULL body height (top to bottom, alongside the chain
        // strip too) — carve it off first so nothing below it eats into its column.
        auto right = body.removeFromRight(kPalW);
        mPaletteView->setBounds(right);
        mPalette->setSize(right.getWidth() - mPaletteView->getScrollBarThickness(), 100);
        mPalette->setSize(mPalette->getWidth(), juce::jmax(right.getHeight(), mPalette->contentHeight()));
        body.removeFromRight(12);

        // Chain strip's right edge now stops at the palette's left edge instead of
        // running the full panel width.
        mChain->setBounds(body.removeFromBottom(kChainH));
        body.removeFromBottom(8);
        mDeckView->setBounds(body);
        layoutDeckSize();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        // Recessed DECK WELL: the pedal shelf is a near-black engraved surface
        // (StompPill slot family) so the enclosures stand off it — bypassed
        // pedals' neutral grey wash was near-invisible on the plain panel bg.
        if (mDeckView != nullptr)
        {
            auto w = mDeckView->getBounds().toFloat().expanded(4.0f);
            g.setColour(juce::Colour(0xff14171d));
            g.fillRoundedRectangle(w, 10.0f);
            g.setColour(juce::Colours::black.withAlpha(0.45f)); // engraved lip
            g.drawRoundedRectangle(w.reduced(0.5f), 10.0f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawRoundedRectangle(w.expanded(0.5f), 11.0f, 1.0f);
        }
    }

    // The bin floats above the deck viewport while a pedal is being dragged.
    void paintOverChildren(juce::Graphics &g) override
    {
        if (!mBinVisible) return;
        auto r = mBinRect.toFloat();
        g.setColour(colors::panel);
        g.fillRoundedRectangle(r, 12.0f);
        g.setColour(mBinHot ? colors::red : colors::outline);
        g.drawRoundedRectangle(r.reduced(0.75f), 12.0f, mBinHot ? 1.8f : 1.2f);
        const juce::Colour c = mBinHot ? colors::red : colors::textDim;
        g.setColour(c);
        auto b = r.withSizeKeepingCentre(24.0f, 26.0f); // trash-can glyph, no label
        juce::Path p;
        p.addRoundedRectangle(b.getX() + 3.0f, b.getY() + 7.0f,
                              b.getWidth() - 6.0f, b.getHeight() - 7.0f, 2.5f);
        p.startNewSubPath(b.getX(), b.getY() + 4.5f);
        p.lineTo(b.getRight(), b.getY() + 4.5f);
        p.startNewSubPath(b.getCentreX() - 4.0f, b.getY() + 4.5f);
        p.lineTo(b.getCentreX() - 4.0f, b.getY() + 1.0f);
        p.lineTo(b.getCentreX() + 4.0f, b.getY() + 1.0f);
        p.lineTo(b.getCentreX() + 4.0f, b.getY() + 4.5f);
        p.startNewSubPath(b.getCentreX() - 4.0f, b.getY() + 11.0f);
        p.lineTo(b.getCentreX() - 4.0f, b.getBottom() - 4.0f);
        p.startNewSubPath(b.getCentreX() + 4.0f, b.getY() + 11.0f);
        p.lineTo(b.getCentreX() + 4.0f, b.getBottom() - 4.0f);
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

private:
    void layoutBin()
    {
        if (mDeckView != nullptr)
            mBinRect = {mDeckView->getRight() - 74, mDeckView->getBottom() - 74, 60, 60};
    }

    void layoutDeckSize()
    {
        if (mDeckView == nullptr || mDeck == nullptr) return;
        const int h = mDeckView->getHeight() - mDeckView->getScrollBarThickness();
        mDeck->setSize(juce::jmax(mDeck->preferredWidth(), mDeckView->getWidth()), juce::jmax(60, h));
    }

    void scrollDeckToSelection()
    {
        if (mDeckView == nullptr || mDeck == nullptr) return;
        for (const auto &c : mDeck->cells())
            if (c.sel == mSelSlot)
            {
                const int want = c.bounds.getCentreX() - mDeckView->getWidth() / 2;
                mDeckView->setViewPosition(juce::jlimit(0, juce::jmax(0, mDeck->getWidth() - mDeckView->getWidth()), want),
                                           0);
                break;
            }
    }

    void repaintZones()
    {
        if (mDeck != nullptr) mDeck->repaint();
        if (mChain != nullptr) mChain->refreshLayout();
    }

    // Structure signature: anything that changes the deck/chain SHAPE (not knob values).
    std::uint64_t structureSig() const
    {
        std::uint64_t sig = 1; // fixed non-zero seed (no more frontOrder to fold in)
        for (int i = 0; i < kSlots; ++i)
        {
            std::uint64_t s = (std::uint64_t)(slotType(i) * 4 + slotLane(i)) + 1;
            s = s * 131u + (std::uint64_t)paramC(sid(i, "dCat")) + 1;
            s = s * 131u + (std::uint64_t)paramC(sid(i, "bModel")) + 1;
            s = s * 131u + (std::uint64_t)paramC(sid(i, "mType")) + 1;
            s = s * 131u + (std::uint64_t)paramC(sid(i, "pModel")) + 1;
            sig = sig * 1000003u + s;
        }
        sig = sig * 131u + (std::uint64_t)paramC("envfilterVoice");
        sig = sig * 131u + (std::uint64_t)paramC("compMode");
        return sig;
    }
    // Cosmetic signature: per-pedal On states (chain LEDs / cable dimming).
    std::uint64_t ledSig() const
    {
        std::uint64_t sig = 0;
        for (int i = 0; i < kSlots; ++i) sig = sig * 3u + (slotOn(i) ? 1u : 0u) + 1u;
        sig = sig * 3u + (paramF("envfilterOn") >= 0.5f ? 1u : 0u);
        sig = sig * 3u + (paramF("compOn") >= 0.5f ? 1u : 0u);
        return sig;
    }

    void syncStructure(bool force)
    {
        const std::uint64_t sig = structureSig();
        if (!force && sig == mLastStructureSig) return;
        mLastStructureSig = sig;
        if (mDeck != nullptr)
        {
            mDeck->rebuild();
            layoutDeckSize();
        }
        if (mChain != nullptr) mChain->refreshLayout();
        // Structure changes can flip a palette row's disabled state (e.g. removing
        // the placed Comp/Env singleton un-grays its RACK row) — rowDisabled() is
        // re-evaluated fresh in paint(), so it just needs a nudge; without this the
        // row stays visually stale until an unrelated repaint (e.g. mouse hover).
        if (mPalette != nullptr) mPalette->repaint();
        const Lanes ln = lanesNow();
        juce::String hdr = (!ln.l[1].empty() || !ln.l[2].empty()) ? "A/B SPLIT" : juce::String();
        setHeaderRight(hdr);
    }

    void timerCallback() override
    {
        syncStructure(false);
        if (mScrollPending)
        {
            mScrollPending = false;
            scrollDeckToSelection();
        }
        if (mDeck != nullptr) mDeck->refreshPedals();
        const std::uint64_t led = ledSig();
        if (led != mLastLedSig)
        {
            mLastLedSig = led;
            if (mChain != nullptr) mChain->repaint();
            if (mDeck != nullptr) mDeck->repaint();
        }
    }

    juce::AudioProcessorValueTreeState &mApvts;
    std::unique_ptr<Deck> mDeck;
    std::unique_ptr<juce::Viewport> mDeckView;
    std::unique_ptr<ChainStrip> mChain;
    std::unique_ptr<PaletteList> mPalette;
    std::unique_ptr<juce::Viewport> mPaletteView;
    int mSelSlot = kSelNone;
    bool mScrollPending = false;
    juce::Rectangle<int> mBinRect;          // remove-dropzone (drag a pedal onto it)
    bool mBinVisible = false, mBinHot = false;
    std::uint64_t mLastStructureSig = ~0ull, mLastLedSig = ~0ull;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalboardPanel)
};

} // namespace nam_rig::ui
