// ZeraLands - ZeraBrain: a small, dependency-free, continually-trained ML system that learns from every map
// made in ZeraLands. It is the data + model foundation for future prompt-to-map generation. See docs/ML.md.
#pragma once

#include "core/Generator.h"

#include <mutex>
#include <string>
#include <vector>

namespace zl {

// ---------------------------------------------------------------- tiny neural network
enum class Act : uint8_t { Linear, Tanh, LeakyRelu, Sigmoid };

struct Dense {
    int in = 0, out = 0;
    Act act = Act::Linear;
    std::vector<float> W, b;            // W[out][in]
    std::vector<float> gW, gb;          // accumulated gradients
    std::vector<float> mW, vW, mb, vb;  // Adam moments
    void init(int i, int o, Act a, Rng& rng);
};

class MLP {
public:
    void build(const std::vector<int>& sizes, const std::vector<Act>& acts, uint64_t seed);
    // Forward pass; keeps activations for backprop.
    const std::vector<float>& forward(const std::vector<float>& x);
    // Backprop dL/dy (of the last forward pass); accumulates gradients. Returns dL/dx.
    std::vector<float> backward(const std::vector<float>& dy);
    void zeroGrad();
    void adamStep(float lr, int batch, float weightDecay = 1e-5f);
    int inputSize() const { return layers_.empty() ? 0 : layers_.front().in; }
    int outputSize() const { return layers_.empty() ? 0 : layers_.back().out; }
    size_t parameterCount() const;
    void write(std::ostream& o) const;
    bool read(std::istream& i);

private:
    std::vector<Dense> layers_;
    std::vector<std::vector<float>> acts_;   // acts_[0] = input
    int step_ = 0;
};

// ---------------------------------------------------------------- dataset
constexpr int kBrainHeightRes = 32;     // AE input resolution (32x32)
constexpr int kBrainStoreRes = 64;      // stored thumbnail resolution (64x64, uint16) for future models
constexpr int kBrainTextDim = 256;      // hashed bag-of-words + bigrams

struct BrainSample {
    std::string id;           // content hash
    std::string source;       // generated | imported | gis | exported
    std::string prompt;
    int envIndex = 0, eraIndex = 0;
    bool hasGenes = false;
    std::vector<float> genes;     // normalized 0..1 recipe genes (see geneNames())
    std::vector<float> metrics;   // relief, slope, water, flat, interest (normalized)
    std::vector<uint16_t> thumb;  // kBrainStoreRes^2 heights
    float weight = 1.f;           // exported (user-approved) maps count more
};

struct BrainStats {
    int samples = 0;
    long long trainSteps = 0;
    float intentLoss = 0, shapeLoss = 0, latentLoss = 0;
    std::vector<float> shapeLossHistory, intentLossHistory;
    size_t parameters = 0;
    std::string dir;
};

class Brain {
public:
    // Opens (or creates) the brain in `dir` (default <userData>/brain). Loads dataset & weights.
    bool open(const std::string& dir = "");
    // Records a finished map and performs a few online training steps (replay batch).
    // Returns false if the exact map was already recorded.
    bool record(const Scene& scene, const std::string& source, float weight = 1.f);
    // Full passes over the dataset (CLI / "Train now").
    void train(int epochs, Progress* progress = nullptr);
    BrainStats stats() const;

    // Experimental read-outs of what it has learned (not used by the generator yet).
    std::vector<float> suggestGenes(const std::string& prompt, int envIndex, int eraIndex);
    Grid sketch(const std::string& prompt, int envIndex, int eraIndex);   // 32x32 decoded heightmap
    float reconstructionError(const Scene& scene);                         // novelty of a map

    static const std::vector<std::string>& geneNames();
    static std::vector<float> encodeText(const std::string& prompt);

private:
    std::vector<float> conditionVector(const std::string& prompt, int env, int era) const;
    void trainStep(const std::vector<int>& batch);
    bool save() const;
    bool appendSample(const BrainSample& s) const;
    void loadDataset();

    mutable std::mutex mu_;
    std::string dir_;
    std::vector<BrainSample> data_;
    MLP intent_;       // condition -> genes + metrics
    MLP encoder_;      // 32x32 height -> latent(32)
    MLP decoder_;      // latent(32) -> 32x32 height
    MLP prior_;        // condition -> latent (text-to-shape prior)
    long long steps_ = 0;
    float lossIntent_ = 0, lossShape_ = 0, lossLatent_ = 0;
    std::vector<float> histShape_, histIntent_;
    Rng rng_{0xB12A1Full};
    bool open_ = false;
};

}  // namespace zl
