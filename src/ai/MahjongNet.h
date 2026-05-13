#pragma once

#ifdef HAS_TORCH

#include <torch/torch.h>

// DiscardNet: CNN+FC hybrid
// Tile channels: [batch, 10, 34] → Conv1d(10→64→128→128) → AvgPool → concat scalars → FC(196→256→34)
struct DiscardNetImpl : torch::nn::Module {
    torch::nn::Conv1d conv1{nullptr}, conv2{nullptr}, conv3{nullptr};
    torch::nn::AdaptiveAvgPool1d pool{nullptr};
    torch::nn::Linear fc1{nullptr}, fc2{nullptr};
    torch::nn::Dropout drop1{nullptr}, drop2{nullptr}, drop3{nullptr};

    DiscardNetImpl();
    torch::Tensor forward(torch::Tensor x);
};
TORCH_MODULE(DiscardNet);

// ClaimNet: CNN+FC hybrid
// Tile channels: [batch, 11, 34] → Conv1d(11→64→128→128) → AvgPool → concat scalars → FC(204→256→4)
struct ClaimNetImpl : torch::nn::Module {
    torch::nn::Conv1d conv1{nullptr}, conv2{nullptr}, conv3{nullptr};
    torch::nn::AdaptiveAvgPool1d pool{nullptr};
    torch::nn::Linear fc1{nullptr}, fc2{nullptr};
    torch::nn::Dropout drop1{nullptr}, drop2{nullptr}, drop3{nullptr};

    ClaimNetImpl();
    torch::Tensor forward(torch::Tensor x);
};
TORCH_MODULE(ClaimNet);

#endif // HAS_TORCH
