#include "AndroidAssetResolver.h"

AndroidAssetResolver::AndroidAssetResolver(const std::string& internalPath,
                                           AAssetManager* assetManager)
    : internalPath_(internalPath)
    , assetManager_(assetManager)
{
    if (!internalPath_.empty() && internalPath_.back() != '/') {
        internalPath_ += '/';
    }
}

std::string AndroidAssetResolver::resolve(const std::string& relativePath) const {
    // Assets are extracted to internal storage on first launch.
    // Return the path in internal storage.
    return internalPath_ + relativePath;
}
