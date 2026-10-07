#include "core/Brain.h"

#include "core/ImageIO.h"
#include "core/Project.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numeric>

namespace zl {

namespace fs = std::filesystem;
using json = nlohmann::json;

// ================================================================ MLP
void Dense::init(int i, int o, Act a, Rng& rng) {
    in = i;
    out = o;
    act = a;
    float scale = (a == Act::LeakyRelu) ? std::sqrt(2.f / float(i)) : std::sqrt(1.f / float(i));
    W.resize(size_t(i) * o);
    for (auto& w : W) w = rng.normal() * scale;
    b.assign(size_t(o), 0.f);
    gW.assign(W.size(), 0.f);
    gb.assign(b.size(), 0.f);
    mW.assign(W.size(), 0.f);
    vW.assign(W.size(), 0.f);
    mb.assign(b.size(), 0.f);
    vb.assign(b.size(), 0.f);
}

void MLP::build(const std::vector<int>& sizes, const std::vector<Act>& acts, uint64_t seed) {
    Rng rng(seed);
    layers_.clear();
    for (size_t l = 0; l + 1 < sizes.size(); ++l) {
        Dense d;
        d.init(sizes[l], sizes[l + 1], acts[l], rng);
        layers_.push_back(std::move(d));
    }
    step_ = 0;
}

static inline float activate(Act a, float x) {
    switch (a) {
        case Act::Tanh: return std::tanh(x);
        case Act::LeakyRelu: return x > 0 ? x : 0.02f * x;
        case Act::Sigmoid: return 1.f / (1.f + std::exp(-x));
        default: return x;
    }
}
static inline float activateGrad(Act a, float y) {   // derivative expressed via the output y
    switch (a) {
        case Act::Tanh: return 1.f - y * y;
        case Act::LeakyRelu: return y > 0 ? 1.f : 0.02f;
        case Act::Sigmoid: return y * (1.f - y);
        default: return 1.f;
    }
}

const std::vector<float>& MLP::forward(const std::vector<float>& x) {
    acts_.resize(layers_.size() + 1);
    acts_[0] = x;
    for (size_t l = 0; l < layers_.size(); ++l) {
        const Dense& d = layers_[l];
        const std::vector<float>& in = acts_[l];
        std::vector<float>& out = acts_[l + 1];
        out.assign(size_t(d.out), 0.f);
        for (int o = 0; o < d.out; ++o) {
            const float* w = &d.W[size_t(o) * d.in];
            float s = d.b[size_t(o)];
            for (int i = 0; i < d.in; ++i) s += w[i] * in[size_t(i)];
            out[size_t(o)] = activate(d.act, s);
        }
    }
    return acts_.back();
}

std::vector<float> MLP::backward(const std::vector<float>& dy) {
    std::vector<float> grad = dy;
    for (size_t l = layers_.size(); l-- > 0;) {
        Dense& d = layers_[l];
        const std::vector<float>& in = acts_[l];
        const std::vector<float>& out = acts_[l + 1];
        std::vector<float> dIn(size_t(d.in), 0.f);
        for (int o = 0; o < d.out; ++o) {
            float g = grad[size_t(o)] * activateGrad(d.act, out[size_t(o)]);
            if (g == 0.f) continue;
            d.gb[size_t(o)] += g;
            float* gw = &d.gW[size_t(o) * d.in];
            const float* w = &d.W[size_t(o) * d.in];
            for (int i = 0; i < d.in; ++i) {
                gw[i] += g * in[size_t(i)];
                dIn[size_t(i)] += g * w[i];
            }
        }
        grad.swap(dIn);
    }
    return grad;
}

void MLP::zeroGrad() {
    for (auto& d : layers_) {
        std::fill(d.gW.begin(), d.gW.end(), 0.f);
        std::fill(d.gb.begin(), d.gb.end(), 0.f);
    }
}

void MLP::adamStep(float lr, int batch, float wd) {
    ++step_;
    const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
    const float c1 = 1.f - std::pow(b1, float(step_)), c2 = 1.f - std::pow(b2, float(step_));
    const float inv = 1.f / float(std::max(1, batch));
    auto upd = [&](std::vector<float>& p, std::vector<float>& g, std::vector<float>& m, std::vector<float>& v, float decay) {
        for (size_t i = 0; i < p.size(); ++i) {
            float gi = clampf(g[i] * inv, -5.f, 5.f) + decay * p[i];
            m[i] = b1 * m[i] + (1 - b1) * gi;
            v[i] = b2 * v[i] + (1 - b2) * gi * gi;
            p[i] -= lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps);
        }
    };
    for (auto& d : layers_) {
        upd(d.W, d.gW, d.mW, d.vW, wd);
        upd(d.b, d.gb, d.mb, d.vb, 0.f);
    }
    zeroGrad();
}

size_t MLP::parameterCount() const {
    size_t n = 0;
    for (auto& d : layers_) n += d.W.size() + d.b.size();
    return n;
}

void MLP::write(std::ostream& o) const {
    int32_t n = int32_t(layers_.size());
    o.write(reinterpret_cast<const char*>(&n), 4);
    o.write(reinterpret_cast<const char*>(&step_), 4);
    for (const auto& d : layers_) {
        int32_t hdr[3] = {d.in, d.out, int32_t(d.act)};
        o.write(reinterpret_cast<const char*>(hdr), 12);
        for (const auto* v : {&d.W, &d.b, &d.mW, &d.vW, &d.mb, &d.vb}) o.write(reinterpret_cast<const char*>(v->data()), std::streamsize(v->size() * 4));
    }
}

bool MLP::read(std::istream& i) {
    int32_t n = 0, st = 0;
    i.read(reinterpret_cast<char*>(&n), 4);
    i.read(reinterpret_cast<char*>(&st), 4);
    if (!i || n != int32_t(layers_.size())) return false;
    std::vector<Dense> tmp = layers_;
    for (auto& d : tmp) {
        int32_t hdr[3];
        i.read(reinterpret_cast<char*>(hdr), 12);
        if (!i || hdr[0] != d.in || hdr[1] != d.out) return false;   // architecture changed -> start fresh
        for (auto* v : {&d.W, &d.b, &d.mW, &d.vW, &d.mb, &d.vb}) i.read(reinterpret_cast<char*>(v->data()), std::streamsize(v->size() * 4));
    }
    if (!i) return false;
    layers_ = std::move(tmp);
    step_ = st;
    return true;
}

// ================================================================ features
namespace {

struct GeneDef {
    const char* name;
    float TerrainRecipe::*field;
    float lo, hi;
    bool logScale;
};

const std::vector<GeneDef>& geneDefs() {
    static const std::vector<GeneDef> g = {
        {"baseFreq", &TerrainRecipe::baseFreq, 0.5f, 9.f, true},       {"gain", &TerrainRecipe::gain, 0.3f, 0.7f, false},
        {"wFbm", &TerrainRecipe::wFbm, 0.f, 1.f, false},               {"wRidged", &TerrainRecipe::wRidged, 0.f, 1.f, false},
        {"wBillow", &TerrainRecipe::wBillow, 0.f, 1.f, false},         {"wEroded", &TerrainRecipe::wEroded, 0.f, 1.f, false},
        {"warp", &TerrainRecipe::warp, 0.f, 1.5f, false},              {"relief", &TerrainRecipe::relief, 0.1f, 1.2f, false},
        {"heightRange", &TerrainRecipe::heightRange, 20.f, 6000.f, true}, {"seaLevel", &TerrainRecipe::seaLevel, -1.f, 1.f, false},
        {"islandMask", &TerrainRecipe::islandMask, 0.f, 1.f, false},   {"ranges", &TerrainRecipe::ranges, 0.f, 5.f, false},
        {"rangeStrength", &TerrainRecipe::rangeStrength, 0.f, 1.f, false}, {"basins", &TerrainRecipe::basins, 0.f, 12.f, false},
        {"terraces", &TerrainRecipe::terraces, 0.f, 20.f, false},      {"terraceStrength", &TerrainRecipe::terraceStrength, 0.f, 1.f, false},
        {"mesas", &TerrainRecipe::mesas, 0.f, 1.f, false},             {"dunes", &TerrainRecipe::dunes, 0.f, 1.5f, false},
        {"craters", &TerrainRecipe::craters, 0.f, 40.f, false},        {"volcanoes", &TerrainRecipe::volcanoes, 0.f, 5.f, false},
        {"lavaFill", &TerrainRecipe::lavaFill, 0.f, 1.f, false},       {"canyons", &TerrainRecipe::canyons, 0.f, 1.5f, false},
        {"karst", &TerrainRecipe::karst, 0.f, 1.f, false},             {"spikes", &TerrainRecipe::spikes, 0.f, 1.f, false},
        {"glacial", &TerrainRecipe::glacial, 0.f, 1.5f, false},        {"flatten", &TerrainRecipe::flatten, 0.f, 1.f, false},
        {"plateau", &TerrainRecipe::plateau, 0.f, 1.f, false},         {"rivers", &TerrainRecipe::rivers, 0.f, 2.f, false},
        {"hydraulic", &TerrainRecipe::hydraulic, 0.f, 2.f, false},     {"thermal", &TerrainRecipe::thermal, 0.f, 2.f, false},
        {"crystals", &TerrainRecipe::crystals, 0.f, 1.f, false},       {"moisture", &TerrainRecipe::moisture, 0.f, 1.f, false},
        {"temperature", &TerrainRecipe::temperature, 0.f, 1.f, false},
    };
    return g;
}
constexpr int kMetricCount = 5;

std::vector<float> genesOf(const TerrainRecipe& r) {
    std::vector<float> v;
    for (const auto& g : geneDefs()) {
        float x = r.*(g.field);
        float t = g.logScale ? (std::log(std::max(g.lo, x)) - std::log(g.lo)) / (std::log(g.hi) - std::log(g.lo)) : (x - g.lo) / (g.hi - g.lo);
        v.push_back(clamp01(t));
    }
    return v;
}

std::vector<float> metricsOf(const TerrainMetrics& m) {
    return {clamp01(m.relief / 1.2f), clamp01(m.slope / 2.f), clamp01(m.water), clamp01(m.flat), clamp01(m.interest)};
}

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string b64encode(const uint8_t* d, size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = uint32_t(d[i]) << 16 | (i + 1 < n ? uint32_t(d[i + 1]) << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < n ? kB64[v & 63] : '=';
    }
    return out;
}
std::vector<uint8_t> b64decode(const std::string& s) {
    std::vector<uint8_t> out;
    uint32_t v = 0;
    int bits = 0;
    for (char c : s) {
        const char* p = std::strchr(kB64, c);
        if (!p || c == '=') continue;
        v = (v << 6) | uint32_t(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((v >> bits) & 0xFF));
        }
    }
    return out;
}

std::vector<float> height32(const std::vector<uint16_t>& thumb, int aug) {
    // 64 -> 32 box filter + one of 8 dihedral transforms (terrain has no preferred orientation)
    const int R = kBrainHeightRes, S = kBrainStoreRes;
    std::vector<float> out(size_t(R) * R);
    for (int y = 0; y < R; ++y)
        for (int x = 0; x < R; ++x) {
            int sx = x, sy = y;
            if (aug & 1) sx = R - 1 - sx;
            if (aug & 2) sy = R - 1 - sy;
            if (aug & 4) std::swap(sx, sy);
            float s = 0;
            for (int k = 0; k < 4; ++k) s += float(thumb[size_t(sy * 2 + (k >> 1)) * S + size_t(sx * 2 + (k & 1))]);
            out[size_t(y) * R + x] = s / (4.f * 65535.f);
        }
    return out;
}

}  // namespace

const std::vector<std::string>& Brain::geneNames() {
    static std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const auto& g : geneDefs()) n.push_back(g.name);
        for (const char* m : {"metric_relief", "metric_slope", "metric_water", "metric_flat", "metric_interest"}) n.push_back(m);
        return n;
    }();
    return names;
}

std::vector<float> Brain::encodeText(const std::string& prompt) {
    static const char* stop[] = {"a", "an", "the", "with", "and", "of", "in", "on", "to", "some", "lots", "very", "for", "by", "is"};
    std::vector<std::string> toks;
    std::string cur;
    for (char c : prompt + " ") {
        if (std::isalnum(static_cast<unsigned char>(c))) cur += char(std::tolower(static_cast<unsigned char>(c)));
        else if (!cur.empty()) {
            bool isStop = false;
            for (const char* s : stop) isStop = isStop || cur == s;
            if (!isStop) toks.push_back(cur);
            cur.clear();
        }
    }
    std::vector<float> v(kBrainTextDim, 0.f);
    for (size_t i = 0; i < toks.size(); ++i) {
        std::string t = toks[i];
        if (t.size() > 3 && t.back() == 's') t.pop_back();   // crude stemming: plurals
        uint64_t h = hashString(t);
        v[h % kBrainTextDim] += (h & 0x100) ? 1.f : -1.f;   // signed feature hashing
        if (i + 1 < toks.size()) {
            uint64_t hb = hashString(t + "_" + toks[i + 1]);
            v[hb % kBrainTextDim] += (hb & 0x100) ? 0.5f : -0.5f;
        }
    }
    float n = 0;
    for (float x : v) n += x * x;
    n = std::sqrt(n);
    if (n > 0) for (auto& x : v) x /= n;
    return v;
}

std::vector<float> Brain::conditionVector(const std::string& prompt, int env, int era) const {
    std::vector<float> v = encodeText(prompt);
    const int nEnv = int(environments().size()), nEra = int(eras().size());
    for (int i = 0; i < nEnv; ++i) v.push_back(i == env ? 1.f : 0.f);
    for (int i = 0; i < nEra; ++i) v.push_back(i == era ? 1.f : 0.f);
    return v;
}

// ================================================================ brain
bool Brain::open(const std::string& dir) {
    std::lock_guard<std::mutex> lk(mu_);
    dir_ = dir.empty() ? (fs::u8path(userDataDir()) / "brain").u8string() : dir;
    std::error_code ec;
    fs::create_directories(fs::u8path(dir_), ec);
    const int cond = kBrainTextDim + int(environments().size()) + int(eras().size());
    const int genes = int(geneDefs().size()) + kMetricCount;
    const int hin = kBrainHeightRes * kBrainHeightRes;
    intent_.build({cond, 96, 64, genes}, {Act::Tanh, Act::Tanh, Act::Sigmoid}, 11);
    encoder_.build({hin, 128, 32}, {Act::LeakyRelu, Act::Tanh}, 12);
    decoder_.build({32, 128, hin}, {Act::LeakyRelu, Act::Sigmoid}, 13);
    prior_.build({cond, 64, 32}, {Act::Tanh, Act::Tanh}, 14);
    steps_ = 0;
    histShape_.clear();
    histIntent_.clear();
    std::ifstream f(fs::u8path(dir_) / "model.bin", std::ios::binary);
    if (f) {
        char magic[8];
        f.read(magic, 8);
        if (f && std::memcmp(magic, "ZLBRAIN1", 8) == 0) {
            f.read(reinterpret_cast<char*>(&steps_), 8);
            bool ok = intent_.read(f) && encoder_.read(f) && decoder_.read(f) && prior_.read(f);
            if (!ok) {
                // architecture changed: keep the data, retrain from scratch
                intent_.build({cond, 96, 64, genes}, {Act::Tanh, Act::Tanh, Act::Sigmoid}, 11);
                encoder_.build({hin, 128, 32}, {Act::LeakyRelu, Act::Tanh}, 12);
                decoder_.build({32, 128, hin}, {Act::LeakyRelu, Act::Sigmoid}, 13);
                prior_.build({cond, 64, 32}, {Act::Tanh, Act::Tanh}, 14);
                steps_ = 0;
            } else {
                int32_t nh = 0;
                f.read(reinterpret_cast<char*>(&nh), 4);
                if (f && nh >= 0 && nh < 100000) {
                    histShape_.resize(size_t(nh));
                    histIntent_.resize(size_t(nh));
                    f.read(reinterpret_cast<char*>(histShape_.data()), nh * 4);
                    f.read(reinterpret_cast<char*>(histIntent_.data()), nh * 4);
                    if (!f) { histShape_.clear(); histIntent_.clear(); }
                }
            }
        }
    }
    loadDataset();
    open_ = true;
    return true;
}

void Brain::loadDataset() {
    data_.clear();
    std::ifstream f(fs::u8path(dir_) / "samples.jsonl");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        try {
            json j = json::parse(line);
            BrainSample s;
            s.id = j.value("id", std::string());
            s.source = j.value("source", std::string("generated"));
            s.prompt = j.value("prompt", std::string());
            s.envIndex = std::max(0, findEnvironment(j.value("environment", std::string())));
            s.eraIndex = std::max(0, findEra(j.value("era", std::string())));
            s.hasGenes = j.value("hasGenes", false);
            s.weight = j.value("weight", 1.f);
            if (j.contains("genes")) s.genes = j["genes"].get<std::vector<float>>();
            if (j.contains("metrics")) s.metrics = j["metrics"].get<std::vector<float>>();
            auto bytes = b64decode(j.value("thumb", std::string()));
            if (bytes.size() != size_t(kBrainStoreRes) * kBrainStoreRes * 2) continue;
            s.thumb.resize(bytes.size() / 2);
            for (size_t i = 0; i < s.thumb.size(); ++i) s.thumb[i] = uint16_t(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
            if (s.genes.size() != geneDefs().size()) s.hasGenes = false;
            if (s.metrics.size() != size_t(kMetricCount)) s.metrics.assign(kMetricCount, 0.f);
            data_.push_back(std::move(s));
        } catch (...) {
            // skip malformed lines; the file is append-only so one bad write never loses the rest
        }
    }
}

bool Brain::appendSample(const BrainSample& s) const {
    std::vector<uint8_t> bytes(s.thumb.size() * 2);
    for (size_t i = 0; i < s.thumb.size(); ++i) {
        bytes[i * 2] = uint8_t(s.thumb[i] & 0xFF);
        bytes[i * 2 + 1] = uint8_t(s.thumb[i] >> 8);
    }
    json j;
    j["id"] = s.id;
    j["time"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    j["source"] = s.source;
    j["prompt"] = s.prompt;
    j["environment"] = environments()[size_t(s.envIndex)].id;
    j["era"] = eras()[size_t(s.eraIndex)].id;
    j["hasGenes"] = s.hasGenes;
    j["weight"] = s.weight;
    j["genes"] = s.genes;
    j["metrics"] = s.metrics;
    j["thumbRes"] = kBrainStoreRes;
    j["thumb"] = b64encode(bytes.data(), bytes.size());
    std::ofstream f(fs::u8path(dir_) / "samples.jsonl", std::ios::app);
    f << j.dump() << '\n';
    return bool(f);
}

bool Brain::save() const {
    fs::path tmp = fs::u8path(dir_) / "model.bin.tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        f.write("ZLBRAIN1", 8);
        f.write(reinterpret_cast<const char*>(&steps_), 8);
        intent_.write(f);
        encoder_.write(f);
        decoder_.write(f);
        prior_.write(f);
        int32_t nh = int32_t(histShape_.size());
        f.write(reinterpret_cast<const char*>(&nh), 4);
        f.write(reinterpret_cast<const char*>(histShape_.data()), nh * 4);
        f.write(reinterpret_cast<const char*>(histIntent_.data()), nh * 4);
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, fs::u8path(dir_) / "model.bin", ec);
    json st{{"samples", data_.size()}, {"trainSteps", steps_}, {"intentLoss", lossIntent_}, {"shapeLoss", lossShape_}, {"latentLoss", lossLatent_},
            {"genes", geneNames()}};
    std::ofstream sf(fs::u8path(dir_) / "stats.json");
    sf << st.dump(2);
    return true;
}

void Brain::trainStep(const std::vector<int>& batch) {
    if (batch.empty()) return;
    float li = 0, ls = 0, ll = 0;
    const int nG = int(geneDefs().size());
    for (int idx : batch) {
        const BrainSample& s = data_[size_t(idx)];
        const float w = s.weight;
        std::vector<float> cond = conditionVector(s.prompt, s.envIndex, s.eraIndex);
        // 1) intent: condition -> genes + metrics (genes masked for imported / GIS maps)
        {
            const auto& y = intent_.forward(cond);
            std::vector<float> dy(y.size(), 0.f);
            int n = 0;
            for (size_t k = 0; k < y.size(); ++k) {
                bool isGene = int(k) < nG;
                if (isGene && !s.hasGenes) continue;
                float target = isGene ? s.genes[k] : s.metrics[k - size_t(nG)];
                float e = y[k] - target;
                dy[k] = 2.f * e * w;
                li += e * e;
                ++n;
            }
            if (n) {
                for (auto& d : dy) d /= float(n);
                intent_.backward(dy);
            }
        }
        // canonical latent (forward only, no gradient) is the target for the text->shape prior
        std::vector<float> zc = encoder_.forward(height32(s.thumb, 0));
        // 2) shape autoencoder with dihedral augmentation
        std::vector<float> x = height32(s.thumb, int(rng_.next() & 7));
        std::vector<float> z = encoder_.forward(x);
        const auto& rec = decoder_.forward(z);
        std::vector<float> dr(rec.size());
        for (size_t k = 0; k < rec.size(); ++k) {
            float e = rec[k] - x[k];
            ls += e * e / float(rec.size());
            dr[k] = 2.f * e * w / float(rec.size());
        }
        std::vector<float> dz = decoder_.backward(dr);
        encoder_.backward(dz);   // encoder activations are still those of forward(x)
        // 3) text -> shape prior
        const auto& zp = prior_.forward(cond);
        std::vector<float> dp(zp.size());
        for (size_t k = 0; k < zp.size(); ++k) {
            float e = zp[k] - zc[k];
            ll += e * e / float(zp.size());
            dp[k] = 2.f * e * w / float(zp.size());
        }
        prior_.backward(dp);
    }
    const int B = int(batch.size());
    intent_.adamStep(2e-3f, B);
    encoder_.adamStep(1e-3f, B);
    decoder_.adamStep(1e-3f, B);
    prior_.adamStep(1.5e-3f, B);
    ++steps_;
    auto ema = [](float& acc, float v, long long step) { acc = step <= 1 ? v : acc * 0.9f + v * 0.1f; };
    ema(lossIntent_, li / float(B), steps_);
    ema(lossShape_, ls / float(B), steps_);
    ema(lossLatent_, ll / float(B), steps_);
    if (steps_ % 10 == 0) {
        histShape_.push_back(lossShape_);
        histIntent_.push_back(lossIntent_);
        if (histShape_.size() > 600) {
            histShape_.erase(histShape_.begin());
            histIntent_.erase(histIntent_.begin());
        }
    }
}

bool Brain::record(const Scene& sc, const std::string& source, float weight) {
    if (!sc.valid()) return false;
    std::lock_guard<std::mutex> lk(mu_);
    if (!open_) return false;
    BrainSample s;
    s.source = source;
    s.prompt = sc.settings.prompt;
    s.envIndex = std::clamp(sc.settings.envIndex, 0, int(environments().size()) - 1);
    s.eraIndex = std::clamp(sc.settings.eraIndex, 0, int(eras().size()) - 1);
    s.hasGenes = source == "generated" || (source == "exported" && !sc.report.summary.empty() && sc.genome.r.baseFreq > 0 &&
                                             sc.report.summary.rfind("Picked", 0) == 0);
    s.genes = genesOf(sc.genome.r);
    TerrainMetrics m = measure(sc.terrain.height, sc.genome.r, sc.terrain.worldSize);
    s.metrics = metricsOf(m);
    s.weight = weight;
    Grid small = sc.terrain.height.resampled(kBrainStoreRes, kBrainStoreRes);
    small.normalize(0.f, 1.f);
    s.thumb = toU16(small);
    uint64_t h = hashCombine(uint64_t(s.envIndex) * 131 + uint64_t(s.eraIndex), hashString(s.source == "exported" ? "x" : "g"));
    for (size_t i = 0; i < s.thumb.size(); i += 7) h = hashCombine(h, s.thumb[i]);
    char id[20];
    std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(h));
    s.id = id;
    for (const auto& d : data_)
        if (d.id == s.id) return false;
    appendSample(s);
    data_.push_back(std::move(s));

    // online continual learning: newest sample + replay of older maps
    const int n = int(data_.size());
    for (int step = 0; step < 12; ++step) {
        std::vector<int> batch{n - 1};
        for (int k = 0; k < std::min(15, n - 1); ++k) batch.push_back(int(rng_.next() % uint64_t(n)));
        trainStep(batch);
    }
    save();
    return true;
}

void Brain::train(int epochs, Progress* progress) {
    std::lock_guard<std::mutex> lk(mu_);
    if (data_.empty()) return;
    std::vector<int> order(data_.size());
    std::iota(order.begin(), order.end(), 0);
    for (int e = 0; e < epochs; ++e) {
        for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[size_t(rng_.next() % i)]);
        for (size_t b = 0; b < order.size(); b += 16) {
            std::vector<int> batch(order.begin() + long(b), order.begin() + long(std::min(order.size(), b + 16)));
            trainStep(batch);
        }
        if (progress) {
            if (progress->cancel) break;
            progress->set("Training ZeraBrain", float(e + 1) / float(epochs));
        }
    }
    save();
}

BrainStats Brain::stats() const {
    std::lock_guard<std::mutex> lk(mu_);
    BrainStats s;
    s.samples = int(data_.size());
    s.trainSteps = steps_;
    s.intentLoss = lossIntent_;
    s.shapeLoss = lossShape_;
    s.latentLoss = lossLatent_;
    s.shapeLossHistory = histShape_;
    s.intentLossHistory = histIntent_;
    s.parameters = intent_.parameterCount() + encoder_.parameterCount() + decoder_.parameterCount() + prior_.parameterCount();
    s.dir = dir_;
    return s;
}

std::vector<float> Brain::suggestGenes(const std::string& prompt, int env, int era) {
    std::lock_guard<std::mutex> lk(mu_);
    return intent_.forward(conditionVector(prompt, env, era));
}

Grid Brain::sketch(const std::string& prompt, int env, int era) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto& z = prior_.forward(conditionVector(prompt, env, era));
    std::vector<float> zz = z;
    const auto& img = decoder_.forward(zz);
    Grid g(kBrainHeightRes, kBrainHeightRes);
    for (size_t i = 0; i < img.size(); ++i) g.vec()[i] = img[i];
    return g;
}

float Brain::reconstructionError(const Scene& sc) {
    if (!sc.valid()) return 0.f;
    std::lock_guard<std::mutex> lk(mu_);
    Grid small = sc.terrain.height.resampled(kBrainStoreRes, kBrainStoreRes);
    small.normalize(0.f, 1.f);
    auto thumb = toU16(small);
    auto x = height32(thumb, 0);
    std::vector<float> z = encoder_.forward(x);
    const auto& r = decoder_.forward(z);
    float e = 0;
    for (size_t i = 0; i < r.size(); ++i) e += (r[i] - x[i]) * (r[i] - x[i]);
    return e / float(r.size());
}

}  // namespace zl
