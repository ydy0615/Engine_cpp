#include <png.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct Rect {
    int left;
    int top;
    int right;
    int bottom;

    int width() const { return right - left; }
    int height() const { return bottom - top; }
};

struct Pixel {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;

    bool operator==(const Pixel& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator<(const Pixel& other) const {
        return std::tie(r, g, b, a) < std::tie(other.r, other.g, other.b, other.a);
    }
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    Pixel pixel_at(int x, int y) const {
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U +
                                  static_cast<std::size_t>(x) * 4U;
        return Pixel{rgba[index], rgba[index + 1], rgba[index + 2], rgba[index + 3]};
    }
};

struct Options {
    fs::path source = "2D_pixel";
    fs::path output = "2D_data";
    int min_area = 1;
    int alpha_threshold = 0;
};

constexpr int kConnectedFallbackMinPixels = 4096;

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [source] [output] [--min-area N] [--alpha-threshold N]\n";
}

Options parse_args(int argc, char** argv) {
    Options options;
    std::vector<std::string> positionals;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }

        if (arg == "--min-area" || arg == "--alpha-threshold") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value for option: " + arg);
            }
            const int value = std::stoi(argv[++index]);
            if (arg == "--min-area") {
                options.min_area = value;
            } else {
                options.alpha_threshold = value;
            }
            continue;
        }

        if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error("Unknown option: " + arg);
        }

        positionals.push_back(arg);
    }

    if (!positionals.empty()) {
        options.source = positionals[0];
    }
    if (positionals.size() > 1) {
        options.output = positionals[1];
    }
    if (positionals.size() > 2) {
        throw std::runtime_error("Too many positional arguments");
    }

    return options;
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool is_png_file(const fs::path& path) {
    return path.has_extension() && lower_copy(path.extension().string()) == ".png";
}

std::string sanitize_name(const std::string& value) {
    std::string cleaned;
    cleaned.reserve(value.size());

    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-') {
            cleaned.push_back(static_cast<char>(ch));
        } else {
            cleaned.push_back('_');
        }
    }

    while (!cleaned.empty() && (cleaned.front() == '.' || cleaned.front() == '_')) {
        cleaned.erase(cleaned.begin());
    }
    while (!cleaned.empty() && (cleaned.back() == '.' || cleaned.back() == '_')) {
        cleaned.pop_back();
    }

    return cleaned.empty() ? "asset" : cleaned;
}

std::uint64_t fnv1a_hash(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hex_digest(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(10) << (value & 0xFFFFFFFFFFULL);
    return stream.str();
}

std::string flatten_relpath(const fs::path& rel_path) {
    fs::path without_suffix = rel_path;
    without_suffix.replace_extension();

    std::string joined;
    for (const auto& part : without_suffix) {
        if (!joined.empty()) {
            joined += "__";
        }
        joined += part.string();
    }

    const std::string cleaned = sanitize_name(joined);
    if (cleaned.size() <= 160) {
        return cleaned;
    }

    return cleaned.substr(0, 120) + "_" + hex_digest(fnv1a_hash(rel_path.generic_string()));
}

std::vector<fs::path> iter_png_files(const fs::path& root) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && is_png_file(entry.path())) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

Image load_png(const fs::path& path) {
    FILE* file = std::fopen(path.string().c_str(), "rb");
    if (!file) {
        throw std::runtime_error("Failed to open PNG: " + path.string());
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        std::fclose(file);
        throw std::runtime_error("Failed to create png read struct");
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        std::fclose(file);
        throw std::runtime_error("Failed to create png info struct");
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        std::fclose(file);
        throw std::runtime_error("libpng failed while reading: " + path.string());
    }

    png_init_io(png_ptr, file);
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int bit_depth = 0;
    int color_type = 0;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png_ptr, info_ptr);

    Image image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4U);

    std::vector<png_bytep> rows(static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        rows[static_cast<std::size_t>(y)] = image.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4U;
    }

    png_read_image(png_ptr, rows.data());
    png_read_end(png_ptr, nullptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    std::fclose(file);
    return image;
}

void save_png(const fs::path& path, const Image& image) {
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) {
        throw std::runtime_error("Failed to open PNG for writing: " + path.string());
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        std::fclose(file);
        throw std::runtime_error("Failed to create png write struct");
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        std::fclose(file);
        throw std::runtime_error("Failed to create png write info struct");
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        std::fclose(file);
        throw std::runtime_error("libpng failed while writing: " + path.string());
    }

    png_init_io(png_ptr, file);
    png_set_IHDR(
        png_ptr,
        info_ptr,
        static_cast<png_uint_32>(image.width),
        static_cast<png_uint_32>(image.height),
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE,
        PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    std::vector<png_bytep> rows(static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        rows[static_cast<std::size_t>(y)] = const_cast<png_bytep>(
            image.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4U);
    }

    png_write_image(png_ptr, rows.data());
    png_write_end(png_ptr, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    std::fclose(file);
}

std::vector<std::uint8_t> build_foreground_mask(const Image& image, int alpha_threshold) {
    bool has_transparency = false;
    for (int y = 0; y < image.height && !has_transparency; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.pixel_at(x, y).a <= alpha_threshold) {
                has_transparency = true;
                break;
            }
        }
    }

    std::vector<std::uint8_t> mask(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), 0);
    if (has_transparency) {
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)] =
                    image.pixel_at(x, y).a > alpha_threshold ? 1U : 0U;
            }
        }
        return mask;
    }

    const std::array<Pixel, 4> corners = {
        image.pixel_at(0, 0),
        image.pixel_at(image.width - 1, 0),
        image.pixel_at(0, image.height - 1),
        image.pixel_at(image.width - 1, image.height - 1),
    };
    std::map<Pixel, int> counts;
    for (const Pixel& pixel : corners) {
        counts[pixel] += 1;
    }
    const Pixel background = std::max_element(counts.begin(), counts.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second < rhs.second;
    })->first;

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)] =
                image.pixel_at(x, y) == background ? 0U : 1U;
        }
    }
    return mask;
}

bool mask_at(const std::vector<std::uint8_t>& mask, int width, int x, int y) {
    return mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] != 0U;
}

std::vector<std::pair<int, int>> split_spans(const std::vector<int>& indices) {
    std::vector<std::pair<int, int>> spans;
    if (indices.empty()) {
        return spans;
    }

    int start = indices.front();
    int previous = indices.front();
    for (std::size_t i = 1; i < indices.size(); ++i) {
        if (indices[i] == previous + 1) {
            previous = indices[i];
            continue;
        }
        spans.emplace_back(start, previous + 1);
        start = previous = indices[i];
    }
    spans.emplace_back(start, previous + 1);
    return spans;
}

std::vector<Rect> split_by_empty_bands(const std::vector<std::uint8_t>& mask, int width, int height, const Rect& rect) {
    std::vector<int> active_rows;
    for (int y = rect.top; y < rect.bottom; ++y) {
        bool active = false;
        for (int x = rect.left; x < rect.right; ++x) {
            if (mask_at(mask, width, x, y)) {
                active = true;
                break;
            }
        }
        if (active) {
            active_rows.push_back(y);
        }
    }

    if (active_rows.empty()) {
        return {};
    }

    const auto row_spans = split_spans(active_rows);
    if (row_spans.size() > 1) {
        std::vector<Rect> parts;
        for (const auto& [top, bottom] : row_spans) {
            Rect sub_rect{rect.left, top, rect.right, bottom};
            auto sub_parts = split_by_empty_bands(mask, width, height, sub_rect);
            parts.insert(parts.end(), sub_parts.begin(), sub_parts.end());
        }
        return parts;
    }

    std::vector<int> active_cols;
    for (int x = rect.left; x < rect.right; ++x) {
        bool active = false;
        for (int y = rect.top; y < rect.bottom; ++y) {
            if (mask_at(mask, width, x, y)) {
                active = true;
                break;
            }
        }
        if (active) {
            active_cols.push_back(x);
        }
    }

    const auto col_spans = split_spans(active_cols);
    if (col_spans.size() > 1) {
        std::vector<Rect> parts;
        for (const auto& [left, right] : col_spans) {
            Rect sub_rect{left, rect.top, right, rect.bottom};
            auto sub_parts = split_by_empty_bands(mask, width, height, sub_rect);
            parts.insert(parts.end(), sub_parts.begin(), sub_parts.end());
        }
        return parts;
    }

    return {Rect{active_cols.front(), active_rows.front(), active_cols.back() + 1, active_rows.back() + 1}};
}

std::vector<Rect> split_by_connected_components(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height,
    const Rect& rect,
    int min_area) {
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    std::vector<Rect> boxes;

    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            if (visited[index] || !mask[index]) {
                continue;
            }

            std::queue<std::pair<int, int>> pending;
            pending.emplace(x, y);
            visited[index] = 1;

            int left = x;
            int right = x;
            int top = y;
            int bottom = y;
            int count = 0;

            while (!pending.empty()) {
                const auto [current_x, current_y] = pending.front();
                pending.pop();
                ++count;

                left = std::min(left, current_x);
                right = std::max(right, current_x);
                top = std::min(top, current_y);
                bottom = std::max(bottom, current_y);

                const std::array<std::pair<int, int>, 4> deltas = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                for (const auto& [dx, dy] : deltas) {
                    const int next_x = current_x + dx;
                    const int next_y = current_y + dy;
                    if (next_x < rect.left || next_x >= rect.right || next_y < rect.top || next_y >= rect.bottom) {
                        continue;
                    }
                    const std::size_t next_index =
                        static_cast<std::size_t>(next_y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(next_x);
                    if (visited[next_index] || !mask[next_index]) {
                        continue;
                    }
                    visited[next_index] = 1;
                    pending.emplace(next_x, next_y);
                }
            }

            if (count >= min_area) {
                boxes.push_back(Rect{left, top, right + 1, bottom + 1});
            }
        }
    }

    std::sort(boxes.begin(), boxes.end(), [](const Rect& lhs, const Rect& rhs) {
        return std::tie(lhs.top, lhs.left) < std::tie(rhs.top, rhs.left);
    });
    return boxes;
}

std::vector<Rect> detect_asset_regions(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height,
    int min_area,
    bool allow_component_fallback) {
    const Rect full_rect{0, 0, width, height};
    std::vector<Rect> coarse_regions = split_by_empty_bands(mask, width, height, full_rect);
    if (coarse_regions.size() > 1) {
        std::vector<Rect> filtered;
        for (const Rect& region : coarse_regions) {
            if (region.width() * region.height() >= min_area) {
                filtered.push_back(region);
            }
        }
        return filtered;
    }

    if (allow_component_fallback) {
        std::vector<Rect> connected = split_by_connected_components(mask, width, height, full_rect, min_area);
        if (connected.size() > 1) {
            return connected;
        }
    }

    return coarse_regions;
}

bool extract_sequence_prefix(const std::string& stem, std::string& prefix) {
    std::size_t split = stem.size();
    while (split > 0 && std::isdigit(static_cast<unsigned char>(stem[split - 1]))) {
        --split;
    }
    if (split == stem.size()) {
        return false;
    }

    prefix = stem.substr(0, split);
    while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == '-')) {
        prefix.pop_back();
    }
    if (prefix.empty()) {
        prefix = stem;
    }
    return true;
}

bool is_sequence_file(const fs::path& rel_path) {
    std::string prefix;
    return extract_sequence_prefix(rel_path.stem().string(), prefix);
}

std::optional<fs::path> resolve_sequence_dir(
    const fs::path& rel_path,
    const fs::path& output_root,
    std::map<std::string, fs::path>& group_registry) {
    std::string prefix;
    if (!extract_sequence_prefix(rel_path.stem().string(), prefix)) {
        return std::nullopt;
    }

    const std::string preferred = sanitize_name(prefix);
    const fs::path current_origin = rel_path.parent_path().lexically_normal();
    const auto found = group_registry.find(preferred);

    if (found == group_registry.end() || found->second == current_origin) {
        group_registry[preferred] = current_origin;
        return output_root / preferred;
    }

    return output_root / (sanitize_name(current_origin.generic_string()) + "__" + preferred);
}

std::vector<fs::path> target_files_for_source(
    const fs::path& rel_path,
    int region_count,
    const fs::path& output_root,
    std::map<std::string, fs::path>& group_registry) {
    std::vector<fs::path> targets;

    const auto sequence_dir = resolve_sequence_dir(rel_path, output_root, group_registry);
    if (sequence_dir.has_value()) {
        const std::string stem = sanitize_name(rel_path.stem().string());
        if (region_count == 1) {
            targets.push_back(*sequence_dir / (stem + ".png"));
            return targets;
        }
        for (int index = 1; index <= region_count; ++index) {
            std::ostringstream name;
            name << stem << "_part" << std::setw(3) << std::setfill('0') << index << ".png";
            targets.push_back(*sequence_dir / name.str());
        }
        return targets;
    }

    const std::string base_name = flatten_relpath(rel_path);
    if (region_count == 1) {
        targets.push_back(output_root / (base_name + ".png"));
        return targets;
    }
    for (int index = 1; index <= region_count; ++index) {
        std::ostringstream name;
        name << base_name << "_part" << std::setw(3) << std::setfill('0') << index << ".png";
        targets.push_back(output_root / name.str());
    }
    return targets;
}

Image crop_image(const Image& source, const Rect& rect) {
    Image cropped;
    cropped.width = rect.width();
    cropped.height = rect.height();
    cropped.rgba.resize(static_cast<std::size_t>(cropped.width) * static_cast<std::size_t>(cropped.height) * 4U);

    for (int y = 0; y < cropped.height; ++y) {
        for (int x = 0; x < cropped.width; ++x) {
            const Pixel pixel = source.pixel_at(rect.left + x, rect.top + y);
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(cropped.width) * 4U +
                                      static_cast<std::size_t>(x) * 4U;
            cropped.rgba[index] = pixel.r;
            cropped.rgba[index + 1] = pixel.g;
            cropped.rgba[index + 2] = pixel.b;
            cropped.rgba[index + 3] = pixel.a;
        }
    }
    return cropped;
}

void save_regions(const Image& image, const std::vector<Rect>& regions, const std::vector<fs::path>& targets) {
    for (std::size_t i = 0; i < regions.size(); ++i) {
        fs::create_directories(targets[i].parent_path());
        save_png(targets[i], crop_image(image, regions[i]));
    }
}

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        const fs::path source_root = fs::absolute(options.source);
        const fs::path output_root = fs::absolute(options.output);

        if (!fs::exists(source_root)) {
            std::cerr << "Source directory does not exist: " << source_root << '\n';
            return 1;
        }

        const std::vector<fs::path> png_files = iter_png_files(source_root);
        if (png_files.empty()) {
            std::cout << "No PNG files found under " << source_root << '\n';
            return 0;
        }

        std::map<std::string, fs::path> group_registry;
        int saved_assets = 0;
        int split_sources = 0;

        for (const fs::path& png_path : png_files) {
            const fs::path rel_path = fs::relative(png_path, source_root);
            const Image image = load_png(png_path);
            const std::vector<std::uint8_t> mask = build_foreground_mask(image, options.alpha_threshold);
            const bool allow_component_fallback =
                !is_sequence_file(rel_path) && image.width * image.height >= kConnectedFallbackMinPixels;
            const std::vector<Rect> regions =
                detect_asset_regions(mask, image.width, image.height, options.min_area, allow_component_fallback);
            if (regions.empty()) {
                continue;
            }

            const std::vector<fs::path> targets =
                target_files_for_source(rel_path, static_cast<int>(regions.size()), output_root, group_registry);
            save_regions(image, regions, targets);
            saved_assets += static_cast<int>(targets.size());
            if (regions.size() > 1) {
                ++split_sources;
            }
        }

        std::cout << "Processed " << png_files.size() << " PNG files\n";
        std::cout << "Saved " << saved_assets << " assets into " << output_root << "\n";
        std::cout << "Split " << split_sources << " source images into multiple assets\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}