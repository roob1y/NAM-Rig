// preset_test — verification harness for the .namrig single-file preset
// format (PresetFile.h, juce_core only). Exits nonzero on any FAIL.
//
// T1 param map round-trips (values exact within 1e-9)
// T2 model text round-trips BYTE-EXACT (quotes/newlines/backslashes/unicode)
// T3 IR bytes round-trip BYTE-EXACT (pseudo-random binary through base64)
// T4 malformed inputs are rejected (wrong tag, newer version, no params,
//    truncated JSON, empty model payload)
// T5 file write/read round-trip
// T6 params-only preset: hasModel/hasIr false, still valid
#include "PresetFile.h"
#include <cstdio>
#include <random>

using nam_rig::PresetFile;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static PresetFile makeFull()
{
    PresetFile p;
    p.name = "Test Rig";
    auto *params = new juce::DynamicObject();
    params->setProperty("inputGain", -6.5);
    params->setProperty("gateThresh", -50.0);
    params->setProperty("compSustain", 0.35);
    params->setProperty("eq1k", 4.2);
    params->setProperty("delayTime", 375.0);
    params->setProperty("delayPingPong", 1.0);
    params->setProperty("revDecay", 2.7);
    params->setProperty("oversample", 2.0);
    p.params = juce::var(params);
    p.modelName = "DRRI clean";
    p.modelText = juce::String::fromUTF8(
        "{\"version\": \"0.5.4\",\n"
        "  \"metadata\": {\"name\": \"weird \\\"quoted\\\" \\\\path\\\\ \xc3\xa9\xc3\xa1\"},\n"
        "  \"weights\": [1.0e-7, -2.345678901234567, 0.0]}\n");
    p.irName = "SM57 4x12";
    return p;
}

int main()
{
    // ---- T1: param values round-trip ----
    {
        auto p = makeFull();
        PresetFile q;
        CHECK(PresetFile::parse(p.toJson(), q), "T1 full preset parses");
        auto *a = p.params.getDynamicObject();
        auto *b = q.params.getDynamicObject();
        bool ok = b != nullptr && a->getProperties().size() == b->getProperties().size();
        double worst = 0;
        if (ok)
            for (const auto &kv : a->getProperties())
            {
                const double va = (double)kv.value;
                const double vb = (double)b->getProperty(kv.name);
                worst = std::max(worst, std::abs(va - vb));
            }
        CHECK(ok && worst < 1e-9, "T1 %d params round-trip (worst err %.2e)",
              a->getProperties().size(), worst);
        CHECK(q.name == p.name, "T1 name round-trips");
    }

    // ---- T2: model text byte-exact ----
    {
        auto p = makeFull();
        PresetFile q;
        PresetFile::parse(p.toJson(), q);
        CHECK(q.modelText == p.modelText,
              "T2 model text byte-exact (%d chars, quotes/escapes/UTF-8)",
              p.modelText.length());
        CHECK(q.modelName == p.modelName, "T2 model name round-trips");
    }

    // ---- T3: IR bytes byte-exact through base64 ----
    {
        auto p = makeFull();
        std::mt19937 rng(1234);
        juce::MemoryBlock blob(100001); // odd size: exercises base64 padding
        auto *bytes = static_cast<unsigned char *>(blob.getData());
        for (size_t i = 0; i < blob.getSize(); ++i)
            bytes[i] = (unsigned char)(rng() & 0xff);
        p.irBytes = blob;
        PresetFile q;
        CHECK(PresetFile::parse(p.toJson(), q), "T3 preset with binary IR parses");
        CHECK(q.irBytes == p.irBytes, "T3 IR bytes byte-exact (%zu bytes)",
              p.irBytes.getSize());
        CHECK(q.irName == p.irName, "T3 IR name round-trips");
    }

    // ---- T4: malformed inputs rejected ----
    {
        PresetFile q;
        CHECK(!PresetFile::parse("not json at all {", q), "T4 garbage rejected");
        CHECK(!PresetFile::parse("{\"format\":\"other\",\"version\":1,\"params\":{}}", q),
              "T4 wrong format tag rejected");
        CHECK(!PresetFile::parse(
                  "{\"format\":\"nam-rig-preset\",\"version\":99,\"params\":{}}", q),
              "T4 newer version rejected");
        CHECK(!PresetFile::parse("{\"format\":\"nam-rig-preset\",\"version\":1}", q),
              "T4 missing params rejected");
        CHECK(!PresetFile::parse(
                  "{\"format\":\"nam-rig-preset\",\"version\":1,\"params\":{},"
                  "\"model\":{\"name\":\"x\"}}", q),
              "T4 model without payload rejected");
        auto truncated = makeFull().toJson();
        truncated = truncated.substring(0, truncated.length() / 2);
        CHECK(!PresetFile::parse(truncated, q), "T4 truncated JSON rejected");
    }

    // ---- T5: file write/read round-trip ----
    {
        auto p = makeFull();
        std::mt19937 rng(7);
        juce::MemoryBlock blob(4096);
        auto *bytes = static_cast<unsigned char *>(blob.getData());
        for (size_t i = 0; i < blob.getSize(); ++i)
            bytes[i] = (unsigned char)(rng() & 0xff);
        p.irBytes = blob;

        const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("preset_test_rig.namrig");
        CHECK(p.writeToFile(tmp), "T5 writeToFile");
        PresetFile q;
        CHECK(PresetFile::readFromFile(tmp, q), "T5 readFromFile");
        CHECK(q.modelText == p.modelText && q.irBytes == p.irBytes && q.name == p.name,
              "T5 file round-trip identical");
        tmp.deleteFile();
        PresetFile r;
        CHECK(!PresetFile::readFromFile(tmp, r), "T5 missing file rejected");
    }

    // ---- T6: params-only preset ----
    {
        PresetFile p;
        p.name = "knobs only";
        auto *params = new juce::DynamicObject();
        params->setProperty("outputGain", 3.0);
        p.params = juce::var(params);
        PresetFile q;
        CHECK(PresetFile::parse(p.toJson(), q), "T6 params-only preset parses");
        CHECK(!q.hasModel() && !q.hasIr(), "T6 hasModel/hasIr false");
        CHECK((double)q.params.getDynamicObject()->getProperty("outputGain") == 3.0,
              "T6 value intact");
    }

    // ---- T7: v2 round-trips Rig B model + IR alongside Rig A ----
    {
        auto p = makeFull();
        std::mt19937 rng(99);
        juce::MemoryBlock irA(2048), irB(3072);
        for (size_t i = 0; i < irA.getSize(); ++i)
            ((unsigned char *)irA.getData())[i] = (unsigned char)(rng() & 0xff);
        for (size_t i = 0; i < irB.getSize(); ++i)
            ((unsigned char *)irB.getData())[i] = (unsigned char)(rng() & 0xff);
        p.irBytes = irA;
        p.modelNameB = "JCM800 lead";
        p.modelTextB = juce::String::fromUTF8(
            "{\"version\":\"0.5.4\",\"rigB\":true,\"weights\":[9.9e9,-1.0]}");
        p.irNameB = "V30 2x12";
        p.irBytesB = irB;

        PresetFile q;
        CHECK(PresetFile::parse(p.toJson(), q), "T7 v2 dual-rig preset parses");
        CHECK(q.modelTextB == p.modelTextB && q.modelNameB == p.modelNameB,
              "T7 Rig B model byte-exact");
        CHECK(q.irBytesB == p.irBytesB && q.irNameB == p.irNameB, "T7 Rig B IR byte-exact");
        CHECK(q.modelText == p.modelText && q.irBytes == p.irBytes, "T7 Rig A still intact");
        CHECK(q.hasModelB() && q.hasIrB(), "T7 hasModelB/hasIrB true");
        const auto v = juce::JSON::parse(p.toJson());
        CHECK((int)v.getDynamicObject()->getProperty("version") == PresetFile::kVersion,
              "T7 version stamped %d", PresetFile::kVersion);
    }

    // ---- T8: a v1 file (no Rig B) still loads, model -> Rig A ----
    {
        const char *v1 =
            "{\"format\":\"nam-rig-preset\",\"version\":1,\"name\":\"old\","
            "\"params\":{\"outputGain\":1.5},"
            "\"model\":{\"name\":\"old amp\",\"nam\":\"{\\\"v\\\":1}\"}}";
        PresetFile q;
        CHECK(PresetFile::parse(v1, q), "T8 v1 preset parses");
        CHECK(q.hasModel() && q.modelName == "old amp", "T8 v1 model -> Rig A");
        CHECK(!q.hasModelB() && !q.hasIrB(), "T8 v1 has no Rig B");
        CHECK((double)q.params.getDynamicObject()->getProperty("outputGain") == 1.5,
              "T8 v1 params intact");
    }

    std::printf("\n%s (%d FAIL)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
