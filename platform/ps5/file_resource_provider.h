#pragma once

#include "khdays_ps5/frontend_core.h"

#include <filesystem>

namespace khdays::platform::ps5 {

// Reads the extracted /app0 data tree without allowing an asset path to escape
// the title root. Paths in the manifest always use '/' separators.
class FileResourceProvider final : public khdays::ps5::frontend::ResourceProvider {
public:
    explicit FileResourceProvider(std::filesystem::path root);

    [[nodiscard]] bool contains(std::string_view path) const override;
    bool read(std::string_view path,
              std::vector<std::uint8_t>& bytes) const override;

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

private:
    [[nodiscard]] bool resolve(std::string_view path,
                                std::filesystem::path& result) const;
    std::filesystem::path root_;
};

}  // namespace khdays::platform::ps5
