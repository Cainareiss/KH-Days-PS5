#pragma once

#include "khdays_ps5/gameplay_core.h"

#include <filesystem>

namespace khdays::platform::ps5 {

class FileSaveStore final {
public:
    explicit FileSaveStore(std::filesystem::path root);

    [[nodiscard]] bool load(int slot, khdays::ps5::gameplay::SaveData& save) const;
    bool save(int slot, const khdays::ps5::gameplay::SaveData& data) const;

private:
    [[nodiscard]] bool path_for(int slot, std::filesystem::path& path) const;
    std::filesystem::path root_;
};

}  // namespace khdays::platform::ps5
