#include "asset_decode_bridge.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: khdays-ps5-asset-smoke <nitrofs-root>\n";
        return 2;
    }
    const std::filesystem::path root = argv[1];
    const std::vector<std::filesystem::path> samples = {
        "db/db_en.p2",
        "text/font_eu_08.nftr",
        "snd/sound_data.sdat",
        "mv/802.mods",
        "op/scr.z",
        "ba/ch/ro/def.p.z",
        "UI/btl/bm_lo_bg000.NCLR.z",
        "UI/btl/bm_lo_bg002.NCGR.z",
    };
    khdays::platform::ps5::AssetDecodeBridge bridge(root);
    std::size_t valid = 0;
    for (const auto& sample : samples) {
        const auto report = bridge.inspect(sample);
        std::cout << report.path << " | " << report.kind << " | valid="
                  << (report.valid ? "yes" : "no") << " | records="
                  << report.records << " | " << report.detail << '\n';
        valid += report.valid ? 1U : 0U;
    }
    return valid == samples.size() ? 0 : 1;
}
