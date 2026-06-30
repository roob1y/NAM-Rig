# IR Sourcing for Commercial Bundling — Licensing Research

**Goal:** Source one vintage-style **PLATE** and one large **STUDIO ROOM** stereo IR that can be legally
**bundled and redistributed** inside the commercial NAM Rig VST3.
**Date:** 2026-06-30. Verified against actual EULA/terms pages, not marketing copy.

> ⚠️ This is a documentation summary, not legal advice. For anything you ship, confirm the current EULA
> text directly from the rights-holder and keep a saved copy as evidence of the grant at time of bundling.

---

## Headline finding

**No premium IR vendor sells an off-the-shelf "developer/OEM/redistribution" tier** you can simply buy.
Every commercial library investigated is one of:
- **Music-use-only** end-user license (explicitly insufficient — bundling forbidden), or
- **Encrypted / host-locked** (LiquidSonics Fusion-IR, Wave Arts Convology, Altiverb), or
- **Captures of trademarked hardware** (EMT / Lexicon / Bricasti) the vendor is in no position to sub-license.

The only citable, legally-clean paths to embed plate + room IRs in a product you sell are **CC-BY free
libraries** (attribution required) or a **custom written license negotiated directly** with a producer.

---

## Comparison table

| Source | Price | Bundle in commercial VST3? | License clause (verified) | Character fit | Format/SR | Link |
|---|---|---|---|---|---|---|
| **Hopkins EMT-140 Plate** | Free | **YES + attribution** ⚠️ provenance caveat | CC-BY ("Attribution-Any") declared in oramics metadata; original first-party page is dead | **Plate ✅** real EMT-140, bright/dark/medium families — bright, smooth, classic studio plate | Stereo WAV, SR not stated (inspect files) | https://oramics.github.io/sampled/IR/EMT140-Plate/ |
| **OpenAIR (per-space)** | Free | **YES + attribution** (per-file only) | Page footer: "This work is licensed under a Creative Commons Attribution 4.0 International License" | **Room/Hall ✅** large studio/live rooms (e.g. Genesis 6 Live Room), wide/decorrelated B-format | mono/stereo/B-format, many **96 kHz** | https://www.openair.hosted.york.ac.uk/?page_id=483 |
| Freesound.org CC0 files | Free | YES (CC0) / **NO** if -NC or -SA | Per-file CC license; use the "Creative Commons 0" license facet | Variable; mostly DIY captures, often noisy/mono | Per file | https://freesound.org |
| Past To Future Reverbs | Paid | **NO as-is** → custom deal possible | "may not upload, republish, or distribute the data file"; output-only royalty-free | EMT-140 plate + Quantec/Lexicon rooms | 24-bit/96k WAV | Gumroad store |
| Samplicity Bricasti M7 | Free | **NO** | Free "to comply with the request from Bricasti... as long as these IRs are **not included in any commercial product**" | Plate/hall/room, true-stereo | 24-bit WAV (44.1k) | https://samplicity.com/downloads/ |
| Voxengo IM Reverbs | Free | **NO** | Clause 3: "may not... earn any direct or indirect profit from their distribution"; Clause 4c: "no charge is associated with the distribution" | Modeled halls/rooms (not real captures) | 44.1k/16-bit WAV | https://www.voxengo.com/impulses/ |
| Audio Ease Altiverb | Paid | **NO** | "licensed only for use in Altiverb... may not... distribute... in whole or in part... may not re-sample or re-record" | Excellent true-stereo rooms, 96k | host-locked | https://www.audioease.com |
| LiquidSonics | Paid | **NO** | Encrypted Fusion-IR; redistribution banned. Free Bricasti M7 captures need Bricasti's written permission | Plates/rooms | proprietary | https://www.liquidsonics.com/eula-tc/ |
| Wave Arts / Impulse Record | Paid | **NO** | IRs encrypted & locked to Convology XT; captures are 3rd-party EMT/Lexicon IP | EMT-140/250 plates, Real Spaces rooms | 4-ch TS WAV/AIF | https://impulserecord.com |
| Numerical Sound | Paid | **NO** (extended license by request) | Per-product: "musical compositions only"; distribution & competitive use forbidden | Scoring-stage/hall rooms; **no true plate** | 44.1/48k & 88/96k WAV | ernest@numericalsound.com |
| Inspired Acoustics | Paid | **NO** | EULA: "may not... incorporate in... any other product"; resale forbidden | Plate not found | unconfirmed | https://www.inspiredacoustics.com/en/eula |
| 3 Sigma Audio | Paid | **NO** (sells a reverb *plugin*, not reverb IRs) | n/a | n/a | n/a | https://www.3sigmaaudio.com/faq/ |
| Echo Thief | Free | **NO** ("let's talk") | "You are welcome to use the... Library to create derivative work... If you would like to use it in any other way, let's talk." | Caves/bridges/odd spaces — no plate or clean room | WAV | https://www.echothief.com |
| Soundwoofer | Free | **AMBIGUOUS → NO** | No binding written grant; user-upload provenance risk; cab IRs only (no reverb) | Cab IRs only | WAV | https://soundwoofer.com |
| "Dr Reverb" | — | **VENDOR NOT FOUND** — likely a misremembered name (probably Past To Future Reverbs) | — | — | — | — |
| Adventure Kid (AKRT) | Free | YES + attribution (CC-BY 4.0) | CC-BY 4.0 | Rooms/springs — **no plate**; 44.1k/32-float only | 44.1k WAV | https://www.adventurekid.se |

---

## Top recommendations

### PLATE → Greg Hopkins EMT-140 (CC-BY)
The only realistic redistributable plate that exists. A real EMT-140 captured in bright / dark / medium
voicings — bright, dense-from-onset, smooth, classic studio plate. CC-BY permits commercial redistribution
and embedding with attribution and no per-unit royalty.

**Provenance caveat (must resolve before shipping):** the CC-BY designation comes from the third-party
oramics/sampled catalogue; Hopkins' own original license page is dead. Consensus is firmly CC-BY, but for a
product you sell: (1) email Hopkins to confirm the grant + exact attribution string in writing, (2) save the
oramics metadata + any reply in the repo as evidence, (3) inspect each WAV's SR / true-stereo status,
(4) do **not** use the "EMT" trademark in branding — keep calling it "vintage plate."

### ROOM → OpenAIR CC-BY 4.0 spaces (University of York)
Genuine large studio/live rooms, many at 96 kHz, often B-format (SoundField) captures that decode to wide,
decorrelated true-stereo. CC-BY 4.0 permits commercial use + redistribution with attribution.

**Per-file rule (critical):** the license is chosen per upload. You **must** confirm each space's page footer
reads plain **CC-BY 4.0** and reject anything marked **-NC** (non-commercial = forbidden in a paid product)
or **-SA** (ShareAlike = copyleft, would force your bundle open). Attribute "Audiolab, University of York"
plus the named capture team. Audition for a tight studio room vs. a long concert hall — many OpenAIR spaces
are hall-scale.

### If you'd rather pay for clean, no-attribution, guaranteed assets
There is no shelf product — you negotiate a **custom written embedded/redistribution license**. Most
approachable channels: **Past To Future Reverbs** (responsive solo producer; already has EMT-140 plate +
96k rooms) and **Impulse Record's custom-capture service** (impulserecord.com/services).

---

## Ambiguity flags (email before relying on any of these)
1. **Hopkins EMT-140 CC-BY** — primary license page rotted; confirm grant + attribution string in writing.
2. **OpenAIR** — verify the per-page footer license on every individual file; CC-BY only, never -NC / -SA.
3. **Past To Future / Numerical Sound / Impulse Record** — no published OEM tier; a custom deal must be
   requested and put in writing.
4. **Any "free download" pack** (Samplicity, Voxengo, fokkie, Nevo) — "free to use" ≠ "free to redistribute
   in a product." Default assumption is NO unless the license explicitly grants software embedding.

---

## Vendor email request template

> Subject: Licensing your impulse responses for inclusion in a commercial plugin
>
> Hi [name],
>
> I'm the developer of NAM Rig, a commercial VST3 guitar plugin. I'd like to include [a small selection of /
> the X] impulse responses from [product / library] **embedded inside the plugin**, where they ship as built-in
> reverb presets to paying customers.
>
> To be clear about scope, I need a license that grants:
>  • the right to **redistribute the IR files embedded inside my software product**, sold to end users;
>  • **royalty-free / no per-unit royalty** terms;
>  • end users receive only the bundled reverb sound — they cannot extract or re-use the raw IR files.
>
> Your standard end-user license appears to cover use in music productions but not redistribution inside a
> software product, so I wanted to ask directly. Do you offer a **developer / OEM / bundle license** for this?
> If so, could you share the terms and pricing? If a custom agreement is needed, I'm happy to discuss.
>
> For attribution-based licenses, please also confirm the exact credit string you'd like displayed in the
> plugin's About/credits.
>
> Thanks very much,
> Robbie — NAM Rig

---

## Practical close
- Cleanest ship path today: **Hopkins EMT-140 (plate, CC-BY)** + **one or two confirmed-CC-BY OpenAIR rooms**,
  with an attribution block in the plugin About/credits and saved copies of every license page/metadata.
- These are single-source captures — audition for baked-in noise floor and confirm true-stereo / SR before
  committing. You can truncate for the short-decay variants as planned.
- This reinforces the existing EULA-safe stance: stock commercial IR licenses essentially never permit
  software redistribution; CC-BY academic/free sets (or a bespoke written deal) are the only clean routes.
