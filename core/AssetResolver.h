#pragma once
#include <string>

// Abstract interface for platform-specific asset path resolution.
// Desktop: filesystem paths via ASSETS_PATH macro
// iOS: NSBundle resource paths
// Android: AAssetManager paths
class AssetResolver {
public:
    virtual ~AssetResolver() = default;

    // Resolve a relative asset path to a full platform path.
    // e.g., "fonts/NotoSansTC-Regular.ttf" → "/path/to/assets/fonts/NotoSansTC-Regular.ttf"
    virtual std::string resolve(const std::string& relativePath) const = 0;

    // Convenience: resolve and return as C string (valid until next call)
    const char* resolveC(const std::string& relativePath) {
        lastResolved_ = resolve(relativePath);
        return lastResolved_.c_str();
    }

private:
    std::string lastResolved_;
};

// Desktop implementation: prepends ASSETS_PATH to relative paths
class DesktopAssetResolver : public AssetResolver {
public:
    explicit DesktopAssetResolver(const std::string& assetsPath)
        : basePath_(assetsPath) {
        // Ensure trailing slash
        if (!basePath_.empty() && basePath_.back() != '/') {
            basePath_ += '/';
        }
    }

    std::string resolve(const std::string& relativePath) const override {
        return basePath_ + relativePath;
    }

private:
    std::string basePath_;
};
