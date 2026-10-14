#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace khdays::platform::ps5 {

// Format-neutral result consumed by the PS5 resource cache. The concrete
// Nintendo DS decoders stay outside the gameplay core; this is the seam where
// their decoded metadata will be handed to the Vulkan/audio/video backends.
struct AssetDecodeSummary final {
    std::string path;
    std::string kind;
    bool valid = false;
    std::size_t records = 0;
    int width = 0;
    int height = 0;
    std::size_t samples = 0;
    std::string detail;
};

class AssetDecodeBridge final {
public:
    explicit AssetDecodeBridge(std::filesystem::path root);

    [[nodiscard]] AssetDecodeSummary inspect(
        const std::filesystem::path& relative_path) const;

private:
    std::filesystem::path root_;
};

}  // namespace khdays::platform::ps5
