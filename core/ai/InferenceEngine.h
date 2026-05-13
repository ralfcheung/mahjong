#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <utility>

// Lightweight pure-C++ neural network inference engine.
// Runs forward pass through CNN+FC hybrid DiscardNet and ClaimNet architectures.
// Zero external dependencies.
//
// Binary weight format (all values are little-endian float32):
//   For each conv layer in order:
//     weight: out_channels × in_channels × kernel_size floats (row-major)
//     bias:   out_channels floats
//   For each FC layer in order:
//     weight: out_features × in_features floats (row-major)
//     bias:   out_features floats
//
// DiscardNet: Conv1d(10→64,k3) → Conv1d(64→128,k3) → Conv1d(128→128,k3)
//             → AvgPool → concat(128+68) → Linear(196→256) → Linear(256→34)
// ClaimNet:   Conv1d(11→64,k3) → Conv1d(64→128,k3) → Conv1d(128→128,k3)
//             → AvgPool → concat(128+76) → Linear(204→256) → Linear(256→4)

class InferenceEngine {
public:
    // Load weights from flat binary files.
    bool loadDiscardWeights(const std::string& path);
    bool loadClaimWeights(const std::string& path);

    bool hasDiscardWeights() const { return !discardConvLayers_.empty(); }
    bool hasClaimWeights() const { return !claimConvLayers_.empty(); }

    // Run forward pass. Input size must match network input dimension (408/450).
    std::vector<float> inferDiscard(const std::vector<float>& features) const;
    std::vector<float> inferClaim(const std::vector<float>& features) const;

    // Select best valid action from logits.
    static int selectBestAction(const std::vector<float>& logits,
                                const std::vector<bool>& validMask);

    // LinearLayer is public so AdaptiveEngine can clone weights
    struct LinearLayer {
        int inFeatures;
        int outFeatures;
        std::vector<float> weight;  // outFeatures × inFeatures, row-major
        std::vector<float> bias;    // outFeatures
    };

    struct Conv1dLayer {
        int inChannels;
        int outChannels;
        int kernelSize;
        std::vector<float> weight;  // outChannels × inChannels × kernelSize, row-major
        std::vector<float> bias;    // outChannels
    };

    const std::vector<Conv1dLayer>& getDiscardConvLayers() const { return discardConvLayers_; }
    const std::vector<LinearLayer>& getDiscardFcLayers() const { return discardFcLayers_; }
    const std::vector<Conv1dLayer>& getClaimConvLayers() const { return claimConvLayers_; }
    const std::vector<LinearLayer>& getClaimFcLayers() const { return claimFcLayers_; }

    // Feature reshaping constants
    static constexpr int TILE_WIDTH = 34;
    static constexpr int DISCARD_NUM_CHANNELS = 10;
    static constexpr int CLAIM_NUM_CHANNELS = 11;
    static constexpr int DISCARD_NUM_SCALARS = 68;
    static constexpr int CLAIM_NUM_SCALARS = 76;

    // Reshape flat feature vectors into tile channels and scalar features.
    // channels: [numChannels × 34] row-major, scalars: [numScalars]
    static void reshapeDiscardFeatures(const std::vector<float>& flat,
                                       std::vector<float>& channels,
                                       std::vector<float>& scalars);
    static void reshapeClaimFeatures(const std::vector<float>& flat,
                                     std::vector<float>& channels,
                                     std::vector<float>& scalars);

private:
    std::vector<Conv1dLayer> discardConvLayers_;
    std::vector<LinearLayer> discardFcLayers_;
    std::vector<Conv1dLayer> claimConvLayers_;
    std::vector<LinearLayer> claimFcLayers_;

    // Load conv + FC layers from binary file
    static bool loadCNNLayers(const std::string& path,
                              const std::vector<std::tuple<int,int,int>>& convDims,
                              const std::vector<std::pair<int,int>>& fcDims,
                              std::vector<Conv1dLayer>& convLayers,
                              std::vector<LinearLayer>& fcLayers);

    // Conv1d forward on a single layer.
    // input: [inChannels × width] row-major → output: [outChannels × width]
    static void conv1dForward(const Conv1dLayer& layer,
                              const std::vector<float>& input, int width,
                              std::vector<float>& output);

    // Full CNN+FC forward pass.
    static std::vector<float> cnnForward(const std::vector<Conv1dLayer>& convLayers,
                                         const std::vector<LinearLayer>& fcLayers,
                                         const std::vector<float>& channels,
                                         int numChannels, int width,
                                         const std::vector<float>& scalars);
};
