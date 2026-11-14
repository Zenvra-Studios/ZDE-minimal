#include "Services/Language/Detection/LanguageDetector.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

namespace Zenvra::Services::Language::Detection
{

namespace
{
std::string to_lower(std::string_view str)
{
    std::string result;
    result.reserve(str.size());
    for (char c : str)
    {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}
} // namespace

void LanguageDetector::set_user_association(std::string pattern, std::string language_id)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_user_associations[to_lower(pattern)] = std::move(language_id);
}

void LanguageDetector::remove_user_association(std::string_view pattern)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_user_associations.erase(to_lower(pattern));
}

void LanguageDetector::clear_user_associations()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_user_associations.clear();
}

std::string LanguageDetector::extract_first_line(std::string_view content) noexcept
{
    if (content.empty()) return {};
    const auto newline_pos = content.find_first_of("\r\n");
    if (newline_pos == std::string_view::npos)
    {
        return std::string(content.substr(0, std::min<std::size_t>(content.size(), 256)));
    }
    return std::string(content.substr(0, std::min<std::size_t>(newline_pos, 256)));
}

bool LanguageDetector::is_stl_header(std::string_view filename, const std::filesystem::path& full_path) noexcept
{
    static const std::unordered_set<std::string_view> s_stl_headers = {
        "algorithm", "any", "array", "atomic", "barrier", "bit", "bitset", "charconv",
        "chrono", "codecvt", "compare", "complex", "concepts", "condition_variable",
        "coroutine", "csetjmp", "csignal", "cstdarg", "cstddef", "cstdint", "cstdio",
        "cstdlib", "cstring", "ctime", "cuchar", "cwchar", "cwctype", "deque",
        "exception", "execution", "expected", "filesystem", "flat_map", "flat_set",
        "format", "forward_list", "fstream", "functional", "future", "generator",
        "initializer_list", "iomanip", "ios", "iosfwd", "iostream", "istream",
        "iterator", "latch", "limits", "list", "locale", "map", "mdspan", "memory",
        "memory_resource", "mutex", "new", "numbers", "numeric", "optional", "ostream",
        "print", "queue", "random", "ranges", "ratio", "regex", "scoped_allocator",
        "semaphore", "set", "shared_mutex", "source_location", "span", "spanstream",
        "sstream", "stack", "stacktrace", "stdexcept", "stdfloat", "stop_token",
        "streambuf", "string", "string_view", "strstream", "syncstream", "system_error",
        "text_encoding", "thread", "tuple", "typeindex", "typeinfo", "type_traits",
        "unordered_map", "unordered_set", "utility", "valarray", "variant", "vector",
        "version"
    };

    const std::string lower = to_lower(filename);
    if (s_stl_headers.contains(lower)) return true;

    if (!full_path.empty())
    {
        const std::string generic_path = full_path.generic_string();
        if (generic_path.find("/include/c++/") != std::string::npos ||
            generic_path.find("/VC/Tools/MSVC/") != std::string::npos ||
            generic_path.find("/usr/include/c++/") != std::string::npos ||
            generic_path.find("/usr/include/x86_64-linux-gnu/c++/") != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

DetectionResult LanguageDetector::detect(
    const std::filesystem::path& file_path,
    std::string_view file_content,
    const std::filesystem::path& workspace_root,
    const std::vector<Definitions::LanguageDefinition>& available_languages) const
{
    const std::string filename = file_path.filename().string();
    const std::string lower_filename = to_lower(filename);
    const std::string ext = file_path.extension().string();
    const std::string lower_ext = to_lower(ext);

    // 1. User overrides (highest priority, 1.0 confidence)
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (!lower_filename.empty())
        {
            if (auto it = m_user_associations.find(lower_filename); it != m_user_associations.end())
            {
                return DetectionResult{
                    .language_id = it->second,
                    .reason = DetectionMatchReason::UserOverride,
                    .confidence = 1.0F
                };
            }
        }
        if (!lower_ext.empty())
        {
            if (auto it = m_user_associations.find(lower_ext); it != m_user_associations.end())
            {
                return DetectionResult{
                    .language_id = it->second,
                    .reason = DetectionMatchReason::UserOverride,
                    .confidence = 1.0F
                };
            }
            if (auto it = m_user_associations.find("*" + lower_ext); it != m_user_associations.end())
            {
                return DetectionResult{
                    .language_id = it->second,
                    .reason = DetectionMatchReason::UserOverride,
                    .confidence = 1.0F
                };
            }
        }
    }

    // 2. Special filename match (0.95 confidence)
    if (!lower_filename.empty())
    {
        for (const auto& lang : available_languages)
        {
            if (lang.matches_filename(filename))
            {
                return DetectionResult{
                    .language_id = lang.id,
                    .reason = DetectionMatchReason::SpecialFilename,
                    .confidence = 0.95F
                };
            }
        }
    }

    // 3. File extension match (0.90 confidence)
    if (!lower_ext.empty())
    {
        for (const auto& lang : available_languages)
        {
            if (lang.matches_extension(ext))
            {
                return DetectionResult{
                    .language_id = lang.id,
                    .reason = DetectionMatchReason::FileExtension,
                    .confidence = 0.90F
                };
            }
        }
    }

    // 4. Content / First-line Pattern match (0.85 confidence)
    std::string first_line;
    if (!file_content.empty())
    {
        first_line = extract_first_line(file_content);
    }
    else if (!file_path.empty())
    {
        std::error_code ec;
        if (std::filesystem::exists(file_path, ec) && std::filesystem::is_regular_file(file_path, ec))
        {
            std::ifstream file(file_path, std::ios::binary);
            if (file)
            {
                std::string line;
                if (std::getline(file, line))
                {
                    first_line = extract_first_line(line);
                }
            }
        }
    }

    if (!first_line.empty())
    {
        for (const auto& lang : available_languages)
        {
            if (lang.matches_first_line(first_line))
            {
                return DetectionResult{
                    .language_id = lang.id,
                    .reason = DetectionMatchReason::FirstLinePattern,
                    .confidence = 0.85F
                };
            }
        }
    }

    // 5. C++ Standard Library header heuristic without extension
    if (ext.empty() && is_stl_header(filename, file_path))
    {
        return DetectionResult{
            .language_id = "cpp",
            .reason = DetectionMatchReason::ContentHeuristic,
            .confidence = 0.80F
        };
    }

    // 6. Project marker match (if workspace root is available)
    if (!workspace_root.empty())
    {
        for (const auto& lang : available_languages)
        {
            for (const auto& marker : lang.project_markers)
            {
                std::error_code ec;
                const auto marker_path = workspace_root / marker;
                if (std::filesystem::exists(marker_path, ec))
                {
                    // If file has no extension but workspace contains project marker
                    if (ext.empty())
                    {
                        return DetectionResult{
                            .language_id = lang.id,
                            .reason = DetectionMatchReason::ProjectMarker,
                            .confidence = 0.50F
                        };
                    }
                }
            }
        }
    }

    return DetectionResult{
        .language_id = "plaintext",
        .reason = DetectionMatchReason::Fallback,
        .confidence = 0.10F
    };
}

} // namespace Zenvra::Services::Language::Detection

