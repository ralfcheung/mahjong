#pragma once
#include "InferenceEngine.h"
#include <vector>
#include <cstdint>

struct AdaptationConfig {
    float learningRate = 0.001f;
    int numSimulations = 200;
    int maxSGDSteps = 20;
    int batchSize = 32;
    float gamma = 0.95f;
    float gradClipNorm = 1.0f;
};

struct AdaptationSample {
    std::vector<float> state;
    int action;
    float tdTarget;
};

// Trainable clone of InferenceEngine weights.
// Supports CNN+FC forward pass with caching, backward pass, and SGD updates.
class AdaptiveEngine {
public:
    void cloneFrom(const InferenceEngine& base);

    bool hasDiscardWeights() const { return !discardConvLayers_.empty(); }
    bool hasClaimWeights() const { return !claimConvLayers_.empty(); }

    std::vector<float> inferDiscard(const std::vector<float>& features) const;
    std::vector<float> inferClaim(const std::vector<float>& features) const;

    float trainDiscardBatch(const std::vector<AdaptationSample>& batch,
                            const AdaptationConfig& config);
    float trainClaimBatch(const std::vector<AdaptationSample>& batch,
                          const AdaptationConfig& config);

private:
    using LinearLayer = InferenceEngine::LinearLayer;
    using Conv1dLayer = InferenceEngine::Conv1dLayer;

    std::vector<Conv1dLayer> discardConvLayers_;
    std::vector<LinearLayer> discardFcLayers_;
    std::vector<Conv1dLayer> claimConvLayers_;
    std::vector<LinearLayer> claimFcLayers_;

    enum class NetworkType { Discard, Claim };

    struct ForwardCache {
        // Conv intermediates: input to each layer, pre-activation, post-activation
        // Each stored as flat [channels × width] row-major
        std::vector<std::vector<float>> convInputs;
        std::vector<std::vector<float>> convPreActs;
        std::vector<std::vector<float>> convActs;

        // Scalars extracted from input
        std::vector<float> scalars;

        // Pooled conv output (before concat)
        std::vector<float> pooled;

        // FC intermediates
        std::vector<float> fcInput;  // concat(pooled, scalars)
        std::vector<std::vector<float>> fcPreActivations;
        std::vector<std::vector<float>> fcActivations;

        int width;  // tile width (34)
    };

    // Forward pass (inference only, no caching)
    static std::vector<float> forward(
        const std::vector<Conv1dLayer>& convLayers,
        const std::vector<LinearLayer>& fcLayers,
        const std::vector<float>& features,
        NetworkType netType);

    // Forward pass with caching for backprop
    static std::vector<float> forwardCached(
        const std::vector<Conv1dLayer>& convLayers,
        const std::vector<LinearLayer>& fcLayers,
        const std::vector<float>& features,
        NetworkType netType,
        ForwardCache& cache);

    // Train on a batch. Returns average loss.
    static float trainBatch(
        std::vector<Conv1dLayer>& convLayers,
        std::vector<LinearLayer>& fcLayers,
        const std::vector<AdaptationSample>& batch,
        NetworkType netType,
        const AdaptationConfig& config);
};
