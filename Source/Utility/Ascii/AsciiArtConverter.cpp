#include "Utility/Ascii/AsciiArtConverter.h"
#include "Utility/stb_image.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Zenvra::Utility::Ascii {

namespace {

constexpr std::string_view RAMP_STANDARD = " .':-=+*#%@";
constexpr std::string_view RAMP_DETAILED =
    " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
constexpr std::string_view RAMP_BLOCKS = " .:-=+*#%@";
constexpr std::string_view RAMP_MINIMAL = " .oO@";

std::mutex g_cache_mutex;
std::unordered_map<std::string, AsciiArtResult> g_ascii_cache;

} // namespace

std::string_view
AsciiArtConverter::get_ramp_characters(AsciiRamp ramp) noexcept {
  switch (ramp) {
  case AsciiRamp::Detailed:
    return RAMP_DETAILED;
  case AsciiRamp::Blocks:
    return RAMP_BLOCKS;
  case AsciiRamp::Minimal:
    return RAMP_MINIMAL;
  case AsciiRamp::Standard:
  default:
    return RAMP_STANDARD;
  }
}

std::filesystem::path
AsciiArtConverter::resolve_image_path(const std::string &input_path) {
  if (input_path.empty()) {
    return {};
  }

  std::error_code ec;
  std::filesystem::path p{input_path};
  if (p.is_absolute() && std::filesystem::is_regular_file(p, ec)) {
    return p;
  }
  if (std::filesystem::is_regular_file(p, ec)) {
    return std::filesystem::absolute(p, ec);
  }

  // Normalize slashes
  std::string normalized = input_path;
  for (char &c : normalized) {
    if (c == '\\')
      c = '/';
  }

  std::string clean = normalized;
  if (clean.starts_with("Assets/icons/"))
    clean = clean.substr(13);
  else if (clean.starts_with("Assets/"))
    clean = clean.substr(7);
  else if (clean.starts_with("Resources/icons/"))
    clean = clean.substr(16);
  else if (clean.starts_with("Resources/"))
    clean = clean.substr(10);
  else if (clean.starts_with("icons/"))
    clean = clean.substr(6);

  std::vector<std::string> subpaths;
  const std::string fn = std::filesystem::path(input_path).filename().string();
  subpaths.push_back(normalized);
  if (clean != normalized) subpaths.push_back(clean);
  subpaths.push_back("Resources/icons/" + fn);
  subpaths.push_back("Assets/icons/" + fn);
  subpaths.push_back("Resources/" + fn);
  subpaths.push_back("Assets/" + fn);
  subpaths.push_back("icons/" + fn);
  subpaths.push_back(fn);

  // 1. Try executable directory first (Crucial for Inno Setup installer & Desktop/Start Menu shortcuts)
#ifdef _WIN32
  std::array<wchar_t, 4096> exe_buf{};
  const DWORD len = GetModuleFileNameW(nullptr, exe_buf.data(), static_cast<DWORD>(exe_buf.size()));
  if (len > 0) {
    std::filesystem::path exe_dir = std::filesystem::path(exe_buf.data()).parent_path();
    for (int i = 0; i < 6 && !exe_dir.empty(); ++i) {
      for (const auto &sub : subpaths) {
        const auto candidate = exe_dir / sub;
        if (std::filesystem::is_regular_file(candidate, ec)) {
          return candidate;
        }
      }
      if (!exe_dir.has_parent_path() || exe_dir == exe_dir.parent_path())
        break;
      exe_dir = exe_dir.parent_path();
    }
  }
#elif defined(__APPLE__)
  uint32_t buf_size = 0;
  _NSGetExecutablePath(nullptr, &buf_size);
  if (buf_size > 0) {
    std::vector<char> buf(buf_size);
    if (_NSGetExecutablePath(buf.data(), &buf_size) == 0) {
      std::filesystem::path exe_dir = std::filesystem::path(buf.data()).parent_path();
      for (int i = 0; i < 6 && !exe_dir.empty(); ++i) {
        for (const auto &sub : subpaths) {
          const auto candidate = exe_dir / sub;
          if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
          }
        }
        if (!exe_dir.has_parent_path() || exe_dir == exe_dir.parent_path())
          break;
        exe_dir = exe_dir.parent_path();
      }
    }
  }
#elif defined(__linux__)
  std::error_code proc_ec;
  std::filesystem::path exe_symlink = "/proc/self/exe";
  if (std::filesystem::exists(exe_symlink, proc_ec)) {
    std::filesystem::path exe_dir = std::filesystem::read_symlink(exe_symlink, proc_ec).parent_path();
    for (int i = 0; i < 6 && !exe_dir.empty(); ++i) {
      for (const auto &sub : subpaths) {
        const auto candidate = exe_dir / sub;
        if (std::filesystem::is_regular_file(candidate, ec)) {
          return candidate;
        }
      }
      if (!exe_dir.has_parent_path() || exe_dir == exe_dir.parent_path())
        break;
      exe_dir = exe_dir.parent_path();
    }
  }
#endif

  // 2. Try current working directory and ancestors up to 6 levels
  auto current_dir = std::filesystem::current_path(ec);
  for (int i = 0; i < 6 && !current_dir.empty(); ++i) {
    for (const auto &sub : subpaths) {
      const auto candidate = current_dir / sub;
      if (std::filesystem::is_regular_file(candidate, ec)) {
        return candidate;
      }
    }
    const auto parent = current_dir.parent_path();
    if (parent == current_dir) {
      break;
    }
    current_dir = parent;
  }

  return {};
}

AsciiArtResult
AsciiArtConverter::convert_raw_pixels(const uint8_t *rgba_data, int img_width,
                                      int img_height,
                                      const AsciiConvertOptions &options) {
  AsciiArtResult result;
  if (!rgba_data || img_width <= 0 || img_height <= 0) {
    return result;
  }

  const int target_w = std::clamp(options.target_width, 4, 300);
  int target_h = options.target_height;
  if (target_h <= 0) {
    // Monospace characters are about twice as tall as they are wide.
    // Aspect adjustment: target_h = (img_height / img_width) * target_w *
    // char_aspect
    const float aspect =
        static_cast<float>(img_height) / static_cast<float>(img_width);
    target_h = std::max(2, static_cast<int>(std::round(target_w * aspect *
                                                       options.char_aspect)));
  }
  target_h = std::clamp(target_h, 2, 200);

  result.width = target_w;
  result.height = target_h;
  result.cells.resize(static_cast<std::size_t>(target_w * target_h));

  const std::string_view ramp = get_ramp_characters(options.ramp);
  const std::size_t ramp_len = ramp.size();

  std::ostringstream text_builder;

  for (int y = 0; y < target_h; ++y) {
    const float src_y0 =
        (static_cast<float>(y) * static_cast<float>(img_height)) /
        static_cast<float>(target_h);
    const float src_y1 =
        (static_cast<float>(y + 1) * static_cast<float>(img_height)) /
        static_cast<float>(target_h);
    const int iy0 =
        std::clamp(static_cast<int>(std::floor(src_y0)), 0, img_height - 1);
    const int iy1 =
        std::clamp(static_cast<int>(std::ceil(src_y1)), iy0 + 1, img_height);

    for (int x = 0; x < target_w; ++x) {
      const float src_x0 =
          (static_cast<float>(x) * static_cast<float>(img_width)) /
          static_cast<float>(target_w);
      const float src_x1 =
          (static_cast<float>(x + 1) * static_cast<float>(img_width)) /
          static_cast<float>(target_w);
      const int ix0 =
          std::clamp(static_cast<int>(std::floor(src_x0)), 0, img_width - 1);
      const int ix1 =
          std::clamp(static_cast<int>(std::ceil(src_x1)), ix0 + 1, img_width);

      // Area average
      uint64_t sum_r = 0;
      uint64_t sum_g = 0;
      uint64_t sum_b = 0;
      uint64_t sum_a = 0;
      uint64_t pixel_count = 0;

      for (int sy = iy0; sy < iy1; ++sy) {
        const int row_offset = sy * img_width * 4;
        for (int sx = ix0; sx < ix1; ++sx) {
          const int idx = row_offset + sx * 4;
          sum_r += rgba_data[idx];
          sum_g += rgba_data[idx + 1];
          sum_b += rgba_data[idx + 2];
          sum_a += rgba_data[idx + 3];
          ++pixel_count;
        }
      }

      if (pixel_count == 0) {
        pixel_count = 1;
      }

      const auto avg_r = static_cast<uint8_t>(sum_r / pixel_count);
      const auto avg_g = static_cast<uint8_t>(sum_g / pixel_count);
      const auto avg_b = static_cast<uint8_t>(sum_b / pixel_count);
      const auto avg_a = static_cast<uint8_t>(sum_a / pixel_count);

      AsciiCell cell;
      cell.r = avg_r;
      cell.g = avg_g;
      cell.b = avg_b;
      cell.a = avg_a;

      if (avg_a < options.alpha_threshold) {
        cell.character = ' ';
      } else {
        // Perceptual luminance (Rec. 709)
        const float lum = 0.2126F * static_cast<float>(avg_r) +
                          0.7152F * static_cast<float>(avg_g) +
                          0.0722F * static_cast<float>(avg_b);
        // Saturation / Chrominance presence (vital for vibrant blue/cyan/purple
        // logos)
        const float max_c = static_cast<float>(std::max({avg_r, avg_g, avg_b}));
        const float presence = std::max(lum, max_c * 0.82F);

        // Alpha factor
        const float effective = (presence * static_cast<float>(avg_a)) / 255.0F;

        // Gamma curve (0.80) to lift subtle details and contours
        const float normalized = std::clamp(effective / 255.0F, 0.0F, 1.0F);
        const float gamma_corrected = std::pow(normalized, 0.80F);

        int ramp_idx = static_cast<int>(
            std::round(gamma_corrected * static_cast<float>(ramp_len - 1)));
        ramp_idx = std::clamp(ramp_idx, 0, static_cast<int>(ramp_len - 1));

        if (options.invert) {
          ramp_idx = static_cast<int>(ramp_len - 1) - ramp_idx;
        }
        cell.character = ramp[static_cast<std::size_t>(ramp_idx)];
      }

      result.cells[static_cast<std::size_t>(y * target_w + x)] = cell;
      text_builder << cell.character;
    }
    if (y + 1 < target_h) {
      text_builder << '\n';
    }
  }

  result.text = text_builder.str();
  return result;
}

AsciiArtResult
AsciiArtConverter::convert_image_file(const std::string &file_path,
                                      const AsciiConvertOptions &options) {
  const auto resolved = resolve_image_path(file_path);
  if (resolved.empty()) {
    return {};
  }

  const std::string cache_key =
      resolved.string() + "#" + std::to_string(options.target_width) + "x" +
      std::to_string(options.target_height) + "#" +
      std::to_string(static_cast<int>(options.ramp)) + "#" +
      std::to_string(options.colored) + "#" + std::to_string(options.invert);

  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    const auto it = g_ascii_cache.find(cache_key);
    if (it != g_ascii_cache.end()) {
      return it->second;
    }
  }

  int img_w = 0;
  int img_h = 0;
  int channels = 0;
  unsigned char *raw_pixels =
      stbi_load(resolved.string().c_str(), &img_w, &img_h, &channels, 4);
  if (!raw_pixels) {
    return {};
  }

  const auto result = convert_raw_pixels(raw_pixels, img_w, img_h, options);
  stbi_image_free(raw_pixels);

  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    g_ascii_cache[cache_key] = result;
  }

  return result;
}

std::string AsciiArtConverter::to_plain_string(const AsciiArtResult &art) {
  return art.text;
}

void AsciiArtConverter::clear_cache() {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_ascii_cache.clear();
}

} // namespace Zenvra::Utility::Ascii
