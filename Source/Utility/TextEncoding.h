#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Zenvra::Utility
{

namespace detail
{

inline bool decode_utf8_code_point(
    std::string_view text,
    std::size_t& index,
    std::uint32_t& code_point) noexcept
{
    if (index >= text.size())
    {
        return false;
    }

    const auto byte_at = [&text](std::size_t offset) {
        return static_cast<std::uint8_t>(text[offset]);
    };
    const std::uint8_t lead = byte_at(index);
    std::size_t continuation_count = 0;
    std::uint32_t minimum_code_point = 0;

    if (lead <= 0x7FU)
    {
        code_point = lead;
        ++index;
        return true;
    }
    if (lead >= 0xC2U && lead <= 0xDFU)
    {
        continuation_count = 1;
        code_point = lead & 0x1FU;
        minimum_code_point = 0x80U;
    }
    else if (lead >= 0xE0U && lead <= 0xEFU)
    {
        continuation_count = 2;
        code_point = lead & 0x0FU;
        minimum_code_point = 0x800U;
    }
    else if (lead >= 0xF0U && lead <= 0xF4U)
    {
        continuation_count = 3;
        code_point = lead & 0x07U;
        minimum_code_point = 0x10000U;
    }
    else
    {
        return false;
    }

    if (continuation_count > text.size() - index - 1)
    {
        return false;
    }
    for (std::size_t offset = 1; offset <= continuation_count; ++offset)
    {
        const std::uint8_t continuation = byte_at(index + offset);
        if ((continuation & 0xC0U) != 0x80U)
        {
            return false;
        }
        code_point = (code_point << 6U) | (continuation & 0x3FU);
    }

    if (code_point < minimum_code_point || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU))
    {
        return false;
    }
    index += continuation_count + 1;
    return true;
}

inline bool decode_utf16_code_point(
    std::u16string_view text,
    std::size_t& index,
    std::uint32_t& code_point) noexcept
{
    if (index >= text.size())
    {
        return false;
    }

    const std::uint32_t first = static_cast<std::uint16_t>(text[index++]);
    if (first >= 0xD800U && first <= 0xDBFFU)
    {
        if (index >= text.size())
        {
            return false;
        }
        const std::uint32_t second = static_cast<std::uint16_t>(text[index]);
        if (second < 0xDC00U || second > 0xDFFFU)
        {
            return false;
        }
        ++index;
        code_point = 0x10000U + ((first - 0xD800U) << 10U) + (second - 0xDC00U);
        return true;
    }
    if (first >= 0xDC00U && first <= 0xDFFFU)
    {
        return false;
    }

    code_point = first;
    return true;
}

inline void append_utf8_code_point(std::string& result, std::uint32_t code_point)
{
    if (code_point <= 0x7FU)
    {
        result.push_back(static_cast<char>(code_point));
    }
    else if (code_point <= 0x7FFU)
    {
        result.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
    else if (code_point <= 0xFFFFU)
    {
        result.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
    else
    {
        result.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        result.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

inline void append_utf16_code_point(std::u16string& result, std::uint32_t code_point)
{
    if (code_point <= 0xFFFFU)
    {
        result.push_back(static_cast<char16_t>(code_point));
        return;
    }

    const std::uint32_t adjusted = code_point - 0x10000U;
    result.push_back(static_cast<char16_t>(0xD800U + (adjusted >> 10U)));
    result.push_back(static_cast<char16_t>(0xDC00U + (adjusted & 0x3FFU)));
}

} // namespace detail

/**
 * @brief Checks whether text is well-formed UTF-8.
 *
 * The validator rejects overlong sequences, UTF-16 surrogate code points,
 * truncated sequences, and code points above U+10FFFF.
 */
[[nodiscard]] inline bool is_valid_utf8(std::string_view text) noexcept
{
    std::size_t index = 0;
    std::uint32_t code_point = 0;
    while (index < text.size())
    {
        if (!detail::decode_utf8_code_point(text, index, code_point))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Converts UTF-8 to portable UTF-16 code units.
 * @return An empty optional when the input is not valid UTF-8.
 */
[[nodiscard]] inline std::optional<std::u16string> utf8_to_utf16(
    std::string_view text)
{
    std::u16string result;
    result.reserve(text.size());

    std::size_t index = 0;
    std::uint32_t code_point = 0;
    while (index < text.size())
    {
        if (!detail::decode_utf8_code_point(text, index, code_point))
        {
            return std::nullopt;
        }
        detail::append_utf16_code_point(result, code_point);
    }
    return result;
}

/**
 * @brief Converts portable UTF-16 code units to UTF-8.
 * @return An empty optional when the input contains an invalid surrogate.
 */
[[nodiscard]] inline std::optional<std::string> utf16_to_utf8(
    std::u16string_view text)
{
    std::string result;
    result.reserve(text.size());

    std::size_t index = 0;
    std::uint32_t code_point = 0;
    while (index < text.size())
    {
        if (!detail::decode_utf16_code_point(text, index, code_point))
        {
            return std::nullopt;
        }
        detail::append_utf8_code_point(result, code_point);
    }
    return result;
}

/**
 * @brief Converts UTF-8 to the platform wide-character representation.
 *
 * Windows uses UTF-16 wchar_t values, while Linux commonly uses UTF-32
 * wchar_t values. The conversion keeps the input/output Unicode scalar
 * values identical on both platforms.
 */
[[nodiscard]] inline std::optional<std::wstring> utf8_to_wide(std::string_view text)
{
    const std::optional<std::u16string> utf16 = utf8_to_utf16(text);
    if (!utf16)
    {
        return std::nullopt;
    }

    std::wstring result;
    if constexpr (sizeof(wchar_t) == sizeof(char16_t))
    {
        result.assign(utf16->begin(), utf16->end());
        return result;
    }

    result.reserve(utf16->size());
    std::size_t index = 0;
    std::uint32_t code_point = 0;
    while (index < utf16->size())
    {
        if (!detail::decode_utf16_code_point(*utf16, index, code_point))
        {
            return std::nullopt;
        }
        result.push_back(static_cast<wchar_t>(code_point));
    }
    return result;
}

/**
 * @brief Converts the platform wide-character representation to UTF-8.
 */
[[nodiscard]] inline std::optional<std::string> wide_to_utf8(std::wstring_view text)
{
    std::u16string utf16;
    if constexpr (sizeof(wchar_t) == sizeof(char16_t))
    {
        utf16.assign(text.begin(), text.end());
    }
    else
    {
        utf16.reserve(text.size());
        for (const wchar_t character : text)
        {
            const std::uint32_t code_point = static_cast<std::uint32_t>(character);
            if (code_point > 0x10FFFFU ||
                (code_point >= 0xD800U && code_point <= 0xDFFFU))
            {
                return std::nullopt;
            }
            detail::append_utf16_code_point(utf16, code_point);
        }
    }
    return utf16_to_utf8(utf16);
}

} // namespace Zenvra::Utility
