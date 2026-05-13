#ifdef HAS_TORCH

#include "MahjongNet.h"
#include "RLFeatures.h"

// DiscardNet: CNN+FC hybrid
// Input: flat 408-dim vector, reshaped into 10 tile channels (34 wide) + 68 scalars
DiscardNetImpl::DiscardNetImpl() {
    conv1 = register_module("conv1", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(10, 64, 3).padding(1)));
    conv2 = register_module("conv2", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(64, 128, 3).padding(1)));
    conv3 = register_module("conv3", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(128, 128, 3).padding(1)));
    pool = register_module("pool", torch::nn::AdaptiveAvgPool1d(1));
    fc1 = register_module("fc1", torch::nn::Linear(128 + 68, 256));
    fc2 = register_module("fc2", torch::nn::Linear(256, NUM_TILE_TYPES));
    drop1 = register_module("drop1", torch::nn::Dropout(0.1));
    drop2 = register_module("drop2", torch::nn::Dropout(0.1));
    drop3 = register_module("drop3", torch::nn::Dropout(0.1));
}

torch::Tensor DiscardNetImpl::forward(torch::Tensor x) {
    // x: [batch, 408]
    // Extract 10 tile channels (each 34 wide) from known offsets
    auto ch0 = x.narrow(1, 0, 34);      // hand counts
    auto ch1 = x.narrow(1, 34, 34);     // own melds
    auto ch2 = x.narrow(1, 68, 34);     // opp1 melds
    auto ch3 = x.narrow(1, 102, 34);    // opp2 melds
    auto ch4 = x.narrow(1, 136, 34);    // opp3 melds
    auto ch5 = x.narrow(1, 182, 34);    // self discards
    auto ch6 = x.narrow(1, 216, 34);    // opp1 discards
    auto ch7 = x.narrow(1, 250, 34);    // opp2 discards
    auto ch8 = x.narrow(1, 284, 34);    // opp3 discards
    auto ch9 = x.narrow(1, 344, 34);    // live tile counts

    // Stack → [batch, 10, 34]
    auto channels = torch::stack({ch0, ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8, ch9}, 1);

    // Extract scalar features → [batch, 68]
    auto s0 = x.narrow(1, 170, 12);     // opp meld types
    auto s1 = x.narrow(1, 318, 26);     // game context
    auto s2 = x.narrow(1, 378, 30);     // additional features
    auto scalars = torch::cat({s0, s1, s2}, 1);

    // Conv layers
    channels = drop1(torch::relu(conv1(channels)));  // [batch, 64, 34]
    channels = drop2(torch::relu(conv2(channels)));  // [batch, 128, 34]
    channels = torch::relu(conv3(channels));          // [batch, 128, 34]

    // Global average pool → [batch, 128, 1] → squeeze → [batch, 128]
    auto pooled = pool(channels).squeeze(-1);

    // Concat with scalars → [batch, 196]
    auto combined = torch::cat({pooled, scalars}, 1);

    // FC layers
    combined = drop3(torch::relu(fc1(combined)));
    return fc2(combined);
}

// ClaimNet: CNN+FC hybrid
// Input: flat 450-dim vector, reshaped into 11 tile channels (34 wide) + 76 scalars
ClaimNetImpl::ClaimNetImpl() {
    conv1 = register_module("conv1", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(11, 64, 3).padding(1)));
    conv2 = register_module("conv2", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(64, 128, 3).padding(1)));
    conv3 = register_module("conv3", torch::nn::Conv1d(
        torch::nn::Conv1dOptions(128, 128, 3).padding(1)));
    pool = register_module("pool", torch::nn::AdaptiveAvgPool1d(1));
    fc1 = register_module("fc1", torch::nn::Linear(128 + 76, 256));
    fc2 = register_module("fc2", torch::nn::Linear(256, NUM_CLAIM_ACTIONS));
    drop1 = register_module("drop1", torch::nn::Dropout(0.1));
    drop2 = register_module("drop2", torch::nn::Dropout(0.1));
    drop3 = register_module("drop3", torch::nn::Dropout(0.1));
}

torch::Tensor ClaimNetImpl::forward(torch::Tensor x) {
    // x: [batch, 450]
    // Same 10 discard channels + 1 extra (claimed tile one-hot)
    auto ch0  = x.narrow(1, 0, 34);
    auto ch1  = x.narrow(1, 34, 34);
    auto ch2  = x.narrow(1, 68, 34);
    auto ch3  = x.narrow(1, 102, 34);
    auto ch4  = x.narrow(1, 136, 34);
    auto ch5  = x.narrow(1, 182, 34);
    auto ch6  = x.narrow(1, 216, 34);
    auto ch7  = x.narrow(1, 250, 34);
    auto ch8  = x.narrow(1, 284, 34);
    auto ch9  = x.narrow(1, 344, 34);
    auto ch10 = x.narrow(1, 408, 34);    // claimed tile one-hot

    // Stack → [batch, 11, 34]
    auto channels = torch::stack({ch0, ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8, ch9, ch10}, 1);

    // Extract scalar features → [batch, 76]
    auto s0 = x.narrow(1, 170, 12);     // opp meld types
    auto s1 = x.narrow(1, 318, 26);     // game context
    auto s2 = x.narrow(1, 378, 30);     // additional features
    auto s3 = x.narrow(1, 442, 8);      // claim mask + shanten after
    auto scalars = torch::cat({s0, s1, s2, s3}, 1);

    // Conv layers
    channels = drop1(torch::relu(conv1(channels)));
    channels = drop2(torch::relu(conv2(channels)));
    channels = torch::relu(conv3(channels));

    // Global average pool → squeeze → [batch, 128]
    auto pooled = pool(channels).squeeze(-1);

    // Concat with scalars → [batch, 204]
    auto combined = torch::cat({pooled, scalars}, 1);

    // FC layers
    combined = drop3(torch::relu(fc1(combined)));
    return fc2(combined);
}

#endif // HAS_TORCH
