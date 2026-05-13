#pragma once
#include "AssetResolver.h"

struct AAssetManager;

/// Android asset resolver.
/// For files that exist in the APK assets/ folder, the path is resolved
/// relative to the internal storage directory where they're extracted.
/// The app should extract assets on first launch.
class AndroidAssetResolver : public AssetResolver {
public:
    /// @param internalPath App's internal files directory (Context.getFilesDir())
    /// @param assetManager Android AAssetManager for reading from APK
    AndroidAssetResolver(const std::string& internalPath, AAssetManager* assetManager);

    std::string resolve(const std::string& relativePath) const override;

    /// Get the AAssetManager for direct asset reading
    AAssetManager* assetManager() const { return assetManager_; }

private:
    std::string internalPath_;
    AAssetManager* assetManager_;
};
