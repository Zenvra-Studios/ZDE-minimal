#pragma once

#include "Utility/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace Zenvra::Utility
{

/// Decoded RGBA8 image (row-major, top-down).
struct DecodedImage
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels; ///< width * height * 4 (RGBA)
};

namespace detail
{

inline std::uint16_t read_u16_le(const unsigned char* data)
{
    return static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8U);
}

inline std::uint32_t read_u32_le(const unsigned char* data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U);
}

inline std::int32_t read_s32_le(const unsigned char* data)
{
    return static_cast<std::int32_t>(read_u32_le(data));
}

} // namespace detail

/// Decode the largest / highest quality icon embedded in a .ico/.cur file into top-down RGBA8.
[[nodiscard]] inline std::optional<DecodedImage> decode_ico_memory(const unsigned char* data_ptr, std::size_t data_size)
{
    if (data_ptr == nullptr || data_size < 22)
    {
        return std::nullopt;
    }

    // ICONDIR: reserved(2) = 0, type(2) = 1 (icon) or 2 (cursor), count(2).
    if (data_ptr[0] != 0 || data_ptr[1] != 0)
    {
        return std::nullopt;
    }
    const std::uint16_t icon_type = detail::read_u16_le(data_ptr + 2);
    if (icon_type != 1 && icon_type != 2)
    {
        return std::nullopt;
    }
    const std::uint16_t icon_count = detail::read_u16_le(data_ptr + 4);
    if (icon_count == 0)
    {
        return std::nullopt;
    }

    // ICONDIRENTRY (16 bytes each): pick the best entry (highest width, then bit depth).
    std::size_t best_index = 0;
    int best_width = -1;
    int best_bpp = -1;
    for (std::uint16_t index = 0; index < icon_count; ++index)
    {
        const std::size_t entry_offset = 6U + static_cast<std::size_t>(index) * 16U;
        if (entry_offset + 16U > data_size)
        {
            break;
        }
        const unsigned char raw_width = data_ptr[entry_offset];
        const int entry_width = raw_width == 0 ? 256 : static_cast<int>(raw_width);
        const std::uint16_t entry_bpp = detail::read_u16_le(data_ptr + entry_offset + 6);
        if (entry_width > best_width || (entry_width == best_width && static_cast<int>(entry_bpp) > best_bpp))
        {
            best_width = entry_width;
            best_bpp = static_cast<int>(entry_bpp);
            best_index = index;
        }
    }

    const std::size_t entry_offset = 6U + best_index * 16U;
    const std::size_t image_size = detail::read_u32_le(data_ptr + entry_offset + 8);
    const std::size_t image_offset = detail::read_u32_le(data_ptr + entry_offset + 12);
    if (image_offset >= data_size || image_size > data_size - image_offset || image_size == 0)
    {
        return std::nullopt;
    }
    const unsigned char* image_data = data_ptr + image_offset;

    // PNG-compressed entry: delegate to stb_image.
    static constexpr unsigned char png_magic[8]{
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (image_size >= 8 && std::memcmp(image_data, png_magic, 8) == 0)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load_from_memory(
            image_data,
            static_cast<int>(image_size),
            &width,
            &height,
            &channels,
            4);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            if (pixels != nullptr) stbi_image_free(pixels);
            return std::nullopt;
        }
        DecodedImage result;
        result.width = width;
        result.height = height;
        result.pixels.assign(
            pixels, pixels + static_cast<std::size_t>(width * height) * 4U);
        stbi_image_free(pixels);
        return result;
    }

    // Classic DIB entry (BITMAPINFOHEADER / OS2 BITMAPCOREHEADER).
    if (image_size < 40)
    {
        return std::nullopt;
    }
    const std::uint32_t header_size = detail::read_u32_le(image_data);
    if (header_size < 40 || header_size > image_size)
    {
        return std::nullopt;
    }

    const std::int32_t width = detail::read_s32_le(image_data + 4);
    const std::int32_t raw_height = detail::read_s32_le(image_data + 8);
    const std::uint16_t planes = detail::read_u16_le(image_data + 12);
    const std::uint16_t bit_count = detail::read_u16_le(image_data + 14);
    const std::uint32_t compression = detail::read_u32_le(image_data + 16);
    const std::uint32_t clr_used = detail::read_u32_le(image_data + 32);

    if (width <= 0 || raw_height == 0 || planes != 1 || compression != 0)
    {
        return std::nullopt;
    }

    const bool top_down = raw_height < 0;
    int height = std::abs(raw_height);
    bool has_and_mask = false;
    // In Windows ICO format, biHeight is the combined height of XOR and AND masks (height * 2)
    if (!top_down && height >= 2)
    {
        height /= 2;
        has_and_mask = true;
    }
    if (height <= 0)
    {
        return std::nullopt;
    }

    // Parse color palette if present (for 8-bit, 4-bit, 1-bit)
    std::size_t palette_colors = 0;
    if (bit_count <= 8)
    {
        palette_colors = (clr_used > 0 && clr_used <= (1U << bit_count))
            ? clr_used
            : (1U << bit_count);
    }
    const std::size_t palette_bytes = palette_colors * 4U;
    const unsigned char* palette_start = image_data + header_size;
    const unsigned char* pixel_start = palette_start + palette_bytes;

    const std::size_t stride = (static_cast<std::size_t>(width) * bit_count + 31U) / 32U * 4U;
    const std::size_t pixel_bytes = stride * static_cast<std::size_t>(height);
    const std::size_t mask_stride = (static_cast<std::size_t>(width) + 31U) / 32U * 4U;
    const std::size_t mask_bytes = has_and_mask ? (mask_stride * static_cast<std::size_t>(height)) : 0U;

    if (pixel_start + pixel_bytes + mask_bytes > image_data + image_size)
    {
        return std::nullopt;
    }

    const unsigned char* mask_start = pixel_start + pixel_bytes;

    DecodedImage result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width * height) * 4U);

    // Check if 32-bit DIB has a real non-zero alpha channel
    bool has_valid_32bit_alpha = false;
    if (bit_count == 32)
    {
        for (int y = 0; y < height && !has_valid_32bit_alpha; ++y)
        {
            const int src_row = top_down ? y : (height - 1 - y);
            const unsigned char* row = pixel_start + static_cast<std::size_t>(src_row) * stride;
            for (int x = 0; x < width; ++x)
            {
                if (row[static_cast<std::size_t>(x) * 4U + 3] > 0)
                {
                    has_valid_32bit_alpha = true;
                    break;
                }
            }
        }
    }

    for (int y = 0; y < height; ++y)
    {
        // DIB bitmaps are stored bottom-up (row 0 is bottom row)
        const int src_row = top_down ? y : (height - 1 - y);
        const unsigned char* row = pixel_start + static_cast<std::size_t>(src_row) * stride;
        const std::size_t mask_row_offset = static_cast<std::size_t>(src_row) * mask_stride;

        for (int x = 0; x < width; ++x)
        {
            const std::size_t out = static_cast<std::size_t>(y * width + x) * 4U;

            if (bit_count == 32)
            {
                result.pixels[out + 0] = row[static_cast<std::size_t>(x) * 4U + 2]; // R
                result.pixels[out + 1] = row[static_cast<std::size_t>(x) * 4U + 1]; // G
                result.pixels[out + 2] = row[static_cast<std::size_t>(x) * 4U + 0]; // B
                result.pixels[out + 3] = has_valid_32bit_alpha
                    ? row[static_cast<std::size_t>(x) * 4U + 3]
                    : 255;
            }
            else if (bit_count == 24)
            {
                result.pixels[out + 0] = row[static_cast<std::size_t>(x) * 3U + 2];
                result.pixels[out + 1] = row[static_cast<std::size_t>(x) * 3U + 1];
                result.pixels[out + 2] = row[static_cast<std::size_t>(x) * 3U + 0];
                result.pixels[out + 3] = 255;
            }
            else if (bit_count == 8)
            {
                const unsigned char idx = row[x];
                if (static_cast<std::size_t>(idx) < palette_colors)
                {
                    const unsigned char* entry = palette_start + static_cast<std::size_t>(idx) * 4U;
                    result.pixels[out + 0] = entry[2];
                    result.pixels[out + 1] = entry[1];
                    result.pixels[out + 2] = entry[0];
                }
                result.pixels[out + 3] = 255;
            }
            else if (bit_count == 4)
            {
                const unsigned char byte = row[x / 2];
                const unsigned char idx = (x % 2 == 0) ? (byte >> 4U) : (byte & 0x0FU);
                if (static_cast<std::size_t>(idx) < palette_colors)
                {
                    const unsigned char* entry = palette_start + static_cast<std::size_t>(idx) * 4U;
                    result.pixels[out + 0] = entry[2];
                    result.pixels[out + 1] = entry[1];
                    result.pixels[out + 2] = entry[0];
                }
                result.pixels[out + 3] = 255;
            }
            else if (bit_count == 1)
            {
                const unsigned char byte = row[x / 8];
                const unsigned char idx = (byte >> (7U - (x % 8))) & 1U;
                if (static_cast<std::size_t>(idx) < palette_colors)
                {
                    const unsigned char* entry = palette_start + static_cast<std::size_t>(idx) * 4U;
                    result.pixels[out + 0] = entry[2];
                    result.pixels[out + 1] = entry[1];
                    result.pixels[out + 2] = entry[0];
                }
                result.pixels[out + 3] = 255;
            }

            // Apply 1-bit AND mask only if 32-bit alpha channel is not used
            if (has_and_mask && (!has_valid_32bit_alpha || bit_count != 32))
            {
                const std::size_t mask_index = mask_row_offset + static_cast<std::size_t>(x) / 8U;
                const bool mask_bit_set =
                    ((mask_start[mask_index] >> (7U - (static_cast<std::size_t>(x) % 8U))) & 1U) != 0U;
                if (mask_bit_set)
                {
                    result.pixels[out + 3] = 0;
                }
            }
        }
    }

    return result;
}

[[nodiscard]] inline std::optional<DecodedImage> decode_ico_file(std::string_view path)
{
    FILE* file = std::fopen(path.data(), "rb");
    if (file == nullptr)
    {
        return std::nullopt;
    }
    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size < 22)
    {
        std::fclose(file);
        return std::nullopt;
    }
    std::vector<unsigned char> data(static_cast<std::size_t>(file_size));
    const std::size_t bytes_read = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    if (bytes_read != data.size())
    {
        return std::nullopt;
    }
    return decode_ico_memory(data.data(), data.size());
}
} // namespace Zenvra::Utility
