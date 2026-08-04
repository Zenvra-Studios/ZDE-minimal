#pragma once

#include "Utility/stb_image.h"

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

} // namespace detail

/// Decode the largest icon embedded in a .ico/.cur file into top-down RGBA8.
///
/// Handles both PNG-compressed entries (delegated to stb_image) and classic
/// BITMAPINFOHEADER entries with 32/24-bit uncompressed pixels plus the
/// trailing 1-bit AND mask.
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

    // ICONDIR: reserved(2) = 0, type(2) = 1 (icon), count(2).
    if (data[0] != 0 || data[1] != 0 ||
        detail::read_u16_le(data.data() + 2) != 1)
    {
        return std::nullopt;
    }
    const std::uint16_t icon_count = detail::read_u16_le(data.data() + 4);
    if (icon_count == 0)
    {
        return std::nullopt;
    }

    // ICONDIRENTRY (16 bytes each): pick the largest entry.
    std::size_t best_index = 0;
    int best_width = 0;
    for (std::uint16_t index = 0; index < icon_count; ++index)
    {
        const std::size_t entry_offset = 6U + static_cast<std::size_t>(index) * 16U;
        if (entry_offset + 16U > data.size())
        {
            break;
        }
        const unsigned char raw_width = data[entry_offset];
        const int entry_width = raw_width == 0 ? 256 : static_cast<int>(raw_width);
        if (entry_width > best_width)
        {
            best_width = entry_width;
            best_index = index;
        }
    }

    const std::size_t entry_offset = 6U + best_index * 16U;
    // ICONDIRENTRY: dwBytesInRes at +8, dwImageOffset at +12.
    const std::size_t image_size =
        detail::read_u32_le(data.data() + entry_offset + 8);
    const std::size_t image_offset =
        detail::read_u32_le(data.data() + entry_offset + 12);
    if (image_offset >= data.size() || image_size > data.size() - image_offset)
    {
        return std::nullopt;
    }
    const unsigned char* image_data = data.data() + image_offset;

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
            stbi_image_free(pixels);
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

    // Classic BITMAPINFOHEADER entry.
    if (image_size < 40 || detail::read_u32_le(image_data) < 40)
    {
        return std::nullopt;
    }
    const std::int32_t width = static_cast<std::int32_t>(
        detail::read_u32_le(image_data + 4));
    const std::int32_t raw_height = static_cast<std::int32_t>(
        detail::read_u32_le(image_data + 8));
    const std::uint16_t planes = detail::read_u16_le(image_data + 12);
    const std::uint16_t bit_count = detail::read_u16_le(image_data + 14);
    const std::uint32_t compression = detail::read_u32_le(image_data + 16);
    if (width <= 0 || raw_height == 0 || planes != 1 || compression != 0 ||
        (bit_count != 32 && bit_count != 24))
    {
        return std::nullopt;
    }

    // Positive height encodes the image plus its 1-bit AND mask; negative
    // height means top-down pixels without a mask. Square icons store the
    // doubled height (image + mask), matching the classic ICO layout.
    const bool top_down = raw_height < 0;
    const bool has_and_mask = !top_down && raw_height == width * 2;
    int height = std::abs(raw_height);
    if (has_and_mask)
    {
        height /= 2;
    }
    if (height <= 0)
    {
        return std::nullopt;
    }

    const std::size_t stride = static_cast<std::size_t>(
        (static_cast<std::size_t>(width) * bit_count + 31U) / 32U * 4U);
    const std::size_t pixel_bytes = stride * static_cast<std::size_t>(height);
    const unsigned char* pixel_start = image_data + 40U;
    const std::size_t mask_bytes =
        has_and_mask
            ? (static_cast<std::size_t>(width) + 31U) / 32U * 4U *
                static_cast<std::size_t>(height)
            : 0U;
    if (pixel_start + pixel_bytes + mask_bytes >
        data.data() + image_offset + image_size)
    {
        return std::nullopt;
    }

    DecodedImage result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width * height) * 4U);

    const unsigned char* mask_start = pixel_start + pixel_bytes;
    const std::size_t mask_stride = static_cast<std::size_t>(
        (static_cast<std::size_t>(width) + 31U) / 32U * 4U);

    for (int y = 0; y < height; ++y)
    {
        // Bottom-up rows are stored with the last image row first.
        const int source_row = top_down ? y : (height - 1 - y);
        const unsigned char* row =
            pixel_start + static_cast<std::size_t>(source_row) * stride;
        const std::size_t mask_row_offset =
            static_cast<std::size_t>(y) * mask_stride;

        for (int x = 0; x < width; ++x)
        {
            std::size_t out = static_cast<std::size_t>(y * width + x) * 4U;
            if (bit_count == 32)
            {
                result.pixels[out + 0] = row[static_cast<std::size_t>(x) * 4U + 2];
                result.pixels[out + 1] = row[static_cast<std::size_t>(x) * 4U + 1];
                result.pixels[out + 2] = row[static_cast<std::size_t>(x) * 4U + 0];
                result.pixels[out + 3] = row[static_cast<std::size_t>(x) * 4U + 3];
            }
            else
            {
                result.pixels[out + 0] = row[static_cast<std::size_t>(x) * 3U + 2];
                result.pixels[out + 1] = row[static_cast<std::size_t>(x) * 3U + 1];
                result.pixels[out + 2] = row[static_cast<std::size_t>(x) * 3U + 0];
                result.pixels[out + 3] = 255;
            }

            // Apply the AND mask (1 = transparent) when present.
            if (has_and_mask)
            {
                const std::size_t mask_index =
                    mask_row_offset + static_cast<std::size_t>(x) / 8U;
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

} // namespace Zenvra::Utility
