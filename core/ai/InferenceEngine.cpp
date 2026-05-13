#include "InferenceEngine.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>

// --- Feature reshaping ---

void InferenceEngine::reshapeDiscardFeatures(const std::vector<float>& flat,
                                              std::vector<float>& channels,
                                              std::vector<float>& scalars) {
    // 10 tile channels × 34 types, extracted from known offsets in the 408-dim vector
    static constexpr int offsets[DISCARD_NUM_CHANNELS] = {
        0, 34, 68, 102, 136, 182, 216, 250, 284, 344
    };

    channels.resize(DISCARD_NUM_CHANNELS * TILE_WIDTH);
    for (int c = 0; c < DISCARD_NUM_CHANNELS; c++) {
        int off = offsets[c];
        for (int j = 0; j < TILE_WIDTH; j++) {
            channels[c * TILE_WIDTH + j] = flat[off + j];
        }
    }

    // 68 scalar features from 3 non-contiguous ranges
    scalars.resize(DISCARD_NUM_SCALARS);
    int s = 0;
    for (int i = 170; i < 182; i++) scalars[s++] = flat[i];  // 12: opp meld types
    for (int i = 318; i < 344; i++) scalars[s++] = flat[i];  // 26: game context
    for (int i = 378; i < 408; i++) scalars[s++] = flat[i];  // 30: additional features
}

void InferenceEngine::reshapeClaimFeatures(const std::vector<float>& flat,
                                            std::vector<float>& channels,
                                            std::vector<float>& scalars) {
    // 11 tile channels: same 10 as discard + claimed tile one-hot
    static constexpr int offsets[CLAIM_NUM_CHANNELS] = {
        0, 34, 68, 102, 136, 182, 216, 250, 284, 344, 408
    };

    channels.resize(CLAIM_NUM_CHANNELS * TILE_WIDTH);
    for (int c = 0; c < CLAIM_NUM_CHANNELS; c++) {
        int off = offsets[c];
        for (int j = 0; j < TILE_WIDTH; j++) {
            channels[c * TILE_WIDTH + j] = flat[off + j];
        }
    }

    // 76 scalar features: 68 from discard + 8 from claim
    scalars.resize(CLAIM_NUM_SCALARS);
    int s = 0;
    for (int i = 170; i < 182; i++) scalars[s++] = flat[i];
    for (int i = 318; i < 344; i++) scalars[s++] = flat[i];
    for (int i = 378; i < 408; i++) scalars[s++] = flat[i];
    for (int i = 442; i < 450; i++) scalars[s++] = flat[i];  // 8: claim mask + shanten
}

// --- Weight loading ---

bool InferenceEngine::loadCNNLayers(const std::string& path,
                                     const std::vector<std::tuple<int,int,int>>& convDims,
                                     const std::vector<std::pair<int,int>>& fcDims,
                                     std::vector<Conv1dLayer>& convLayers,
                                     std::vector<LinearLayer>& fcLayers) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Calculate expected size
    size_t expectedSize = 0;
    for (const auto& [inCh, outCh, kSize] : convDims) {
        expectedSize += (size_t)(outCh * inCh * kSize + outCh) * sizeof(float);
    }
    for (const auto& [in, out] : fcDims) {
        expectedSize += (size_t)(out * in + out) * sizeof(float);
    }

    if (fileSize != expectedSize) {
        std::fprintf(stderr, "InferenceEngine: weight file size mismatch for %s "
                    "(expected %zu, got %zu)\n", path.c_str(), expectedSize, fileSize);
        return false;
    }

    convLayers.clear();
    convLayers.reserve(convDims.size());

    for (const auto& [inCh, outCh, kSize] : convDims) {
        Conv1dLayer layer;
        layer.inChannels = inCh;
        layer.outChannels = outCh;
        layer.kernelSize = kSize;
        layer.weight.resize(outCh * inCh * kSize);
        layer.bias.resize(outCh);

        file.read(reinterpret_cast<char*>(layer.weight.data()),
                  layer.weight.size() * sizeof(float));
        file.read(reinterpret_cast<char*>(layer.bias.data()),
                  layer.bias.size() * sizeof(float));

        if (!file.good()) {
            std::fprintf(stderr, "InferenceEngine: read error in %s\n", path.c_str());
            convLayers.clear();
            return false;
        }

        convLayers.push_back(std::move(layer));
    }

    fcLayers.clear();
    fcLayers.reserve(fcDims.size());

    for (const auto& [in, out] : fcDims) {
        LinearLayer layer;
        layer.inFeatures = in;
        layer.outFeatures = out;
        layer.weight.resize(out * in);
        layer.bias.resize(out);

        file.read(reinterpret_cast<char*>(layer.weight.data()),
                  layer.weight.size() * sizeof(float));
        file.read(reinterpret_cast<char*>(layer.bias.data()),
                  layer.bias.size() * sizeof(float));

        if (!file.good()) {
            std::fprintf(stderr, "InferenceEngine: read error in %s\n", path.c_str());
            convLayers.clear();
            fcLayers.clear();
            return false;
        }

        fcLayers.push_back(std::move(layer));
    }

    return true;
}

bool InferenceEngine::loadDiscardWeights(const std::string& path) {
    // DiscardNet CNN: Conv(10→64,k3) → Conv(64→128,k3) → Conv(128→128,k3)
    //                → AvgPool → FC(196→256) → FC(256→34)
    return loadCNNLayers(path,
                         {{10, 64, 3}, {64, 128, 3}, {128, 128, 3}},
                         {{196, 256}, {256, 34}},
                         discardConvLayers_, discardFcLayers_);
}

bool InferenceEngine::loadClaimWeights(const std::string& path) {
    // ClaimNet CNN: Conv(11→64,k3) → Conv(64→128,k3) → Conv(128→128,k3)
    //              → AvgPool → FC(204→256) → FC(256→4)
    return loadCNNLayers(path,
                         {{11, 64, 3}, {64, 128, 3}, {128, 128, 3}},
                         {{204, 256}, {256, 4}},
                         claimConvLayers_, claimFcLayers_);
}

// --- Conv1d forward pass ---

void InferenceEngine::conv1dForward(const Conv1dLayer& layer,
                                     const std::vector<float>& input, int width,
                                     std::vector<float>& output) {
    int outCh = layer.outChannels;
    int inCh = layer.inChannels;
    int kSize = layer.kernelSize;
    int pad = kSize / 2;

    output.resize(outCh * width);

    for (int o = 0; o < outCh; o++) {
        for (int p = 0; p < width; p++) {
            float sum = layer.bias[o];
            for (int i = 0; i < inCh; i++) {
                for (int k = 0; k < kSize; k++) {
                    int inPos = p + k - pad;
                    if (inPos >= 0 && inPos < width) {
                        sum += layer.weight[(o * inCh + i) * kSize + k] *
                               input[i * width + inPos];
                    }
                }
            }
            output[o * width + p] = sum;
        }
    }
}

// --- Full CNN+FC forward pass ---

std::vector<float> InferenceEngine::cnnForward(
    const std::vector<Conv1dLayer>& convLayers,
    const std::vector<LinearLayer>& fcLayers,
    const std::vector<float>& channels,
    int numChannels, int width,
    const std::vector<float>& scalars)
{
    // Conv layers with ReLU
    std::vector<float> current = channels;
    for (const auto& layer : convLayers) {
        std::vector<float> output;
        conv1dForward(layer, current, width, output);
        // ReLU
        for (auto& v : output) v = std::fmax(v, 0.0f);
        current = std::move(output);
    }

    // Average pool over width → [outChannels]
    int finalChannels = convLayers.back().outChannels;
    std::vector<float> pooled(finalChannels, 0.0f);
    for (int c = 0; c < finalChannels; c++) {
        float sum = 0.0f;
        for (int p = 0; p < width; p++) {
            sum += current[c * width + p];
        }
        pooled[c] = sum / static_cast<float>(width);
    }

    // Concat pooled + scalars
    std::vector<float> combined;
    combined.reserve(pooled.size() + scalars.size());
    combined.insert(combined.end(), pooled.begin(), pooled.end());
    combined.insert(combined.end(), scalars.begin(), scalars.end());

    // FC layers with ReLU (except last)
    for (size_t l = 0; l < fcLayers.size(); l++) {
        const auto& layer = fcLayers[l];
        std::vector<float> output(layer.outFeatures);

        for (int i = 0; i < layer.outFeatures; i++) {
            float sum = layer.bias[i];
            const float* row = layer.weight.data() + i * layer.inFeatures;
            for (int j = 0; j < layer.inFeatures; j++) {
                sum += row[j] * combined[j];
            }
            if (l < fcLayers.size() - 1) {
                sum = std::fmax(sum, 0.0f);
            }
            output[i] = sum;
        }

        combined = std::move(output);
    }

    return combined;
}

// --- Public inference methods ---

std::vector<float> InferenceEngine::inferDiscard(const std::vector<float>& features) const {
    if (discardConvLayers_.empty()) return {};
    std::vector<float> channels, scalars;
    reshapeDiscardFeatures(features, channels, scalars);
    return cnnForward(discardConvLayers_, discardFcLayers_,
                      channels, DISCARD_NUM_CHANNELS, TILE_WIDTH, scalars);
}

std::vector<float> InferenceEngine::inferClaim(const std::vector<float>& features) const {
    if (claimConvLayers_.empty()) return {};
    std::vector<float> channels, scalars;
    reshapeClaimFeatures(features, channels, scalars);
    return cnnForward(claimConvLayers_, claimFcLayers_,
                      channels, CLAIM_NUM_CHANNELS, TILE_WIDTH, scalars);
}

int InferenceEngine::selectBestAction(const std::vector<float>& logits,
                                       const std::vector<bool>& validMask) {
    int bestIdx = -1;
    float bestVal = -1e30f;

    for (int i = 0; i < (int)logits.size() && i < (int)validMask.size(); i++) {
        if (validMask[i] && logits[i] > bestVal) {
            bestVal = logits[i];
            bestIdx = i;
        }
    }

    // Fallback: pick first valid action
    if (bestIdx < 0) {
        for (int i = 0; i < (int)validMask.size(); i++) {
            if (validMask[i]) return i;
        }
    }

    return bestIdx;
}
