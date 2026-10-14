#include "asset_decode_bridge.h"

#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "khdays/assets/audio.h"
#include "khdays/assets/cakp.h"
#include "khdays/assets/font.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/mdl0.h"
#include "khdays/assets/message.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/mods.h"
#include "khdays/assets/sdat.h"
#include "khdays/assets/tex0.h"

namespace khdays::platform::ps5 {

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("asset cannot be opened");
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("asset size cannot be read");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()
        && !input.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("asset read failed");
    }
    return bytes;
}

bool has_suffix(const std::filesystem::path& path, const char* suffix) {
    const auto text = path.filename().generic_string();
    const std::string ending(suffix);
    return text.size() >= ending.size()
        && text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
}

}  // namespace

AssetDecodeBridge::AssetDecodeBridge(std::filesystem::path root)
    : root_(std::filesystem::absolute(std::move(root)).lexically_normal()) {}

AssetDecodeSummary AssetDecodeBridge::inspect(
    const std::filesystem::path& relative_path) const {
    AssetDecodeSummary result;
    result.path = relative_path.generic_string();
    const auto path = (root_ / relative_path).lexically_normal();

    try {
        if (has_suffix(path, ".p2")) {
            const auto archive = assets::load_p2_archive(path);
            result.kind = "P2";
            result.records = archive.subdbs.size();
            for (const auto& subdb : archive.subdbs) {
                result.samples += subdb.size();
            }
            result.valid = true;
            result.detail = "decoded message/container sub-databases";
        } else if (has_suffix(path, ".nftr")) {
            const auto font = assets::decode_nftr(path);
            result.kind = "NFTR";
            result.records = font.glyphs.size();
            result.width = font.cell_width;
            result.height = font.cell_height;
            result.valid = true;
            result.detail = "decoded glyph table";
        } else if (has_suffix(path, ".mods")) {
            const auto video = assets::parse_mods_header(path);
            result.kind = "MODS";
            result.records = video.frame_count;
            result.width = video.width;
            result.height = video.height;
            result.valid = true;
            result.detail = video.has_audio() ? "video with audio" : "video-only clip";
        } else if (has_suffix(path, ".sdat")) {
            const auto sdat = assets::open_sdat(path);
            result.kind = "SDAT";
            result.records = assets::sdat_sequence_count(*sdat);
            result.samples = assets::sdat_stream_count(*sdat);
            result.valid = true;
            result.detail = "indexed sequences and streams";
        } else if (has_suffix(path, ".NCLR.z") || has_suffix(path, ".NCLR")) {
            const auto palette = assets::decode_nclr(path);
            result.kind = "NCLR";
            result.records = palette.colors.size();
            result.valid = true;
            result.detail = "decoded 2D palette";
        } else if (has_suffix(path, ".NCGR.z") || has_suffix(path, ".NCGR")) {
            const auto tiles = assets::decode_ncgr(path);
            result.kind = "NCGR";
            result.records = tiles.tile_count;
            result.valid = true;
            result.detail = "decoded 2D tile graphics";
        } else if (has_suffix(path, ".NSCR.z") || has_suffix(path, ".NSCR")) {
            const auto map = assets::decode_nscr(path);
            result.kind = "NSCR";
            result.records = map.cells.size();
            result.width = map.width_tiles;
            result.height = map.height_tiles;
            result.valid = true;
            result.detail = "decoded 2D tilemap";
        } else if (path.filename() == "scr.z") {
            const auto script = assets::decode_ov012_movie_script(read_file(path));
            result.kind = "CAKP/MODS script";
            for (const auto& language : script.subtitles) {
                result.records += language.size();
            }
            result.valid = true;
            result.detail = script.movie_path;
        } else {
            const auto bytes = read_file(path);
            auto unpacked = bytes;
            if (!unpacked.empty() && (unpacked[0] == 0x10U || unpacked[0] == 0x11U)) {
                unpacked = assets::lz_decompress(unpacked);
            }
            const auto slots = assets::parse_slot_container(
                unpacked.data(), unpacked.size());
            if (slots.valid && !slots.slots[7].empty()) {
                const auto model = assets::decode_model_geometry(
                    slots.slots[7].front().data, slots.slots[7].front().size);
                result.kind = "KAPH/NSBMD";
                result.records = model.meshes.size();
                result.samples = assets::list_tex0_textures(
                    slots.slots[7].front().data,
                    slots.slots[7].front().size).size();
                result.valid = true;
                result.detail = "decoded model meshes and texture bindings";
            } else {
                try {
                    const auto model = assets::inspect_mdl0(path);
                    result.kind = "MDL0/NSBMD";
                    result.records = model.models.size();
                    result.valid = true;
                    result.detail = "decoded model and render command metadata";
                } catch (const std::exception&) {
                    const auto names = assets::list_tex0_textures(
                        bytes.data(), bytes.size());
                    if (!names.empty()) {
                        result.kind = "TEX0/NSBTX";
                        result.records = names.size();
                        result.valid = true;
                        result.detail = "indexed texture dictionary";
                    }
                }
            }
        }
    } catch (const std::exception& error) {
        result.detail = error.what();
    }
    return result;
}

}  // namespace khdays::platform::ps5
