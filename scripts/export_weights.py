#!/usr/bin/env python3
"""
Export PyTorch model weights to flat binary format for InferenceEngine.

Binary format (all values little-endian float32):
  For each conv layer in order:
    weight: out_channels × in_channels × kernel_size floats (row-major)
    bias:   out_channels floats
  For each linear layer in order:
    weight: out_features × in_features floats (row-major)
    bias:   out_features floats

Usage:
  python scripts/export_weights.py [model_dir]

  model_dir defaults to assets/model/
  Reads:  discard_net.pt, claim_net.pt
  Writes: discard_weights.bin, claim_weights.bin
"""

import sys
import os
import struct


def export_model(pt_path, bin_path, layer_names):
    """Export a PyTorch model to flat binary weights."""
    try:
        import torch
    except ImportError:
        print("Error: PyTorch not installed. Install with: pip install torch")
        sys.exit(1)

    if not os.path.exists(pt_path):
        print(f"  Skipping {pt_path} (not found)")
        return False

    print(f"  Loading {pt_path}")
    # Try TorchScript first (jit.save format), fall back to raw state_dict
    try:
        model = torch.jit.load(pt_path, map_location='cpu')
        state_dict = model.state_dict()
    except Exception:
        state_dict = torch.load(pt_path, map_location='cpu', weights_only=False)

    total_params = 0
    with open(bin_path, 'wb') as f:
        for name in layer_names:
            weight_key = f"{name}.weight"
            bias_key = f"{name}.bias"

            if weight_key not in state_dict or bias_key not in state_dict:
                print(f"  Error: missing {weight_key} or {bias_key} in state dict")
                print(f"  Available keys: {list(state_dict.keys())}")
                return False

            weight = state_dict[weight_key].float().numpy()
            bias = state_dict[bias_key].float().numpy()

            if weight.ndim == 3:
                # Conv1d: [out_channels, in_channels, kernel_size]
                out_ch, in_ch, ksize = weight.shape
                print(f"    {name}: Conv1d({in_ch}, {out_ch}, kernel={ksize}) "
                      f"({weight.size + bias.size} params)")
            elif weight.ndim == 2:
                # Linear: [out_features, in_features]
                out_features, in_features = weight.shape
                print(f"    {name}: Linear({in_features}, {out_features}) "
                      f"({weight.size + bias.size} params)")
            else:
                print(f"  Error: unexpected weight shape {weight.shape} for {name}")
                return False

            # Write weight (already row-major in numpy)
            f.write(weight.tobytes())
            # Write bias vector
            f.write(bias.tobytes())

            total_params += weight.size + bias.size

    file_size = os.path.getsize(bin_path)
    print(f"  Wrote {bin_path} ({file_size} bytes, {total_params} params)")
    return True


def main():
    model_dir = sys.argv[1] if len(sys.argv) > 1 else "assets/model"

    if not os.path.isdir(model_dir):
        print(f"Model directory not found: {model_dir}")
        sys.exit(1)

    print(f"Exporting weights from {model_dir}/\n")

    # DiscardNet CNN: Conv(10→64,k3) → Conv(64→128,k3) → Conv(128→128,k3)
    #                → AvgPool → FC(196→256) → FC(256→34)
    print("DiscardNet:")
    export_model(
        os.path.join(model_dir, "discard_net.pt"),
        os.path.join(model_dir, "discard_weights.bin"),
        ["conv1", "conv2", "conv3", "fc1", "fc2"]
    )

    print()

    # ClaimNet CNN: Conv(11→64,k3) → Conv(64→128,k3) → Conv(128→128,k3)
    #              → AvgPool → FC(204→256) → FC(256→4)
    print("ClaimNet:")
    export_model(
        os.path.join(model_dir, "claim_net.pt"),
        os.path.join(model_dir, "claim_weights.bin"),
        ["conv1", "conv2", "conv3", "fc1", "fc2"]
    )

    print("\nDone!")


if __name__ == "__main__":
    main()
