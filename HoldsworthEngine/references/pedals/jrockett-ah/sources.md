# Sources and evidence limits

Retrieved/checked 2026-09-12. Rank is fitness for this design, not popularity.
**P** = manufacturer documentation; **C** = firsthand circuit observation;
**O** = firsthand listening; **I** = inference; **A** = assumption;
**V** = provisional software choice. High confidence means explicit primary
documentation; medium means credible but unit/access-limited; low means an
unverified inference. A/V never become product facts through repetition.

## Ranked register

1. **P1 — original manufacturer operating manual, diagram edition.**
   [Thomann-hosted PDF](https://images.thomann.de/pics/atg/atgdata/document/manual/allan_holdsworth.pdf),
   two pages, undated; page 1 controls/routing/history, page 2 warranty.
   Primary authored material on a dealer mirror, not a dealer-written summary.
   Downloaded and page 1 rendered with macOS PDFKit and inspected; web PDF
   screenshots failed. SHA-256:
   `8f1706da1b8762c92b82b6ebfe4dea5a550901a4cf5acef21dca7538da5ea9ac`.
   **High** for visible instructions. The PDF text layer also exposes reversed
   pot/component-like labels (GAIN A50K, FAT C25K, TONE B10K, GLEVEL B100K,
   BLEVEL B25K) absent from the visible diagram. These are not a service BOM or
   netlist; do not promote hidden artwork text into fitted component facts.

2. **P2 — manufacturer operating manual, photograph/power edition.**
   [Gear4music-hosted PDF](https://r2.gear4music.com/media/13/135418/download_135418_1.pdf),
   linked by the dealer's [product page](https://www.gear4music.de/de/Gitarre-and-Bass/OFFLINE-Rockett-Allan-Holdsworth-Overdrive-Boost-Pedal-Pedale/19AW).
   Two pages, undated; page 1 controls/routing/history/power, page 2 warranty.
   Downloaded, extracted and page 1 visually checked with PDFKit. SHA-256:
   `84d380c7a526adc0e94151896c72a2981326836a1c4e2fa29c9fb393f2c02173`.
   **High**; agrees with P1 and explicitly specifies 9 V, battery/adapter and
   polarity. Their chronology and correspondence to PCB revisions are unknown.
   A [Scribd transcription](https://www.scribd.com/document/460051156/JRAD-Holdsworth-Manual-and-Warranty)
   provided discovery corroboration only; its AI summary is not evidence.

3. **C1 — original owner inspection/recreation thread.**
   [Freestompboxes discussion](https://www.freestompboxes.org/viewtopic.php?t=24343).
   Paul Marossy, 25–26 October 2024: reports a Rev 8 board, JRC4558,
   corrected counts of four 2N3906 and two 2N5485, Drive 50K-A Gain pot,
   and pairs of 1N4148/1N4007. **Medium for reported parts on that unit**;
   no independently inspected full board images or continuity trace here.
   His Blue Note/Lemon Aid relationship is an inference, not a completed trace.
   Gupta, 21 September 2025: reports Rev 7 and a KiCad recreation underway.
   This later post was available through the search index; the opened cached
   page ended in 2024 and direct fetching returned 403. No finished KiCad files,
   healthy-unit response data or verified full schematic retrieved.
   Earlier 2014 posts explicitly speculate from limited photos and disagree
   about transistor roles. Those guesses cannot specify clipping or EQ order.

4. **O1 — firsthand listening, secondary for specifications.**
   [Guitar.com review](https://guitar.com/reviews/review-j-rockett-audio-designs-pedals/),
   indexed AH section, describes interactive controls and modest drive;
   [Pedal of the Day, 25 June 2015](https://www.pedal-of-the-day.com/2015/06/25/j-rockett-audio-designs-allan-holdsworth-overdriveboost/)
   reports separate use and engaging both footswitches together. **Medium for
   these reviewers' experience, low for quantitative modeling.** These are
   primary listening accounts, not transfer-function measurements; embedded
   demos were not analyzed or used to fit a response.

5. **Discovery leads / excluded technical authority.**
   [Original 2012 photo/review lead](https://thoughtfulguitarist.com/2012/12/rockett-allan-holdsworth-overdrive-review/)
   was inaccessible (expired TLS certificate); its photo was discussed in C1.
   [DIYStompboxes thread](https://www.diystompboxes.com/smfforum/index.php?topic=132689.0)
   was discoverable but returned 403.
   [Layout request](https://guitar-fx-layouts.238.s1.nabble.com/Allan-Holdsworth-Signature-OD-Boost-td8387.html)
   supplies an older unresolved trace request, not proof of present absence.
   Dealer/category claims such as “Hi + lo shelf,” translated dB figures,
   true bypass and database current consumption do not establish circuit values.
   Other Rockett models, guessed clones and artist-gear aggregators are excluded
   as AH circuit or Allan-settings authority.

## Claim coverage and unresolved questions

| Topic | Evidence | What remains unknown |
| --- | --- | --- |
| Controls and discrete choices | P1/P2 page 1, high | Pot tapers/endpoints, unity locations, gain range in dB |
| Cascade | P1/P2 repeatedly state Boost into Drive; high | Switch wiring, bypass impedance/loading and transient behavior |
| F/C/T | P1 diagram Fat, prose Full; P2 prose Full; Clean and Treble agree | Curves, relative loudness, whether F changes gain/bandwidth/nonlinearity |
| L/H | P1/P2 say low/high emphasis | Shelf vs cutoff vs tilt; frequencies, amount, dependence on F/C/T and level |
| Bass/Treble | P1/P2 assign them to Drive | Cut-only vs boost/cut, centers, range, loading, pre/post-clipping placement; noon is not established flat |
| Gain/Volume | P1/P2 distinguish drive gain from drive level | Gain-zero behavior, output range, gain-dependent EQ, stage saturation and taper |
| Supply/headroom | P2: 9 V supply, center negative | Internal rails/bias/charge pump, input/output swing, current, THD thresholds; no support for an 18 V mode |
| Schematic/repair/teardown | C1 parts and revision observations | No retrieved factory/service schematic, full verified AH trace or quantitative repair data |
| Measurements | No controlled dataset located in bounded search | Terminal AC responses, calibrated sweeps, impedance, clipping/dynamics, revision differences |
| History | P1/P2 describe development with Allan for live/studio needs | Actual mode use and knob settings; no artist settings inferred |

**I:** Increasing Boost before Drive can increase or reshape Drive excitation;
it need not produce a proportional final volume increase. Gain likely changes
distortion as well as level, while Volume serves a level-setting role. This
functional interpretation does not locate either pot or prove that Volume
cannot affect any internal saturation. A named Clean mode also does not prove
flat response or unlimited headroom.

## Research stop

Searched manufacturer/dealer manual sources and combinations of Rockett,
Holdsworth, schematic, trace, repair, teardown, KiCad, frequency response,
measurement, oscilloscope, dB and headroom. The current manufacturer home/RMA
pages supplied no retrieved AH service material. Accessible primary operating
documents settle the interface; the owner thread provides bounded parts leads.
No calibrated transfer data or complete verified AH schematic was found.
This is a retrieval limit, not a claim that none exists anywhere. Do not spend
M1 reconstructing Blue Note/Lemon Aid/Tim Pierce circuits. Further investigation
belongs only to the evidence-triggered M3 described in [milestones.md](milestones.md).
