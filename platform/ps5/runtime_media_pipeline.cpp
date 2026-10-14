#include "runtime_media_pipeline.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "khdays/assets/audio.h"
#include "khdays/assets/graphics2d.h"
#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h"
#include "khdays/assets/mods_video.h"
#include "khdays/assets/tex0.h"

namespace khdays::platform::ps5 {

namespace {

std::uint16_t read16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0] | (value[1] << 8U));
}

std::uint32_t read32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0])
        | (static_cast<std::uint32_t>(value[1]) << 8U)
        | (static_cast<std::uint32_t>(value[2]) << 16U)
        | (static_cast<std::uint32_t>(value[3]) << 24U);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open " + path.generic_string());
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot size " + path.generic_string());
    }
    std::vector<std::uint8_t> result(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!result.empty()
        && !input.read(reinterpret_cast<char*>(result.data()),
                       static_cast<std::streamsize>(result.size()))) {
        throw std::runtime_error("short read from " + path.generic_string());
    }
    return result;
}

std::optional<std::size_t> appended_data_size(
    const std::vector<std::uint8_t>& bytes) {
    for (std::size_t appended = 0; appended < 0x20U; appended += 4U) {
        if (bytes.size() < appended + 8U) {
            return std::nullopt;
        }
        const auto header = read32(bytes.data() + bytes.size() - appended - 8U);
        const auto header_size = static_cast<std::size_t>(header >> 24U);
        const auto compressed_size = static_cast<std::size_t>(header & 0x00ffffffU);
        if (header_size < 8U || header_size > bytes.size() - appended
            || compressed_size > bytes.size() - appended) {
            continue;
        }
        const auto padding_begin = bytes.size() - appended - header_size;
        const auto padding_end = bytes.size() - appended - 8U;
        if (std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(padding_begin),
                        bytes.begin() + static_cast<std::ptrdiff_t>(padding_end),
                        [](const std::uint8_t byte) { return byte == 0xffU; })) {
            return appended;
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> decompress_blz(
    const std::vector<std::uint8_t>& input) {
    const auto appended = appended_data_size(input);
    if (!appended.has_value()) {
        return input;
    }
    const auto core_size = input.size() - *appended;
    if (core_size < 8U) {
        throw std::runtime_error("invalid BLZ header");
    }
    const auto header = read32(input.data() + core_size - 8U);
    const auto extra_size = static_cast<std::size_t>(
        read32(input.data() + core_size - 4U));
    if (extra_size == 0U) {
        return input;
    }
    const auto header_size = static_cast<std::size_t>(header >> 24U);
    auto compressed_size = static_cast<std::size_t>(header & 0x00ffffffU);
    compressed_size = std::min(compressed_size, core_size);
    if (header_size > compressed_size) {
        throw std::runtime_error("invalid BLZ lengths");
    }
    const auto passthrough_size = core_size - compressed_size;
    const auto compressed_data_size = compressed_size - header_size;
    const auto output_size = core_size + extra_size - passthrough_size;
    std::vector<std::uint8_t> decoded(output_size);
    std::size_t written = 0;
    std::size_t consumed = 0;
    std::uint8_t flags = 0;
    std::uint8_t mask = 1;
    const auto byte_from_end = [&](const std::size_t index) {
        if (index >= compressed_data_size) {
            throw std::runtime_error("truncated BLZ stream");
        }
        return input[passthrough_size + compressed_data_size - 1U - index];
    };
    while (written < output_size) {
        if (mask == 1U) {
            flags = byte_from_end(consumed++);
            mask = 0x80U;
        } else {
            mask >>= 1U;
        }
        if ((flags & mask) == 0U) {
            decoded[output_size - 1U - written++] = byte_from_end(consumed++);
            continue;
        }
        const auto first = byte_from_end(consumed++);
        const auto second = byte_from_end(consumed++);
        const auto length = static_cast<std::size_t>((first >> 4U) + 3U);
        auto distance = static_cast<std::size_t>(
            ((static_cast<unsigned int>(first & 0x0fU) << 8U) | second) + 3U);
        if (distance > written) {
            if (written < 2U) {
                throw std::runtime_error("invalid BLZ back-reference");
            }
            distance = 2U;
        }
        for (std::size_t index = 0; index < length && written < output_size;
             ++index) {
            const auto source_from_end = written - distance;
            decoded[output_size - 1U - written] =
                decoded[output_size - 1U - source_from_end];
            ++written;
        }
    }
    std::vector<std::uint8_t> result;
    result.reserve(passthrough_size + decoded.size() + *appended);
    result.insert(result.end(), input.begin(),
                  input.begin() + static_cast<std::ptrdiff_t>(passthrough_size));
    result.insert(result.end(), decoded.begin(), decoded.end());
    result.insert(result.end(),
                  input.begin() + static_cast<std::ptrdiff_t>(core_size), input.end());
    return result;
}

std::vector<std::int16_t> resample_stereo_48k(
    const assets::DecodedAudio& audio) {
    if (audio.sample_rate == 0U || audio.channels == 0U
        || audio.samples.empty()) {
        return {};
    }
    const auto source_frames = audio.samples.size() / audio.channels;
    const auto output_frames = static_cast<std::size_t>(std::ceil(
        static_cast<double>(source_frames) * 48000.0 / audio.sample_rate));
    std::vector<std::int16_t> output(output_frames * 2U, 0);
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        const auto source_position = static_cast<double>(frame)
            * static_cast<double>(audio.sample_rate) / 48000.0;
        const auto first = std::min(static_cast<std::size_t>(source_position),
                                    source_frames - 1U);
        const auto second = std::min(first + 1U, source_frames - 1U);
        const auto fraction = source_position - static_cast<double>(first);
        for (std::size_t channel = 0; channel < 2U; ++channel) {
            const auto source_channel = std::min<std::size_t>(
                channel, static_cast<std::size_t>(audio.channels - 1U));
            const auto a = audio.samples[first * audio.channels + source_channel];
            const auto b = audio.samples[second * audio.channels + source_channel];
            output[frame * 2U + channel] = static_cast<std::int16_t>(
                std::lround(static_cast<double>(a)
                    + (static_cast<double>(b) - a) * fraction));
        }
    }
    return output;
}

}  // namespace

class RuntimeMediaPipeline::Impl final {
public:
    Impl(std::filesystem::path input_root, RuntimeMediaHooks input_hooks)
        : root(std::filesystem::absolute(std::move(input_root)).lexically_normal()),
          hooks(input_hooks) {}

    std::filesystem::path resolve(const std::filesystem::path& relative) const {
        if (relative.empty() || relative.is_absolute()) {
            throw std::runtime_error("asset path must be relative");
        }
        const auto result = (root / relative).lexically_normal();
        const auto root_text = root.generic_string();
        const auto result_text = result.generic_string();
        if (result_text.rfind(root_text + "/", 0) != 0) {
            throw std::runtime_error("asset path escapes data root");
        }
        return result;
    }

    assets::MobiClipCoefficientTables load_coefficient_tables() const {
        const auto table_blob = read_file(root / "system/arm9_overlay_table.bin");
        constexpr std::size_t kEntrySize = 32U;
        const std::uint8_t* entry = nullptr;
        for (std::size_t offset = 0; offset + kEntrySize <= table_blob.size();
             offset += kEntrySize) {
            if (read32(table_blob.data() + offset) == 24U) {
                entry = table_blob.data() + offset;
                break;
            }
        }
        if (entry == nullptr) {
            throw std::runtime_error("MobiClip overlay 24 is absent");
        }
        const auto ram_address = read32(entry + 4U);
        const auto ram_size = static_cast<std::size_t>(read32(entry + 8U));
        const auto file_id = read32(entry + 24U);
        std::ostringstream name;
        name << "file_" << std::setw(5) << std::setfill('0') << file_id << ".bin";
        const auto overlay = decompress_blz(read_file(root / "_unnamed" / name.str()));
        if (overlay.size() < ram_size) {
            throw std::runtime_error("MobiClip overlay is shorter than RAM size");
        }
        constexpr std::uint32_t kAddresses[2] = {0x0208a7c4U, 0x020886c4U};
        assets::MobiClipCoefficientTables tables{};
        for (std::size_t variant = 0; variant < 2U; ++variant) {
            if (kAddresses[variant] < ram_address) {
                throw std::runtime_error("MobiClip table precedes overlay RAM");
            }
            const auto offset = static_cast<std::size_t>(
                kAddresses[variant] - ram_address);
            if (offset + 0x2100U > overlay.size()) {
                throw std::runtime_error("MobiClip table exceeds overlay");
            }
            for (std::size_t index = 0; index < 4096U; ++index) {
                tables[variant].lookup[index] =
                    read16(overlay.data() + offset + index * 2U);
            }
            std::copy_n(overlay.data() + offset + 0x2000U, 256U,
                        tables[variant].residue.data());
        }
        return tables;
    }

    void queue_audio(const assets::DecodedAudio& audio) {
        auto converted = resample_stereo_48k(audio);
        const auto frames = converted.size() / 2U;
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            audio_samples.insert(audio_samples.end(), converted.begin(), converted.end());
        }
        media_stats.audio_frames_queued += frames;
    }

    void fill(std::int16_t* output, const int frame_count) {
        if (output == nullptr || frame_count <= 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(audio_mutex);
        const auto sample_count = static_cast<std::size_t>(frame_count) * 2U;
        for (std::size_t index = 0; index < sample_count; ++index) {
            if (audio_samples.empty()) {
                output[index] = 0;
            } else {
                output[index] = audio_samples.front();
                audio_samples.pop_front();
            }
        }
    }

    std::filesystem::path root;
    RuntimeMediaHooks hooks{};
    RuntimeMediaStats media_stats{};
    std::deque<std::int16_t> audio_samples;
    std::mutex audio_mutex;
    std::unique_ptr<assets::ModsVideoDecoder> video;
    std::int64_t video_frame_ns = 0;
    std::int64_t video_accumulator_ns = 0;
    bool video_started = false;
    AudioOutput* audio_output = nullptr;
};

RuntimeMediaPipeline::RuntimeMediaPipeline(std::filesystem::path data_root,
                                           RuntimeMediaHooks hooks)
    : impl_(std::make_unique<Impl>(std::move(data_root), hooks)) {}

RuntimeMediaPipeline::~RuntimeMediaPipeline() {
    stop_audio();
}

RuntimeMediaPipeline::RuntimeMediaPipeline(RuntimeMediaPipeline&&) noexcept = default;
RuntimeMediaPipeline& RuntimeMediaPipeline::operator=(
    RuntimeMediaPipeline&&) noexcept = default;

bool RuntimeMediaPipeline::upload_kaph_model(
    const std::filesystem::path& relative_path, std::string* error) {
    try {
        if (impl_->hooks.upload_texture == nullptr
            || impl_->hooks.upload_mesh == nullptr) {
            throw std::runtime_error("Vulkan upload hooks are unavailable");
        }
        auto bytes = read_file(impl_->resolve(relative_path));
        if (!bytes.empty() && (bytes[0] == 0x10U || bytes[0] == 0x11U)) {
            bytes = assets::lz_decompress(bytes);
        }
        const auto slots = assets::parse_slot_container(bytes.data(), bytes.size());
        if (!slots.valid || slots.slots[7].empty()) {
            throw std::runtime_error("KAPH has no slot-7 NSBMD model");
        }
        const auto model_data = slots.slots[7].front();
        const auto model = assets::decode_model_geometry(
            model_data.data, model_data.size);
        std::unordered_map<std::string, std::string> texture_keys;
        for (const auto& name : assets::list_tex0_textures(
                 model_data.data, model_data.size)) {
            const auto texture = assets::load_tex0_texture(
                model_data.data, model_data.size, name);
            const auto key = relative_path.generic_string() + "#texture:" + name;
            if (!impl_->hooks.upload_texture(
                    impl_->hooks.user, key.c_str(),
                    static_cast<std::uint32_t>(texture.width),
                    static_cast<std::uint32_t>(texture.height),
                    texture.rgba.data(), texture.rgba.size())) {
                throw std::runtime_error("Vulkan texture upload failed: " + name);
            }
            texture_keys.emplace(name, key);
            ++impl_->media_stats.textures_uploaded;
        }
        for (std::size_t mesh_index = 0; mesh_index < model.meshes.size();
             ++mesh_index) {
            const auto& mesh = model.meshes[mesh_index];
            std::vector<RuntimeGpuVertex> vertices;
            vertices.reserve(mesh.vertices.size());
            for (const auto& vertex : mesh.vertices) {
                vertices.push_back(RuntimeGpuVertex{
                    vertex.position, vertex.texcoord, vertex.color,
                    vertex.joints, vertex.weights});
            }
            const auto key = relative_path.generic_string() + "#mesh:"
                + std::to_string(mesh_index) + ":" + mesh.name;
            const auto texture = texture_keys.find(mesh.texture_name);
            const char* texture_key = texture == texture_keys.end()
                ? "" : texture->second.c_str();
            if (!impl_->hooks.upload_mesh(
                    impl_->hooks.user, key.c_str(), vertices.data(), vertices.size(),
                    mesh.indices.data(), mesh.indices.size(), texture_key)) {
                throw std::runtime_error("Vulkan mesh upload failed: " + mesh.name);
            }
            ++impl_->media_stats.meshes_uploaded;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool RuntimeMediaPipeline::queue_sdat_stream(
    const std::filesystem::path& relative_path, const std::size_t stream_index,
    std::string* error) {
    try {
        const auto sdat = assets::open_sdat(impl_->resolve(relative_path));
        if (stream_index >= assets::sdat_stream_count(*sdat)) {
            throw std::runtime_error("SDAT stream index is out of range");
        }
        impl_->queue_audio(assets::decode_stream(*sdat, stream_index));
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool RuntimeMediaPipeline::open_mods_video(
    const std::filesystem::path& relative_path, std::string* error) {
    try {
        impl_->video = std::make_unique<assets::ModsVideoDecoder>(
            read_file(impl_->resolve(relative_path)),
            impl_->load_coefficient_tables());
        const auto fps = impl_->video->frames_per_second();
        if (!std::isfinite(fps) || fps <= 0.0) {
            throw std::runtime_error("MODS video has an invalid frame rate");
        }
        impl_->video_frame_ns = static_cast<std::int64_t>(
            std::llround(1'000'000'000.0 / fps));
        impl_->video_accumulator_ns = 0;
        impl_->video_started = false;
        return true;
    } catch (const std::exception& exception) {
        impl_->video.reset();
        impl_->video_frame_ns = 0;
        impl_->video_accumulator_ns = 0;
        impl_->video_started = false;
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool RuntimeMediaPipeline::advance_video(const std::int64_t elapsed_ns,
                                         std::string* error) {
    if (elapsed_ns < 0) {
        if (error != nullptr) {
            *error = "video elapsed time cannot be negative";
        }
        return false;
    }
    if (impl_->video == nullptr || impl_->video_frame_ns <= 0) {
        if (error != nullptr) {
            *error = "MODS video is not open";
        }
        return false;
    }
    if (!impl_->video_started) {
        if (!decode_next_video_frame(error)) {
            return false;
        }
        impl_->video_started = true;
    }
    if (elapsed_ns > std::numeric_limits<std::int64_t>::max()
            - impl_->video_accumulator_ns) {
        impl_->video_accumulator_ns = std::numeric_limits<std::int64_t>::max();
    } else {
        impl_->video_accumulator_ns += elapsed_ns;
    }
    while (!impl_->video->finished()
           && impl_->video_accumulator_ns >= impl_->video_frame_ns) {
        impl_->video_accumulator_ns -= impl_->video_frame_ns;
        if (!decode_next_video_frame(error)) {
            return false;
        }
    }
    return true;
}

bool RuntimeMediaPipeline::decode_next_video_frame(std::string* error) {
    try {
        if (impl_->video == nullptr) {
            throw std::runtime_error("MODS video is not open");
        }
        if (impl_->video->finished()) {
            throw std::runtime_error("MODS video is finished");
        }
        if (!impl_->video->decode_next(true)) {
            throw std::runtime_error("MODS frame decode failed");
        }
        const auto& frame = impl_->video->frame();
        if (impl_->hooks.present_video_frame == nullptr
            || !impl_->hooks.present_video_frame(
                impl_->hooks.user, impl_->video->frame_index(),
                static_cast<std::uint32_t>(frame.width),
                static_cast<std::uint32_t>(frame.height),
                frame.rgba.data(), frame.rgba.size())) {
            throw std::runtime_error("Vulkan video-frame upload failed");
        }
        impl_->queue_audio(impl_->video->audio_chunk());
        ++impl_->media_stats.video_frames_presented;
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool RuntimeMediaPipeline::start_audio(AudioOutput& output) {
    if (impl_->audio_output != nullptr) {
        return false;
    }
    if (!output.start(&RuntimeMediaPipeline::fill_audio, this)) {
        return false;
    }
    impl_->audio_output = &output;
    return true;
}

void RuntimeMediaPipeline::stop_audio() {
    if (impl_ != nullptr && impl_->audio_output != nullptr) {
        impl_->audio_output->stop();
        impl_->audio_output = nullptr;
    }
}

bool RuntimeMediaPipeline::video_finished() const {
    return impl_->video == nullptr || impl_->video->finished();
}

std::int64_t RuntimeMediaPipeline::video_frame_duration_ns() const {
    return impl_->video_frame_ns;
}

RuntimeMediaStats RuntimeMediaPipeline::stats() const {
    return impl_->media_stats;
}

void RuntimeMediaPipeline::fill_audio(std::int16_t* output,
                                      const int frame_count, void* user) {
    if (user != nullptr) {
        static_cast<RuntimeMediaPipeline*>(user)->impl_->fill(output, frame_count);
    }
}

}  // namespace khdays::platform::ps5
