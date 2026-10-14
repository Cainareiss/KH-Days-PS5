#include "save_store.h"

#include <fstream>

namespace khdays::platform::ps5 {

FileSaveStore::FileSaveStore(std::filesystem::path root)
    : root_(std::filesystem::absolute(std::move(root)).lexically_normal()) {}

bool FileSaveStore::path_for(const int slot, std::filesystem::path& path) const {
    if (slot < 0 || slot > 99) {
        return false;
    }
    path = root_ / ("slot" + std::to_string(slot) + ".bin");
    return true;
}

bool FileSaveStore::load(const int slot,
                         khdays::ps5::gameplay::SaveData& save) const {
    std::filesystem::path path;
    if (!path_for(slot, path)) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    return khdays::ps5::gameplay::decode_save(bytes, save);
}

bool FileSaveStore::save(const int slot,
                         const khdays::ps5::gameplay::SaveData& data) const {
    std::filesystem::path path;
    if (!path_for(slot, path)) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) {
        return false;
    }
    const auto bytes = khdays::ps5::gameplay::encode_save(data);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

}  // namespace khdays::platform::ps5
