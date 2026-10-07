#include "core/Director.h"

#include "core/Generator.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace zl {

uint64_t seedFromText(const std::string& text) {
    // Pure numbers map to themselves (so "42" behaves like a classic numeric seed); anything else is hashed.
    bool numeric = !text.empty() && text.size() < 19;
    for (char c : text) numeric = numeric && std::isdigit(static_cast<unsigned char>(c));
    if (numeric) return splitmix64(std::stoull(text));
    return hashString(text);
}

// ------------------------------------------------------------------ prompt interpretation
namespace {

struct Lexeme {
    const char* words;   // space separated synonyms
    const char* label;
    std::function<void(TerrainRecipe&, float)> apply;   // amount > 0 more, < 0 less
};

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            cur += char(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool matchesWord(const char* list, const std::string& tok) {
    std::istringstream ss(list);
    std::string w;
    while (ss >> w) {
        if (tok == w) return true;
        // tolerate plurals ("mountains", "lakes")
        if (tok.size() == w.size() + 1 && tok.back() == 's' && tok.compare(0, w.size(), w) == 0) return true;
    }
    return false;
}

const std::vector<Lexeme>& lexicon() {
    static const std::vector<Lexeme> lex = {
        {"mountain peak alpine summit tall high", "higher, more mountainous", [](TerrainRecipe& r, float a) {
             r.relief = clampf(r.relief * (1.f + 0.25f * a), 0.1f, 1.2f);
             r.ranges = std::max(0.f, r.ranges + a);
             r.rangeStrength = clampf(r.rangeStrength + 0.15f * a, 0.f, 1.f);
             r.heightRange *= (1.f + 0.3f * a);
             r.flatten = clampf(r.flatten - 0.2f * a, 0.f, 0.95f);
         }},
        {"flat plain plains gentle lowland level", "flatter", [](TerrainRecipe& r, float a) {
             r.flatten = clampf(r.flatten + 0.25f * a, 0.f, 0.95f);
             r.relief = clampf(r.relief * (1.f - 0.2f * a), 0.1f, 1.2f);
         }},
        {"river stream creek brook waterway", "more rivers", [](TerrainRecipe& r, float a) {
             r.rivers = clampf(r.rivers + 0.4f * a, 0.f, 2.f);
             r.moisture = clampf(r.moisture + 0.1f * a, 0.f, 1.f);
         }},
        {"lake pond tarn lagoon loch", "more lakes", [](TerrainRecipe& r, float a) {
             r.basins = std::max(0.f, r.basins + 1.5f * a);
             r.moisture = clampf(r.moisture + 0.1f * a, 0.f, 1.f);
         }},
        {"island archipelago atoll isle", "islands", [](TerrainRecipe& r, float a) {
             r.islandMask = clampf(r.islandMask + 0.4f * a, 0.f, 1.f);
             if (r.seaLevel < 0 && a > 0) r.seaLevel = 0.3f;
         }},
        {"coast coastal ocean sea shore beach bay", "coastline", [](TerrainRecipe& r, float a) {
             if (a > 0) r.seaLevel = std::max(r.seaLevel, 0.25f);
             else r.seaLevel = -1.f;
             r.islandMask = clampf(r.islandMask + 0.15f * a, 0.f, 1.f);
         }},
        {"canyon gorge ravine chasm", "canyons", [](TerrainRecipe& r, float a) { r.canyons = clampf(r.canyons + 0.45f * a, 0.f, 1.5f); }},
        {"volcano volcanic caldera", "volcanoes", [](TerrainRecipe& r, float a) {
             r.volcanoes = std::max(0.f, r.volcanoes + a);
             if (a > 0) r.lavaFill = std::max(r.lavaFill, 0.4f);
         }},
        {"lava magma molten", "lava", [](TerrainRecipe& r, float a) { r.lavaFill = clampf(r.lavaFill + 0.5f * a, 0.f, 1.f); }},
        {"crater impact meteor", "craters", [](TerrainRecipe& r, float a) { r.craters = std::max(0.f, r.craters + 4.f * a); }},
        {"dune dunes sandy", "dunes", [](TerrainRecipe& r, float a) { r.dunes = clampf(r.dunes + 0.4f * a, 0.f, 1.5f); }},
        {"terrace terraced stepped layered strata", "terraced", [](TerrainRecipe& r, float a) {
             r.terraces = std::max(0.f, (r.terraces > 0 ? r.terraces : 6.f) + 2.f * a);
             r.terraceStrength = clampf(r.terraceStrength + 0.25f * a, 0.f, 1.f);
         }},
        {"eroded weathered old ancient worn", "more weathered", [](TerrainRecipe& r, float a) {
             r.hydraulic = clampf(r.hydraulic + 0.3f * a, 0.f, 2.f);
             r.thermal = clampf(r.thermal + 0.2f * a, 0.f, 2.f);
             r.wEroded = clampf(r.wEroded + 0.15f * a, 0.f, 1.f);
         }},
        {"jagged sharp rugged craggy young spiky", "more jagged", [](TerrainRecipe& r, float a) {
             r.wRidged = clampf(r.wRidged + 0.2f * a, 0.f, 1.f);
             r.gain = clampf(r.gain + 0.04f * a, 0.3f, 0.65f);
             r.talus = clampf(r.talus + 0.3f * a, 0.3f, 3.f);
         }},
        {"smooth rolling soft rounded gentle", "softer", [](TerrainRecipe& r, float a) {
             r.wBillow = clampf(r.wBillow + 0.15f * a, 0.f, 1.f);
             r.wRidged = clampf(r.wRidged - 0.15f * a, 0.f, 1.f);
             r.thermal = clampf(r.thermal + 0.2f * a, 0.f, 2.f);
         }},
        {"valley valleys glacier glacial fjord", "carved valleys", [](TerrainRecipe& r, float a) { r.glacial = clampf(r.glacial + 0.35f * a, 0.f, 1.5f); }},
        {"plateau mesa butte tableland", "mesas & plateaus", [](TerrainRecipe& r, float a) { r.mesas = clampf(r.mesas + 0.35f * a, 0.f, 1.f); }},
        {"spike spire needle pinnacle tower", "spires", [](TerrainRecipe& r, float a) {
             r.spikes = clampf(r.spikes + 0.3f * a, 0.f, 1.f);
             r.karst = clampf(r.karst + 0.2f * a, 0.f, 1.f);
         }},
        {"dry arid barren parched", "drier", [](TerrainRecipe& r, float a) { r.moisture = clampf(r.moisture - 0.2f * a, 0.f, 1.f); }},
        {"wet lush green fertile rainy", "lusher", [](TerrainRecipe& r, float a) { r.moisture = clampf(r.moisture + 0.2f * a, 0.f, 1.f); }},
        {"snow snowy cold frozen icy winter", "colder", [](TerrainRecipe& r, float a) { r.temperature = clampf(r.temperature - 0.25f * a, 0.f, 1.f); }},
        {"hot warm tropical scorching", "warmer", [](TerrainRecipe& r, float a) { r.temperature = clampf(r.temperature + 0.25f * a, 0.f, 1.f); }},
        {"huge massive giant big vast epic", "larger landforms", [](TerrainRecipe& r, float a) {
             r.baseFreq = clampf(r.baseFreq * (1.f - 0.2f * a), 0.6f, 8.f);
             r.heightRange *= (1.f + 0.15f * a);
         }},
        {"small detailed busy intricate tiny", "finer landforms", [](TerrainRecipe& r, float a) { r.baseFreq = clampf(r.baseFreq * (1.f + 0.25f * a), 0.6f, 8.f); }},
        {"crystal crystals gem", "crystals", [](TerrainRecipe& r, float a) { r.crystals = clampf(r.crystals + 0.4f * a, 0.f, 1.f); }},
        {"warped twisted alien surreal", "warped", [](TerrainRecipe& r, float a) { r.warp = clampf(r.warp + 0.25f * a, 0.f, 1.5f); }},
    };
    return lex;
}

}  // namespace

static void interpretPrompt(const std::string& prompt, TerrainRecipe& r, std::vector<std::string>* log) {
    auto toks = tokenize(prompt);
    float pendingScale = 1.f;
    int pendingLife = 0;
    for (size_t i = 0; i < toks.size(); ++i) {
        const std::string& t = toks[i];
        if (t == "very" || t == "extremely" || t == "super" || t == "lots" || t == "many" || t == "huge_amount" || t == "tons" || t == "really") {
            pendingScale = 1.8f; pendingLife = 3; continue;
        }
        if (t == "slightly" || t == "bit" || t == "few" || t == "some" || t == "little" || t == "subtle" || t == "mild") {
            pendingScale = 0.5f; pendingLife = 3; continue;
        }
        if (t == "no" || t == "without" || t == "less" || t == "fewer" || t == "not" || t == "zero" || t == "nothing") {
            pendingScale = -1.f; pendingLife = 3; continue;
        }
        for (const auto& lx : lexicon()) {
            if (matchesWord(lx.words, t)) {
                float amount = pendingLife > 0 ? pendingScale : 1.f;
                lx.apply(r, amount);
                if (log) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "\"%s\" -> %s%s", t.c_str(), amount < 0 ? "less: " : "", lx.label);
                    log->push_back(buf);
                }
                pendingLife = 0;
                break;
            }
        }
        if (pendingLife > 0) --pendingLife;
    }
}

TerrainRecipe composeRecipe(const GenSettings& s, std::vector<std::string>* intentLog) {
    const auto& envs = environments();
    const auto& era = eras()[std::clamp(s.eraIndex, 0, int(eras().size()) - 1)];
    TerrainRecipe r = envs[std::clamp(s.envIndex, 0, int(envs.size()) - 1)].recipe;

    r.heightRange *= s.heightScale;
    float rough = std::max(0.05f, s.roughness);
    r.gain = clampf(r.gain * std::pow(rough, 0.25f), 0.3f, 0.68f);
    r.wRidged *= std::pow(rough, 0.5f);
    r.hydraulic *= s.erosion;
    r.thermal *= s.erosion;
    r.rivers *= s.riverAmount;
    r.baseFreq *= s.featureScale;
    if (r.seaLevel >= 0.f) r.seaLevel = clampf(r.seaLevel + s.waterLevel, 0.02f, 0.9f);
    else if (s.waterLevel > 0.04f) r.seaLevel = s.waterLevel;

    // era influence on the land itself
    r.volcanoes += era.volcanism * 1.5f;
    if (era.volcanism > 0) r.lavaFill = std::max(r.lavaFill, era.volcanism * 0.7f);
    r.craters += era.craters;
    r.moisture = clamp01(r.moisture * era.moistureMul);

    interpretPrompt(s.prompt, r, intentLog);
    return r;
}

// ------------------------------------------------------------------ genome
namespace {

struct Gene {
    float TerrainRecipe::*field;
    float lo, hi, sigma;   // absolute bounds and relative sigma
};

const std::vector<Gene>& genes() {
    static const std::vector<Gene> g = {
        {&TerrainRecipe::baseFreq, 0.5f, 9.f, 0.10f},   {&TerrainRecipe::gain, 0.3f, 0.68f, 0.05f},
        {&TerrainRecipe::wFbm, 0.f, 1.f, 0.15f},        {&TerrainRecipe::wRidged, 0.f, 1.f, 0.15f},
        {&TerrainRecipe::wEroded, 0.f, 1.f, 0.15f},     {&TerrainRecipe::warp, 0.f, 1.5f, 0.2f},
        {&TerrainRecipe::relief, 0.1f, 1.2f, 0.08f},    {&TerrainRecipe::rangeStrength, 0.f, 1.f, 0.12f},
        {&TerrainRecipe::terraceStrength, 0.f, 1.f, 0.1f}, {&TerrainRecipe::mesas, 0.f, 1.f, 0.1f},
        {&TerrainRecipe::dunes, 0.f, 1.5f, 0.1f},       {&TerrainRecipe::canyons, 0.f, 1.5f, 0.12f},
        {&TerrainRecipe::karst, 0.f, 1.f, 0.12f},       {&TerrainRecipe::spikes, 0.f, 1.f, 0.12f},
        {&TerrainRecipe::glacial, 0.f, 1.5f, 0.12f},    {&TerrainRecipe::flatten, 0.f, 0.95f, 0.1f},
        {&TerrainRecipe::islandMask, 0.f, 1.f, 0.08f},
    };
    return g;
}

void mutateRecipe(TerrainRecipe& r, Rng& rng, float strength) {
    for (const auto& g : genes()) {
        float& v = r.*(g.field);
        if (v == 0.f && g.field != &TerrainRecipe::warp) continue;   // don't invent features the environment lacks
        v = clampf(v * (1.f + rng.normal() * g.sigma * strength), g.lo, g.hi);
    }
}

Vec2 randomInside(Rng& rng, float margin) { return {rng.range(margin, 1.f - margin), rng.range(margin, 1.f - margin)}; }

int roundCount(float expected, Rng& rng) {
    if (expected <= 0.f) return 0;
    int base = int(expected);
    float frac = expected - float(base);
    return base + (rng.uniform() < frac ? 1 : 0);
}

void jitterBlobs(std::vector<BlobFeature>& blobs, Rng& rng, float jitter) {
    for (auto& b : blobs) {
        b.pos.x = clampf(b.pos.x + rng.normal() * 0.035f * jitter, 0.02f, 0.98f);
        b.pos.y = clampf(b.pos.y + rng.normal() * 0.035f * jitter, 0.02f, 0.98f);
        b.radius *= 1.f + rng.normal() * 0.1f * jitter;
        b.amount *= 1.f + rng.normal() * 0.1f * jitter;
    }
}

}  // namespace

Genome makeGenome(const TerrainRecipe& recipe, const GenSettings& s, uint64_t macroSeed, uint64_t variationSeed, float jitter) {
    Genome g;
    g.r = recipe;
    g.macroSeed = macroSeed;
    g.detailSeed = hashCombine(macroSeed, variationSeed);
    g.featureScale = s.featureScale;
    const auto& era = eras()[std::clamp(s.eraIndex, 0, int(eras().size()) - 1)];
    g.farmTerraces = era.farmTerraces;

    Rng macro(macroSeed ^ 0x51ED270B27A1ull);
    float wa = macro.range(0.f, kTau);
    g.windDir = {std::cos(wa), std::sin(wa)};
    g.islandCenter = {macro.range(0.4f, 0.6f), macro.range(0.4f, 0.6f)};

    int nRanges = roundCount(recipe.ranges, macro);
    for (int i = 0; i < nRanges; ++i) {
        RangeFeature f;
        Vec2 c = randomInside(macro, 0.2f);
        float ang = macro.range(0.f, kPi);
        float len = macro.range(0.35f, 0.85f);
        Vec2 d{std::cos(ang) * len * 0.5f, std::sin(ang) * len * 0.5f};
        f.p0 = c - d;
        f.p2 = c + d;
        f.p1 = c + d.perp() * macro.range(-0.6f, 0.6f);
        f.width = macro.range(0.07f, 0.15f) / std::sqrt(std::max(0.3f, s.featureScale));
        f.height = recipe.rangeStrength * macro.range(0.7f, 1.2f);
        g.ranges.push_back(f);
    }
    int nBasins = roundCount(recipe.basins, macro);
    for (int i = 0; i < nBasins; ++i) g.basins.push_back({randomInside(macro, 0.12f), macro.range(0.05f, 0.14f), macro.range(0.15f, 0.35f)});
    int nVolc = recipe.volcanoes >= 0.5f ? std::max(1, roundCount(recipe.volcanoes, macro)) : 0;
    for (int i = 0; i < nVolc; ++i) {
        Vec2 p = i == 0 ? Vec2{macro.range(0.35f, 0.65f), macro.range(0.35f, 0.65f)} : randomInside(macro, 0.15f);
        float rad = i == 0 ? macro.range(0.18f, 0.28f) : macro.range(0.08f, 0.16f);
        g.volcanoes.push_back({p, rad, i == 0 ? macro.range(0.75f, 1.f) : macro.range(0.35f, 0.6f)});
    }
    int nCraters = roundCount(recipe.craters, macro);
    for (int i = 0; i < nCraters; ++i) {
        float u = macro.uniform();
        g.craters.push_back({randomInside(macro, 0.05f), 0.015f + 0.14f * u * u * u, macro.range(0.2f, 0.4f)});
    }
    int nQuarries = roundCount(era.quarries, macro);
    for (int i = 0; i < nQuarries; ++i) g.quarries.push_back({randomInside(macro, 0.15f), macro.range(0.025f, 0.05f), macro.range(0.08f, 0.14f)});

    // variation: same macro layout, nudged features + recipe genes + fresh detail seed
    Rng var(hashCombine(macroSeed, variationSeed) ^ 0xC0FFEEull);
    if (jitter > 0.f) {
        mutateRecipe(g.r, var, jitter * 0.6f);
        for (auto& f : g.ranges) {
            f.p0 += Vec2(var.normal(), var.normal()) * 0.03f * jitter;
            f.p1 += Vec2(var.normal(), var.normal()) * 0.05f * jitter;
            f.p2 += Vec2(var.normal(), var.normal()) * 0.03f * jitter;
            f.height *= 1.f + var.normal() * 0.1f * jitter;
        }
        jitterBlobs(g.basins, var, jitter);
        jitterBlobs(g.volcanoes, var, jitter * 0.6f);
        jitterBlobs(g.craters, var, jitter);
        jitterBlobs(g.quarries, var, jitter);
        float wa2 = std::atan2(g.windDir.y, g.windDir.x) + var.normal() * 0.15f * jitter;
        g.windDir = {std::cos(wa2), std::sin(wa2)};
    }
    return g;
}

// ------------------------------------------------------------------ measurement
TerrainMetrics measure(const Grid& h, const TerrainRecipe& r, float worldSize) {
    TerrainMetrics m;
    const int w = h.width(), hh = h.height();
    if (w < 3) return m;
    double sum = 0, sum2 = 0;
    for (float v : h.vec()) { sum += v; sum2 += double(v) * v; }
    double n = double(h.size());
    double mean = sum / n;
    m.relief = float(std::sqrt(std::max(0.0, sum2 / n - mean * mean)) * 4.0);

    const float cell = worldSize / float(w - 1);
    const float hs = r.heightRange / cell;
    double slopeSum = 0, lapSum = 0, lap2 = 0, cx = 0, cy = 0, mass = 0;
    int land = 0, flat = 0, water = 0, count = 0;
    for (int y = 1; y < hh - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            float v = h.at(x, y);
            float gx = (h.at(x + 1, y) - h.at(x - 1, y)) * 0.5f * hs;
            float gy = (h.at(x, y + 1) - h.at(x, y - 1)) * 0.5f * hs;
            float s = std::sqrt(gx * gx + gy * gy);
            float lap = h.at(x + 1, y) + h.at(x - 1, y) + h.at(x, y + 1) + h.at(x, y - 1) - 4.f * v;
            ++count;
            if (r.seaLevel >= 0.f && v < r.seaLevel) { ++water; continue; }
            ++land;
            slopeSum += s;
            if (s < 0.12f) ++flat;
            lapSum += lap;
            lap2 += double(lap) * lap;
            cx += x * v;
            cy += y * v;
            mass += v;
        }
    }
    m.water = count ? float(water) / float(count) : 0.f;
    m.slope = land ? float(slopeSum / land) : 0.f;
    m.flat = land ? float(flat) / float(land) : 0.f;
    if (land) {
        double lm = lapSum / land;
        double lstd = std::sqrt(std::max(0.0, lap2 / land - lm * lm));
        m.interest = clamp01(float(lstd * double(w) * 0.35));
    }
    if (mass > 0) {
        float dx = float(cx / mass) / float(w) - 0.5f, dy = float(cy / mass) / float(hh) - 0.5f;
        m.balance = std::sqrt(dx * dx + dy * dy);
    }
    return m;
}

float scoreMetrics(const TerrainMetrics& m, const DirectorTargets& t, const TerrainRecipe& r) {
    auto sq = [](float a) { return a * a; };
    float e = 0;
    e += 1.0f * sq((m.relief - t.relief) / 0.3f);
    e += 1.2f * sq((m.slope - t.slope) / std::max(0.1f, t.slope));
    if (r.seaLevel >= 0.f) e += 2.0f * sq((m.water - t.water) / 0.2f);
    e += 0.8f * sq((m.flat - t.flat) / 0.25f);
    e += 0.6f * sq((m.interest - t.interest) / 0.3f);
    e += 1.5f * sq(m.balance / 0.25f);   // avoid all the mass piled in one corner
    return 100.f / (1.f + e);
}

Genome directTerrain(const GenSettings& s, DirectorReport& report, Progress* progress, float p0, float p1) {
    const auto& env = environments()[std::clamp(s.envIndex, 0, int(environments().size()) - 1)];
    report = DirectorReport{};
    TerrainRecipe base = composeRecipe(s, &report.intent);

    // Slope targets assume the default world; rescale for the chosen size & height so the director
    // judges shape, not units.
    DirectorTargets targets = env.targets;
    {
        float envScale = env.recipe.heightRange / 4033.f;
        float ourScale = base.heightRange / std::max(100.f, s.worldSize);
        targets.slope *= ourScale / std::max(1e-4f, envScale);
    }
    if (base.seaLevel >= 0.f && env.recipe.seaLevel < 0.f) targets.water = std::max(targets.water, base.seaLevel * 0.6f);

    const uint64_t macroSeed = seedFromText(s.seedText);
    const uint64_t varSeed = splitmix64(uint64_t(uint32_t(s.variation)) + 0x1234567ull);
    const int previewRes = 160;
    const int population = std::max(1, s.candidates);
    const int total = population + std::max(0, s.refineSteps);

    Genome best;
    float bestScore = -1.f;
    int bestIndex = 0;
    Grid preview;
    for (int c = 0; c < population; ++c) {
        if (progress && progress->cancel) break;
        Genome g = makeGenome(base, s, macroSeed, hashCombine(varSeed, uint64_t(c)), c == 0 ? 0.5f : 1.f);
        synthesizeHeight(g, previewRes, preview, nullptr, true);
        TerrainMetrics m = measure(preview, g.r, s.worldSize);
        float sc = scoreMetrics(m, targets, g.r);
        ++report.evaluated;
        if (sc > bestScore) {
            bestScore = sc;
            best = g;
            bestIndex = c;
            report.metrics = m;
        }
        if (progress) progress->set("Director: evaluating candidate layouts", p0 + (p1 - p0) * float(c + 1) / float(total));
    }
    // Hill-climb refinement around the winner.
    Rng refine(hashCombine(varSeed, 0xBEEFull));
    int improvements = 0;
    for (int i = 0; i < s.refineSteps; ++i) {
        if (progress && progress->cancel) break;
        Genome g = best;
        mutateRecipe(g.r, refine, 0.5f);
        g.detailSeed = hashCombine(best.detailSeed, uint64_t(i + 1));
        synthesizeHeight(g, previewRes, preview, nullptr, true);
        TerrainMetrics m = measure(preview, g.r, s.worldSize);
        float sc = scoreMetrics(m, targets, g.r);
        ++report.evaluated;
        if (sc > bestScore) {
            bestScore = sc;
            best = g;
            report.metrics = m;
            ++improvements;
        }
        if (progress) progress->set("Director: refining the best layout", p0 + (p1 - p0) * float(population + i + 1) / float(total));
    }
    // Real-world scale: slopes grow linearly with vertical scale, so nudge the height range toward the
    // environment's realistic mean slope instead of shipping 70-degree "noise mountains".
    if (report.metrics.slope > 1e-3f) {
        float fit = clampf(targets.slope / report.metrics.slope, 0.45f, 1.3f);
        best.r.heightRange *= fit;
        report.metrics.slope *= fit;
        char nb[96];
        std::snprintf(nb, sizeof(nb), "vertical scale fitted x%.2f -> %.0f m relief", fit, best.r.heightRange);
        report.intent.push_back(nb);
    }
    report.score = bestScore;
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "Picked candidate %d of %d (%d refinements kept). Fit %.0f/100 - relief %.2f (target %.2f), slope %.2f (%.2f), "
                  "flat %.0f%% (%.0f%%)%s, %zu ranges, %zu basins, %zu volcanoes, %zu craters.",
                  bestIndex + 1, population, improvements, bestScore, report.metrics.relief, targets.relief, report.metrics.slope,
                  targets.slope, report.metrics.flat * 100.f, targets.flat * 100.f,
                  best.r.seaLevel >= 0 ? (", water " + std::to_string(int(report.metrics.water * 100.f)) + "%").c_str() : "",
                  best.ranges.size(), best.basins.size(), best.volcanoes.size(), best.craters.size());
    report.summary = buf;
    return best;
}

}  // namespace zl
