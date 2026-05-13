#include "AdaptiveEngine.h"
#include <cmath>
#include <algorithm>

void AdaptiveEngine::cloneFrom(const InferenceEngine& base) {
    discardConvLayers_.clear();
    discardFcLayers_.clear();
    claimConvLayers_.clear();
    claimFcLayers_.clear();

    for (const auto& layer : base.getDiscardConvLayers()) {
        discardConvLayers_.push_back({layer.inChannels, layer.outChannels,
                                      layer.kernelSize, layer.weight, layer.bias});
    }
    for (const auto& layer : base.getDiscardFcLayers()) {
        discardFcLayers_.push_back({layer.inFeatures, layer.outFeatures,
                                    layer.weight, layer.bias});
    }
    for (const auto& layer : base.getClaimConvLayers()) {
        claimConvLayers_.push_back({layer.inChannels, layer.outChannels,
                                    layer.kernelSize, layer.weight, layer.bias});
    }
    for (const auto& layer : base.getClaimFcLayers()) {
        claimFcLayers_.push_back({layer.inFeatures, layer.outFeatures,
                                  layer.weight, layer.bias});
    }
}

// --- Conv1d helper (same as InferenceEngine) ---

static void conv1dLayerForward(const InferenceEngine::Conv1dLayer& layer,
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

// --- Forward pass (no caching) ---

std::vector<float> AdaptiveEngine::forward(
    const std::vector<Conv1dLayer>& convLayers,
    const std::vector<LinearLayer>& fcLayers,
    const std::vector<float>& features,
    NetworkType netType)
{
    // Reshape flat features into channels + scalars
    std::vector<float> channels, scalars;
    if (netType == NetworkType::Discard) {
        InferenceEngine::reshapeDiscardFeatures(features, channels, scalars);
    } else {
        InferenceEngine::reshapeClaimFeatures(features, channels, scalars);
    }
    int width = InferenceEngine::TILE_WIDTH;

    // Conv layers with ReLU
    std::vector<float> current = channels;
    for (const auto& layer : convLayers) {
        std::vector<float> output;
        conv1dLayerForward(layer, current, width, output);
        for (auto& v : output) v = std::fmax(v, 0.0f);
        current = std::move(output);
    }

    // Average pool → [outChannels]
    int finalCh = convLayers.back().outChannels;
    std::vector<float> pooled(finalCh, 0.0f);
    for (int c = 0; c < finalCh; c++) {
        float sum = 0.0f;
        for (int p = 0; p < width; p++) sum += current[c * width + p];
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
            if (l < fcLayers.size() - 1) sum = std::fmax(sum, 0.0f);
            output[i] = sum;
        }
        combined = std::move(output);
    }

    return combined;
}

// --- Forward pass with caching for backprop ---

std::vector<float> AdaptiveEngine::forwardCached(
    const std::vector<Conv1dLayer>& convLayers,
    const std::vector<LinearLayer>& fcLayers,
    const std::vector<float>& features,
    NetworkType netType,
    ForwardCache& cache)
{
    // Reshape
    std::vector<float> channels;
    if (netType == NetworkType::Discard) {
        InferenceEngine::reshapeDiscardFeatures(features, channels, cache.scalars);
    } else {
        InferenceEngine::reshapeClaimFeatures(features, channels, cache.scalars);
    }
    int width = InferenceEngine::TILE_WIDTH;
    cache.width = width;

    // Conv layers with caching
    int numConv = static_cast<int>(convLayers.size());
    cache.convInputs.resize(numConv);
    cache.convPreActs.resize(numConv);
    cache.convActs.resize(numConv);

    std::vector<float> current = channels;
    for (int l = 0; l < numConv; l++) {
        cache.convInputs[l] = current;

        std::vector<float> z;
        conv1dLayerForward(convLayers[l], current, width, z);
        cache.convPreActs[l] = z;

        // ReLU
        std::vector<float> a(z.size());
        for (size_t i = 0; i < z.size(); i++) {
            a[i] = std::fmax(z[i], 0.0f);
        }
        cache.convActs[l] = a;
        current = std::move(a);
    }

    // Average pool
    int finalCh = convLayers.back().outChannels;
    cache.pooled.resize(finalCh);
    for (int c = 0; c < finalCh; c++) {
        float sum = 0.0f;
        for (int p = 0; p < width; p++) sum += current[c * width + p];
        cache.pooled[c] = sum / static_cast<float>(width);
    }

    // Concat
    cache.fcInput.clear();
    cache.fcInput.reserve(cache.pooled.size() + cache.scalars.size());
    cache.fcInput.insert(cache.fcInput.end(), cache.pooled.begin(), cache.pooled.end());
    cache.fcInput.insert(cache.fcInput.end(), cache.scalars.begin(), cache.scalars.end());

    // FC layers with caching
    int numFc = static_cast<int>(fcLayers.size());
    cache.fcPreActivations.resize(numFc);
    cache.fcActivations.resize(numFc);

    std::vector<float> combined = cache.fcInput;
    for (int l = 0; l < numFc; l++) {
        const auto& layer = fcLayers[l];
        std::vector<float> z(layer.outFeatures);

        for (int i = 0; i < layer.outFeatures; i++) {
            float sum = layer.bias[i];
            const float* row = layer.weight.data() + i * layer.inFeatures;
            for (int j = 0; j < layer.inFeatures; j++) {
                sum += row[j] * combined[j];
            }
            z[i] = sum;
        }

        cache.fcPreActivations[l] = z;

        if (l < numFc - 1) {
            std::vector<float> a(layer.outFeatures);
            for (int i = 0; i < layer.outFeatures; i++) {
                a[i] = std::fmax(z[i], 0.0f);
            }
            cache.fcActivations[l] = a;
            combined = std::move(a);
        } else {
            cache.fcActivations[l] = z;
            combined = std::move(z);
        }
    }

    return combined;
}

// --- Inference ---

std::vector<float> AdaptiveEngine::inferDiscard(const std::vector<float>& features) const {
    if (discardConvLayers_.empty()) return {};
    return forward(discardConvLayers_, discardFcLayers_, features, NetworkType::Discard);
}

std::vector<float> AdaptiveEngine::inferClaim(const std::vector<float>& features) const {
    if (claimConvLayers_.empty()) return {};
    return forward(claimConvLayers_, claimFcLayers_, features, NetworkType::Claim);
}

// --- Training with backprop (FC only, conv frozen per pMCPA) ---

float AdaptiveEngine::trainBatch(
    std::vector<Conv1dLayer>& convLayers,
    std::vector<LinearLayer>& fcLayers,
    const std::vector<AdaptationSample>& batch,
    NetworkType netType,
    const AdaptationConfig& config)
{
    if (batch.empty() || convLayers.empty() || fcLayers.empty()) return 0.0f;

    int numFc = static_cast<int>(fcLayers.size());

    // Accumulate gradients (FC layers only — conv layers are frozen per pMCPA)
    std::vector<std::vector<float>> dFcW(numFc), dFcB(numFc);

    for (int l = 0; l < numFc; l++) {
        dFcW[l].assign(fcLayers[l].weight.size(), 0.0f);
        dFcB[l].assign(fcLayers[l].bias.size(), 0.0f);
    }

    float totalLoss = 0.0f;

    for (const auto& sample : batch) {
        ForwardCache cache;
        auto output = forwardCached(convLayers, fcLayers, sample.state, netType, cache);
        int outputSize = static_cast<int>(output.size());

        if (sample.action < 0 || sample.action >= outputSize) continue;

        // Huber loss on taken action
        float predicted = output[sample.action];
        float error = sample.tdTarget - predicted;
        float absError = std::fabs(error);

        float loss, dLoss;
        if (absError <= 1.0f) {
            loss = 0.5f * error * error;
            dLoss = -error;
        } else {
            loss = absError - 0.5f;
            dLoss = error > 0 ? -1.0f : 1.0f;
        }
        totalLoss += loss;

        // === FC backward pass ===
        std::vector<float> dOutput(outputSize, 0.0f);
        dOutput[sample.action] = dLoss;

        std::vector<float> dActivation = dOutput;

        for (int l = numFc - 1; l >= 0; l--) {
            const auto& layer = fcLayers[l];
            int out = layer.outFeatures;
            int in = layer.inFeatures;

            std::vector<float> dz(out);
            if (l < numFc - 1) {
                for (int i = 0; i < out; i++) {
                    dz[i] = (cache.fcPreActivations[l][i] > 0.0f) ? dActivation[i] : 0.0f;
                }
            } else {
                dz = dActivation;
            }

            const std::vector<float>& aIn = (l > 0) ? cache.fcActivations[l - 1] : cache.fcInput;

            for (int i = 0; i < out; i++) {
                float* dWrow = dFcW[l].data() + i * in;
                for (int j = 0; j < in; j++) {
                    dWrow[j] += dz[i] * aIn[j];
                }
                dFcB[l][i] += dz[i];
            }

            // Propagate gradient to previous FC layer
            std::vector<float> daPrev(in, 0.0f);
            for (int i = 0; i < out; i++) {
                const float* Wrow = layer.weight.data() + i * in;
                for (int j = 0; j < in; j++) {
                    daPrev[j] += Wrow[j] * dz[i];
                }
            }
            dActivation = std::move(daPrev);
        }

        // pMCPA: gradient stops here — conv layers are frozen, no backprop into pool/conv
    }

    // Average gradients
    float batchSizeF = static_cast<float>(batch.size());
    for (int l = 0; l < numFc; l++) {
        for (auto& g : dFcW[l]) g /= batchSizeF;
        for (auto& g : dFcB[l]) g /= batchSizeF;
    }

    // Global gradient norm for clipping (FC only — conv frozen)
    float gradNormSq = 0.0f;
    for (int l = 0; l < numFc; l++) {
        for (float g : dFcW[l]) gradNormSq += g * g;
        for (float g : dFcB[l]) gradNormSq += g * g;
    }
    float gradNorm = std::sqrt(gradNormSq);

    float clipScale = 1.0f;
    if (gradNorm > config.gradClipNorm) {
        clipScale = config.gradClipNorm / gradNorm;
    }

    // SGD update (FC layers only — conv layers frozen per pMCPA)
    float lr = config.learningRate * clipScale;
    for (int l = 0; l < numFc; l++) {
        for (size_t i = 0; i < fcLayers[l].weight.size(); i++)
            fcLayers[l].weight[i] -= lr * dFcW[l][i];
        for (size_t i = 0; i < fcLayers[l].bias.size(); i++)
            fcLayers[l].bias[i] -= lr * dFcB[l][i];
    }

    return totalLoss / batchSizeF;
}

float AdaptiveEngine::trainDiscardBatch(const std::vector<AdaptationSample>& batch,
                                         const AdaptationConfig& config) {
    return trainBatch(discardConvLayers_, discardFcLayers_, batch,
                      NetworkType::Discard, config);
}

float AdaptiveEngine::trainClaimBatch(const std::vector<AdaptationSample>& batch,
                                       const AdaptationConfig& config) {
    return trainBatch(claimConvLayers_, claimFcLayers_, batch,
                      NetworkType::Claim, config);
}
