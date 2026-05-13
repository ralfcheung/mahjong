#pragma once
#include "AssetResolver.h"

/// iOS asset resolver that resolves paths relative to the app bundle.
class IOSAssetResolver : public AssetResolver {
public:
    explicit IOSAssetResolver(const std::string& bundlePath);
    std::string resolve(const std::string& relativePath) const override;

private:
    std::string bundlePath_;
};
