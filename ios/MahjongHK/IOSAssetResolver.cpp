#include "IOSAssetResolver.h"

IOSAssetResolver::IOSAssetResolver(const std::string& bundlePath)
    : bundlePath_(bundlePath)
{
    if (!bundlePath_.empty() && bundlePath_.back() != '/') {
        bundlePath_ += '/';
    }
}

std::string IOSAssetResolver::resolve(const std::string& relativePath) const {
    return bundlePath_ + relativePath;
}
