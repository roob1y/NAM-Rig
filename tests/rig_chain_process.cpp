// rig_chain_process — offline full-rig processor: runs the literally-shared
// RigChain code (same blocks, same order, same PDC math as the plugin) on a
// raw f32 file. The rig's verification harness: as each block gains DSP, its
// offline measurements run through this.
//
// With only amp + cab active and no IR, output must be bit-identical to
// NAM-AA's chain_process for the same model/rate/factor — that's the chain
// host's first verification gate.
//
// Usage: rig_chain_process <model.nam|-> <ir.wav|-> <dawRate> <requestedFactor> <in.f32> <out.f32> [block]
//        ("-" skips loading that stage)

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "rig/RigChain.h"

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        std::fprintf(stderr,
                     "usage: rig_chain_process <model.nam|-> <ir.wav|-> <dawRate> <requestedFactor> <in.f32> <out.f32> [block]\n");
        return 1;
    }
    try
    {
        const std::string modelPath = argv[1];
        const std::string irPath = argv[2];
        const double dawRate = std::atof(argv[3]);
        const int requested = std::atoi(argv[4]);
        const std::string inPath = argv[5];
        const std::string outPath = argv[6];
        const int block = (argc >= 8) ? std::atoi(argv[7]) : 512;

        nam_rig::RigChain chain;

        if (modelPath != "-")
        {
            const auto info = chain.amp.engine().loadModel(modelPath);
            if (!info.ok)
            {
                std::fprintf(stderr, "model load failed: %s\n", info.error.c_str());
                return 1;
            }
        }

        chain.prepare(dawRate, block);
        chain.amp.setRequestedFactor(requested);

        if (irPath != "-")
        {
            if (!chain.cab.loadIr(juce::File(juce::String(irPath))))
            {
                std::fprintf(stderr, "IR load failed: %s\n", irPath.c_str());
                return 1;
            }
        }

        std::fprintf(stderr, "[rig] daw=%.0f req=%d | PDC=%.2f smp\n",
                     dawRate, requested, chain.latencySamples());

        // ---- input ----
        std::ifstream fi(inPath, std::ios::binary);
        if (!fi)
        {
            std::fprintf(stderr, "cannot open %s\n", inPath.c_str());
            return 1;
        }
        fi.seekg(0, std::ios::end);
        const size_t total = (size_t)fi.tellg() / sizeof(float);
        fi.seekg(0);
        std::vector<float> x(total);
        fi.read(reinterpret_cast<char *>(x.data()), (std::streamsize)(total * sizeof(float)));

        // ---- block loop through the shared chain (mono buffer) ----
        juce::AudioBuffer<float> buf(1, block);
        size_t pos = 0;
        while (pos < total)
        {
            const int n = (int)std::min<size_t>(block, total - pos);
            buf.setSize(1, n, false, false, true);
            std::memcpy(buf.getWritePointer(0), x.data() + pos, (size_t)n * sizeof(float));
            chain.process(buf);
            std::memcpy(x.data() + pos, buf.getReadPointer(0), (size_t)n * sizeof(float));
            pos += (size_t)n;
        }

        std::ofstream fo(outPath, std::ios::binary);
        fo.write(reinterpret_cast<const char *>(x.data()), (std::streamsize)(total * sizeof(float)));
        std::fprintf(stderr, "[rig] processed %zu samples -> %s\n", total, outPath.c_str());
        return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
