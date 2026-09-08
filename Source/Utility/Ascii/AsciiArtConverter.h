#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Utility::Ascii
{

struct AsciiCell
{
    char character = ' ';
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

struct AsciiArtResult
{
    int width = 0;   // columns (character count per row)
    int height = 0;  // rows (line count)
    std::vector<AsciiCell> cells;
    std::string text; // plain text with newlines

    [[nodiscard]] bool is_valid() const noexcept
    {
        return width > 0 && height > 0 && !cells.empty();
    }

    [[nodiscard]] const AsciiCell* cell_at(int x, int y) const noexcept
    {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return nullptr;
        }
        return &cells[static_cast<std::size_t>(y * width + x)];
    }
};

enum class AsciiRamp
{
    Standard, // " .:-=+*#%@"
    Detailed, // " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"
    Blocks,   // " .:-=+*#%@" with solid blocks
    Minimal   // " .oO@"
};

struct AsciiConvertOptions
{
    int target_width = 44;     // columns
    int target_height = 22;    // rows (0 = auto based on aspect ratio)
    float char_aspect = 0.5F;  // monospace font width/height ratio (~0.5)
    AsciiRamp ramp = AsciiRamp::Standard;
    bool colored = true;
    bool invert = false;
    uint8_t alpha_threshold = 32; // pixels with alpha below this are considered empty space
};

class AsciiArtConverter
{
public:
    // Resolve asset path from multiple possible roots
    static std::filesystem::path resolve_image_path(const std::string& input_path);

    // Convert an image file on disk to ASCII art
    static AsciiArtResult convert_image_file(
        const std::string& file_path,
        const AsciiConvertOptions& options = {});

    // Convert raw 32-bit RGBA pixels in memory to ASCII art
    static AsciiArtResult convert_raw_pixels(
        const uint8_t* rgba_data,
        int img_width,
        int img_height,
        const AsciiConvertOptions& options = {});

    // Helper to get ramp string
    static std::string_view get_ramp_characters(AsciiRamp ramp) noexcept;

    // Convert result to plain formatted string
    static std::string to_plain_string(const AsciiArtResult& art);

    // Clear internal result cache
    static void clear_cache();
};

} // namespace Zenvra::Utility::Ascii
