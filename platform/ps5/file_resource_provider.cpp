#include "file_resource_provider.h"

#include <fstream>

namespace khdays::platform::ps5 {

FileResourceProvider::FileResourceProvider(std::filesystem::path root)
    : root_(std::filesystem::absolute(std::move(root)).lexically_normal()) {}

bool FileResourceProvider::resolve(const std::string_view path,
                                   std::filesystem::path& result) const {
    if (path.empty()) {
        return false;
    }
    const std::filesystem::path relative(path);
    if (relative.is_absolute()) {
        return false;
    }
    const auto candidate = (root_ / relative).lexically_normal();
    const auto root_text = root_.generic_string();
    const auto candidate_text = candidate.generic_string();
    if (candidate_text != root_text
        && candidate_text.rfind(root_text + "/", 0) != 0) {
        return false;
    }
    result = candidate;
    return true;
}

bool FileResourceProvider::contains(const std::string_view path) const {
    std::filesystem::path file;
    return resolve(path, file) && std::filesystem::is_regular_file(file);
}

bool FileResourceProvider::read(const std::string_view path,
                                std::vector<std::uint8_t>& bytes) const {
    bytes.clear();
    std::filesystem::path file;
    if (!resolve(path, file) || !std::filesystem::is_regular_file(file)) {
        return false;
    }
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const auto size = input.tellg();
    if (size < 0) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    return bytes.empty()
        || static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()),
                                        static_cast<std::streamsize>(bytes.size())));
}

}  // namespace khdays::platform::ps5
