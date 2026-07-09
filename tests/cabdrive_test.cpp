// cabdrive_test — verification for the Dynamic Cab drive calibration
// (src/CabDriveCal.h), wired in PluginProcessor::calibrateCabDrive via
// RigChain::measurePreCabLevels. Pure/offline (no JUCE): exits nonzero on FAIL.
//
// T1 reference level -> 0 dB trim (a correctly-driven amp needs no correction)
// T2 quiet capture (below ref) -> POSITIVE trim (scale the detectors up)
// T3 hot capture (above ref)   -> NEGATIVE trim (scale the detectors down)
// T4 trim exactly cancels the pre-cab error: measured + trim == reference
// T5 extreme levels clamp to +/- kMaxTrimDb (matches the param range)
// T6 silence / invalid RMS -> 0 dB (nothing meaningful to calibrate)

#include "CabDriveCal.h"

#include <cmath>
#include <cstdio>

using nam_rig::CabDriveCal;

static int gFails = 0;
#define CHECK(cond, ...)                                                        \
    do                                                                          \
    {                                                                           \
        const bool chkOk_ = (cond);                                             \
        std::printf("%s: ", chkOk_ ? "PASS" : "FAIL");                          \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!chkOk_)                                                            \
            ++gFails;                                                           \
    } while (0)

static bool approx(float a, float b, float tol = 1.0e-3f)
{
    return std::fabs(a - b) <= tol;
}

// dBFS -> linear RMS, so tests can express levels in the same dB the doc uses.
static double dbfs(float db) { return std::pow(10.0, db / 20.0); }

int main()
{
    const float ref = CabDriveCal::kRefPreCabDbFS; // -14 dBFS

    // ---- T1: measured == reference -> no correction ----
    CHECK(approx(CabDriveCal::speakerDriveDb(dbfs(ref)), 0.0f),
          "T1 ref level (%.1f dBFS) -> 0 dB trim", ref);

    // ---- T2: quiet capture -> positive trim ----
    const float trimQuiet = CabDriveCal::speakerDriveDb(dbfs(ref - 6.0f));
    CHECK(approx(trimQuiet, 6.0f),
          "T2 quiet (-6 dB below ref) -> +6 dB trim (got %.2f)", trimQuiet);

    // ---- T3: hot capture -> negative trim ----
    const float trimHot = CabDriveCal::speakerDriveDb(dbfs(ref + 6.0f));
    CHECK(approx(trimHot, -6.0f),
          "T3 hot (+6 dB above ref) -> -6 dB trim (got %.2f)", trimHot);

    // ---- T4: trim cancels the error exactly across a sweep ----
    bool cancelsOk = true;
    for (float err = -12.0f; err <= 12.0f; err += 1.5f)
    {
        const float trim = CabDriveCal::speakerDriveDb(dbfs(ref + err));
        // detectors see measured + trim; should land back on the reference.
        if (!approx((ref + err) + trim, ref, 1.0e-2f))
            cancelsOk = false;
    }
    CHECK(cancelsOk, "T4 measured + trim == reference across +/-12 dB");

    // ---- T5: clamp to the param range ----
    CHECK(approx(CabDriveCal::speakerDriveDb(dbfs(ref - 40.0f)), CabDriveCal::kMaxTrimDb),
          "T5 very quiet clamps to +%.0f dB", CabDriveCal::kMaxTrimDb);
    CHECK(approx(CabDriveCal::speakerDriveDb(dbfs(ref + 40.0f)), -CabDriveCal::kMaxTrimDb),
          "T5 very hot clamps to -%.0f dB", CabDriveCal::kMaxTrimDb);

    // ---- T6: silence / invalid -> 0 dB ----
    CHECK(CabDriveCal::speakerDriveDb(0.0) == 0.0f, "T6 silence -> 0 dB");
    CHECK(CabDriveCal::speakerDriveDb(-1.0) == 0.0f, "T6 negative RMS -> 0 dB");

    std::printf("\n%s (%d failure%s)\n", gFails == 0 ? "ALL PASS" : "FAILURES",
                gFails, gFails == 1 ? "" : "s");
    return gFails == 0 ? 0 : 1;
}
