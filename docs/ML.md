# ZeraBrain — the self-training map model

ZeraBrain is a small machine-learning system built into ZeraLands. **Every map you make teaches it something.**
Right now its job is to *learn* — to build up a dataset and a set of models that understand how prompts,
environments and eras relate to terrain shapes — so that a future version of ZeraLands can generate maps
directly from a text prompt. It is written in plain C++ (no Python, no GPU, no external ML library) and trains
in milliseconds on the CPU.

> Status: **learning only.** The generator does not yet use ZeraBrain's predictions. You can peek at what it has
> learned in the editor (Brain tab → *Sketch the current prompt*, *Novelty*), and the dataset it collects is
> designed to be reused by much bigger models later.

---

## 1. When does it learn?

| Event | Source tag | Sample weight | Genes known? |
|---|---|---|---|
| A map is generated (editor *Generate* or CLI) | `generated` | 1.0 | yes |
| A heightmap image is imported | `imported` | 1.0 | no |
| A real place is ripped with GIS | `gis` | 1.0 | no |
| An edited terrain is rebuilt (sculpt → *Re-run erosion*) | `edited` | 1.0 | no |
| A map is exported (editor or *File → Export now*) | `exported` | **2.0** | yes, if it came from the director |

Exported maps count double: exporting is the strongest signal that a map was *good*.
Duplicate maps (same content hash) are never recorded twice.
Learning can be switched off in the Brain tab ("Learn from my maps") or with `--no-brain` on the CLI.

## 2. Where is the data?

Everything lives in the per-user data folder:

| OS | Folder |
|---|---|
| Windows | `%APPDATA%\ZeraLands\brain\` |
| macOS | `~/Library/Application Support/ZeraLands/brain/` |
| Linux | `$XDG_DATA_HOME/zeralands/brain/` or `~/.local/share/zeralands/brain/` |

Files:

* **`samples.jsonl`** — the dataset. Append-only, one JSON object per line (a broken line never corrupts the rest).
* **`model.bin`** — the trained weights + Adam optimizer state + loss history (binary, see §6).
* **`stats.json`** — human-readable summary (sample count, training steps, current losses, gene names).

Back up or share the folder to keep/transfer what the brain has learned. Deleting `model.bin` makes it retrain from
the dataset; deleting `samples.jsonl` starts the dataset over.

### 2.1 Sample format (`samples.jsonl`)

```json
{
  "id": "9f3c2a71d0e4b812",          // content hash (dedupe)
  "time": 1791400000,                 // unix seconds
  "source": "generated",              // generated | imported | gis | edited | exported
  "prompt": "huge jagged peaks with a few lakes",
  "environment": "mountains_alpine",  // environment id (see zeralands_cli --list)
  "era": "medieval",                  // era id
  "hasGenes": true,                   // false for imported / GIS maps (genes unknown)
  "weight": 1.0,
  "genes":   [0.42, 0.51, ...],       // 33 normalized recipe genes, order in stats.json "genes"
  "metrics": [0.61, 0.43, 0, 0.12, 0.7], // relief, slope, water, flat, interest (all 0..1)
  "thumbRes": 64,
  "thumb": "base64..."                // 64x64 heightmap, uint16 little-endian, min/max normalized
}
```

**Genes** are the terrain recipe the AI director finally chose (after its own search), normalized to 0..1:
`baseFreq, gain, wFbm, wRidged, wBillow, wEroded, warp, relief, heightRange (log), seaLevel, islandMask, ranges,
rangeStrength, basins, terraces, terraceStrength, mesas, dunes, craters, volcanoes, lavaFill, canyons, karst,
spikes, glacial, flatten, plateau, rivers, hydraulic, thermal, crystals, moisture, temperature`.

**Metrics** describe the *finished* terrain (after erosion), so they exist for imported and GIS maps too.

## 3. What does it learn? (models)

Inputs are a **condition vector**: the prompt encoded as a 256-dimensional signed feature-hash of words and word
pairs (bag of words + bigrams, plural stemming, stop-words removed, L2-normalized) concatenated with one-hot
environment and era.

| Model | Architecture | Learns | Loss |
|---|---|---|---|
| **Intent net** | cond → 96 tanh → 64 tanh → 38 sigmoid | which recipe genes and terrain metrics a prompt + environment + era leads to | MSE (genes masked when unknown) |
| **Shape autoencoder** | 32×32 heights → 128 leaky-ReLU → **32-d latent** (tanh) → 128 leaky-ReLU → 32×32 sigmoid | a compact "vocabulary" of landform shapes from *every* map, including real-world GIS rips | MSE, with random flips/rotations (8 dihedral augmentations) |
| **Text→shape prior** | cond → 64 tanh → 32 tanh | which region of the shape latent space a prompt points to | MSE to the (un-augmented) latent of the map |

About 333k parameters in total. Optimizer: Adam (β1 0.9, β2 0.999), gradient clipping ±5, small weight decay.

Decoding the prior's latent with the autoencoder's decoder gives a 32×32 **sketch** of what ZeraBrain imagines for a
prompt — that is the seed of future prompt-to-map generation.

## 4. How does it train?

* **Online / continual learning**: after each recorded map it runs 12 Adam steps on mini-batches of 16 made of the
  new map **plus random replays of older maps** (experience replay), so it keeps learning without forgetting.
* **Full passes**: Brain tab → *Train 25 epochs on everything*, or `zeralands_cli --brain-train N`.
* Losses are tracked as exponential moving averages and plotted in the Brain tab.

```
zeralands_cli --brain-stats          # samples, steps, parameters, losses
zeralands_cli --brain-train 50       # 50 epochs over the whole dataset
```

Batch generation is a quick way to grow the dataset:

```bash
for env in mountains_alpine desert_erg fjords karst savanna; do
  for v in 0 1 2 3; do
    zeralands_cli --env $env --era present --seed 7 --variation $v --res 257 --no-foliage --out /tmp/zl
  done
done
zeralands_cli --brain-train 40
```

## 5. Reading the numbers

* **intent loss** — mean squared error per predicted gene/metric. Below ~0.01 means it predicts the director's
  choices well for prompts it has seen.
* **shape loss** — per-pixel MSE of the autoencoder on 0..1 heights. ~0.01 = recognisable reconstructions.
* **text→shape loss** — how well prompts predict the latent of their maps.
* **Novelty** (Brain tab) — reconstruction error of the current map. High = unlike anything it has seen yet.

With few maps the numbers are noisy; they become meaningful after a few dozen maps across several environments.

## 6. `model.bin` layout (v1)

```
char[8]  "ZLBRAIN1"
int64    training steps
MLP x4   intent, encoder, decoder, prior — each:
           int32 layerCount, int32 adamStep,
           per layer: int32 in, int32 out, int32 activation,
                      float32 W[out*in], b[out], mW, vW, mb, vb   (Adam moments)
int32    history length N, float32 shapeLoss[N], float32 intentLoss[N]
float32  current intent, shape, latent loss
```

If the architecture changes in a future version, ZeraLands detects the mismatch, keeps the dataset and simply
retrains from it — learned data is never thrown away.

## 7. Roadmap to prompt-to-map

1. **Now**: collect data from every map; learn intent → genes and a shape latent space (this document).
2. **Next**: let the director seed its candidate population from `suggestGenes()` (the intent net) so prompts it has
   learned steer the search; use the prior's sketch as an extra uplift field in landscape evolution.
3. **Later**: export `samples.jsonl` + full-resolution exports to train a larger model (e.g. a latent diffusion model
   conditioned on text + environment + era) offline, then ship its weights for in-app generation.

Because samples store the prompt, environment, era, director genes, terrain metrics and a normalized height
thumbnail, the dataset is directly usable for all three stages.
