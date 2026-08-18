#include "UI/Editor/WorkspaceSearchModel.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <future>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace Zenvra::UI::Editor
{

namespace
{

bool is_binary_or_ignored_dir(std::string_view name)
{
    return name == ".git";
}

bool is_searchable_file(const std::filesystem::path& path)
{
    const std::string ext = path.extension().string();
    if (ext.empty()) return true;

    static constexpr std::string_view binary_exts[] = {
        ".exe", ".dll", ".obj", ".o", ".so", ".dylib", ".pdb", ".bin",
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".tiff", ".webp",
        ".zip", ".tar", ".gz", ".7z", ".rar", ".bz2", ".xz",
        ".pdf", ".ttf", ".otf", ".woff", ".woff2",
        ".mp3", ".mp4", ".wav", ".ogg", ".flac", ".avi", ".mov",
        ".class", ".pyc", ".pyo", ".pyd", ".idb", ".ilk", ".exp", ".lib", ".a"
    };

    for (const auto& b_ext : binary_exts)
    {
        if (ext == b_ext)
        {
            return false;
        }
    }
    return true;
}

bool is_word_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

} // namespace

void WorkspaceSearchModel::set_workspace_root(const std::filesystem::path& root)
{
    m_workspace_root = root;
    if (!m_search_query.empty())
    {
        execute_search();
    }
}

void WorkspaceSearchModel::set_search_query(std::string_view query)
{
    m_search_query = query;
    m_search_caret = m_search_query.size();
}

void WorkspaceSearchModel::set_replace_query(std::string_view query)
{
    m_replace_query = query;
    m_replace_caret = m_replace_query.size();
}

void WorkspaceSearchModel::toggle_match_case() noexcept
{
    m_match_case = !m_match_case;
    if (!m_search_query.empty())
    {
        execute_search();
    }
}

void WorkspaceSearchModel::toggle_match_word() noexcept
{
    m_match_word = !m_match_word;
    if (!m_search_query.empty())
    {
        execute_search();
    }
}

void WorkspaceSearchModel::toggle_use_regex() noexcept
{
    m_use_regex = !m_use_regex;
    if (!m_search_query.empty())
    {
        execute_search();
    }
}

void WorkspaceSearchModel::toggle_preserve_case() noexcept
{
    m_preserve_case = !m_preserve_case;
}

void WorkspaceSearchModel::toggle_replace_expanded() noexcept
{
    m_replace_expanded = !m_replace_expanded;
}

void WorkspaceSearchModel::set_focused_input(SearchInputFocus focus) noexcept
{
    m_focused_input = focus;
}

void WorkspaceSearchModel::insert_char(char32_t codepoint)
{
    std::string utf8;
    if (codepoint <= 0x7F)
    {
        utf8.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        utf8.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint <= 0xFFFF)
    {
        utf8.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        utf8.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    insert_text(utf8);
}

void WorkspaceSearchModel::select_all() noexcept
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        m_replace_sel_start = 0;
        m_replace_sel_end = m_replace_query.size();
        m_replace_caret = m_replace_query.size();
    }
    else
    {
        m_search_sel_start = 0;
        m_search_sel_end = m_search_query.size();
        m_search_caret = m_search_query.size();
    }
}

void WorkspaceSearchModel::clear_selection() noexcept
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        m_replace_sel_start = m_replace_caret;
        m_replace_sel_end = m_replace_caret;
    }
    else
    {
        m_search_sel_start = m_search_caret;
        m_search_sel_end = m_search_caret;
    }
}

bool WorkspaceSearchModel::has_selection() const noexcept
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        return m_replace_sel_start != m_replace_sel_end;
    }
    return m_search_sel_start != m_search_sel_end;
}

std::pair<std::size_t, std::size_t> WorkspaceSearchModel::get_selection_range() const noexcept
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        return {std::min(m_replace_sel_start, m_replace_sel_end), std::max(m_replace_sel_start, m_replace_sel_end)};
    }
    return {std::min(m_search_sel_start, m_search_sel_end), std::max(m_search_sel_start, m_search_sel_end)};
}

std::string WorkspaceSearchModel::get_selected_text() const
{
    if (!has_selection()) return {};
    auto [s_min, s_max] = get_selection_range();
    if (m_focused_input == SearchInputFocus::Replace)
    {
        return m_replace_query.substr(s_min, s_max - s_min);
    }
    return m_search_query.substr(s_min, s_max - s_min);
}

void WorkspaceSearchModel::set_caret_and_selection(std::size_t caret, std::size_t sel_start, std::size_t sel_end) noexcept
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        m_replace_caret = std::min(caret, m_replace_query.size());
        m_replace_sel_start = std::min(sel_start, m_replace_query.size());
        m_replace_sel_end = std::min(sel_end, m_replace_query.size());
    }
    else
    {
        m_search_caret = std::min(caret, m_search_query.size());
        m_search_sel_start = std::min(sel_start, m_search_query.size());
        m_search_sel_end = std::min(sel_end, m_search_query.size());
    }
}

void WorkspaceSearchModel::update_drag_selection(std::size_t caret) noexcept
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        m_replace_caret = std::min(caret, m_replace_query.size());
        m_replace_sel_end = m_replace_caret;
    }
    else
    {
        m_search_caret = std::min(caret, m_search_query.size());
        m_search_sel_end = m_search_caret;
    }
}

void WorkspaceSearchModel::insert_text(std::string_view text)
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        if (has_selection())
        {
            auto [s_min, s_max] = get_selection_range();
            m_replace_query.erase(s_min, s_max - s_min);
            m_replace_caret = s_min;
            clear_selection();
        }
        m_replace_caret = std::min(m_replace_caret, m_replace_query.size());
        m_replace_query.insert(m_replace_caret, text);
        m_replace_caret += text.size();
        m_replace_sel_start = m_replace_caret;
        m_replace_sel_end = m_replace_caret;
    }
    else
    {
        if (has_selection())
        {
            auto [s_min, s_max] = get_selection_range();
            m_search_query.erase(s_min, s_max - s_min);
            m_search_caret = s_min;
            clear_selection();
        }
        m_search_caret = std::min(m_search_caret, m_search_query.size());
        m_search_query.insert(m_search_caret, text);
        m_search_caret += text.size();
        m_search_sel_start = m_search_caret;
        m_search_sel_end = m_search_caret;
        trigger_async_search();
    }
}

void WorkspaceSearchModel::handle_backspace()
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        if (has_selection())
        {
            auto [s_min, s_max] = get_selection_range();
            m_replace_query.erase(s_min, s_max - s_min);
            m_replace_caret = s_min;
            clear_selection();
        }
        else if (m_replace_caret > 0 && !m_replace_query.empty())
        {
            m_replace_caret = std::min(m_replace_caret, m_replace_query.size());
            m_replace_query.erase(m_replace_caret - 1, 1);
            --m_replace_caret;
            m_replace_sel_start = m_replace_caret;
            m_replace_sel_end = m_replace_caret;
        }
    }
    else
    {
        if (has_selection())
        {
            auto [s_min, s_max] = get_selection_range();
            m_search_query.erase(s_min, s_max - s_min);
            m_search_caret = s_min;
            clear_selection();
            if (m_search_query.empty())
            {
                clear_query();
            }
            else
            {
                trigger_async_search();
            }
        }
        else if (m_search_caret > 0 && !m_search_query.empty())
        {
            m_search_caret = std::min(m_search_caret, m_search_query.size());
            m_search_query.erase(m_search_caret - 1, 1);
            --m_search_caret;
            m_search_sel_start = m_search_caret;
            m_search_sel_end = m_search_caret;
            if (m_search_query.empty())
            {
                clear_query();
            }
            else
            {
                trigger_async_search();
            }
        }
    }
}

void WorkspaceSearchModel::handle_delete()
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        if (has_selection())
        {
            auto [s_min, s_max] = get_selection_range();
            m_replace_query.erase(s_min, s_max - s_min);
            m_replace_caret = s_min;
            clear_selection();
        }
        else if (m_replace_caret < m_replace_query.size())
        {
            m_replace_query.erase(m_replace_caret, 1);
            m_replace_sel_start = m_replace_caret;
            m_replace_sel_end = m_replace_caret;
        }
    }
    else
    {
        if (has_selection())
        {
            auto [s_min, s_max] = get_selection_range();
            m_search_query.erase(s_min, s_max - s_min);
            m_search_caret = s_min;
            clear_selection();
            if (m_search_query.empty())
            {
                clear_query();
            }
            else
            {
                trigger_async_search();
            }
        }
        else if (m_search_caret < m_search_query.size())
        {
            m_search_query.erase(m_search_caret, 1);
            m_search_sel_start = m_search_caret;
            m_search_sel_end = m_search_caret;
            if (m_search_query.empty())
            {
                clear_query();
            }
            else
            {
                trigger_async_search();
            }
        }
    }
}

void WorkspaceSearchModel::handle_left(bool shift)
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        if (m_replace_caret > 0)
        {
            --m_replace_caret;
        }
        if (shift)
        {
            m_replace_sel_end = m_replace_caret;
        }
        else
        {
            m_replace_sel_start = m_replace_caret;
            m_replace_sel_end = m_replace_caret;
        }
    }
    else
    {
        if (m_search_caret > 0)
        {
            --m_search_caret;
        }
        if (shift)
        {
            m_search_sel_end = m_search_caret;
        }
        else
        {
            m_search_sel_start = m_search_caret;
            m_search_sel_end = m_search_caret;
        }
    }
}

void WorkspaceSearchModel::handle_right(bool shift)
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        if (m_replace_caret < m_replace_query.size())
        {
            ++m_replace_caret;
        }
        if (shift)
        {
            m_replace_sel_end = m_replace_caret;
        }
        else
        {
            m_replace_sel_start = m_replace_caret;
            m_replace_sel_end = m_replace_caret;
        }
    }
    else
    {
        if (m_search_caret < m_search_query.size())
        {
            ++m_search_caret;
        }
        if (shift)
        {
            m_search_sel_end = m_search_caret;
        }
        else
        {
            m_search_sel_start = m_search_caret;
            m_search_sel_end = m_search_caret;
        }
    }
}

void WorkspaceSearchModel::handle_home(bool shift)
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        m_replace_caret = 0;
        if (shift)
        {
            m_replace_sel_end = 0;
        }
        else
        {
            m_replace_sel_start = 0;
            m_replace_sel_end = 0;
        }
    }
    else
    {
        m_search_caret = 0;
        if (shift)
        {
            m_search_sel_end = 0;
        }
        else
        {
            m_search_sel_start = 0;
            m_search_sel_end = 0;
        }
    }
}

void WorkspaceSearchModel::handle_end(bool shift)
{
    if (m_focused_input == SearchInputFocus::Replace)
    {
        m_replace_caret = m_replace_query.size();
        if (shift)
        {
            m_replace_sel_end = m_replace_caret;
        }
        else
        {
            m_replace_sel_start = m_replace_caret;
            m_replace_sel_end = m_replace_caret;
        }
    }
    else
    {
        m_search_caret = m_search_query.size();
        if (shift)
        {
            m_search_sel_end = m_search_caret;
        }
        else
        {
            m_search_sel_start = m_search_caret;
            m_search_sel_end = m_search_caret;
        }
    }
}

void WorkspaceSearchModel::clear_query()
{
    m_cancel_requested = true;
    ++m_search_generation;
    m_search_query.clear();
    m_search_query.shrink_to_fit();
    m_search_caret = 0;
    m_search_sel_start = 0;
    m_search_sel_end = 0;
    m_is_searching = false;
    clear_results();
    rebuild_visible_rows();
}

void WorkspaceSearchModel::clear_results()
{
    m_results.clear();
    m_results.shrink_to_fit();
    m_visible_rows.clear();
    m_visible_rows.shrink_to_fit();
    m_total_match_count = 0;
    m_total_file_count = 0;
    m_scroll_offset = 0;
    m_search_error.clear();
    m_search_error.shrink_to_fit();
    {
        std::lock_guard<std::mutex> lock(m_staging_mutex);
        m_staging_results.clear();
        m_staging_results.shrink_to_fit();
    }
}

void WorkspaceSearchModel::collapse_all()
{
    for (auto& file : m_results)
    {
        file.expanded = false;
    }
    rebuild_visible_rows();
}

void WorkspaceSearchModel::expand_all()
{
    for (auto& file : m_results)
    {
        file.expanded = true;
    }
    rebuild_visible_rows();
}

void WorkspaceSearchModel::toggle_file_expanded(std::size_t file_index)
{
    if (file_index < m_results.size())
    {
        m_results[file_index].expanded = !m_results[file_index].expanded;
        rebuild_visible_rows();
    }
}

bool WorkspaceSearchModel::tick() noexcept
{
    bool updated = false;

    // Check if background results are ready
    if (m_results_ready.load())
    {
        std::lock_guard<std::mutex> lock(m_staging_mutex);
        m_results = std::move(m_staging_results);
        m_search_error = std::move(m_staging_error);
        m_total_match_count = m_staging_match_count;
        m_total_file_count = m_results.size();
        m_is_searching = false;
        m_results_ready = false;
        rebuild_visible_rows();
        updated = true;
    }

    return updated;
}

void WorkspaceSearchModel::execute_search()
{
    trigger_async_search();
}

void WorkspaceSearchModel::trigger_async_search()
{
    const std::uint64_t current_gen = ++m_search_generation;
    m_cancel_requested = true;

    if (m_search_query.empty())
    {
        clear_results();
        m_is_searching = false;
        return;
    }

    m_is_searching = true;
    m_search_error.clear();
    m_cancel_requested = false;

    const std::string query = m_search_query;
    const bool match_case = m_match_case;
    const bool match_word = m_match_word;
    const bool use_regex = m_use_regex;

    std::filesystem::path search_root = m_workspace_root;
    if (search_root.empty())
    {
        std::error_code cur_ec;
        std::filesystem::path candidate = std::filesystem::current_path(cur_ec);
        for (int depth = 0; depth < 6 && !candidate.empty() && candidate != candidate.root_path(); ++depth)
        {
            if (std::filesystem::exists(candidate / "CMakeLists.txt", cur_ec) ||
                std::filesystem::exists(candidate / "Source", cur_ec) ||
                std::filesystem::exists(candidate / ".git", cur_ec))
            {
                search_root = candidate;
                break;
            }
            candidate = candidate.parent_path();
        }
        if (search_root.empty())
        {
            search_root = std::filesystem::current_path(cur_ec);
        }
    }

    std::thread([this, current_gen, query, match_case, match_word, use_regex, search_root]() {
        if (m_cancel_requested.load() || current_gen != m_search_generation.load()) return;

        std::error_code ec;
        if (!std::filesystem::exists(search_root, ec) || !std::filesystem::is_directory(search_root, ec))
        {
            return;
        }

        std::regex regex_pattern;
        if (use_regex)
        {
            try
            {
                auto flags = std::regex::ECMAScript;
                if (!match_case) flags |= std::regex::icase;
                regex_pattern = std::regex(query, flags);
            }
            catch (const std::regex_error& err)
            {
                std::lock_guard<std::mutex> lock(m_staging_mutex);
                m_staging_error = err.what();
                m_staging_results.clear();
                m_staging_match_count = 0;
                m_results_ready = true;
                return;
            }
        }

        std::string lower_query;
        if (!match_case && !use_regex)
        {
            lower_query.reserve(query.size());
            for (char c : query)
            {
                lower_query.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }

        // 1. Collect candidate files (using in-memory cache if search_root unchanged)
        std::vector<std::filesystem::path> candidate_files;
        {
            std::lock_guard<std::mutex> cache_lock(m_cache_mutex);
            if (m_cached_search_root == search_root && !m_cached_files.empty())
            {
                candidate_files = m_cached_files;
            }
        }

        if (candidate_files.empty())
        {
            candidate_files.reserve(4096);
            for (std::filesystem::recursive_directory_iterator it(search_root, std::filesystem::directory_options::skip_permission_denied, ec), end;
                 it != end; it.increment(ec))
            {
                if (m_cancel_requested.load() || current_gen != m_search_generation.load()) return;
                if (ec) { ec.clear(); continue; }

                const auto& entry = *it;
                const auto& p = entry.path();
                const std::string filename = p.filename().string();

                if (entry.is_directory(ec))
                {
                    if (is_binary_or_ignored_dir(filename))
                    {
                        it.disable_recursion_pending();
                    }
                    continue;
                }

                if (!entry.is_regular_file(ec) || !is_searchable_file(p))
                {
                    continue;
                }

                const auto file_size = entry.file_size(ec);
                if (ec || file_size > 4 * 1024 * 1024 || file_size == 0)
                {
                    continue;
                }

                candidate_files.push_back(p);
            }

            if (m_cancel_requested.load() || current_gen != m_search_generation.load()) return;

            {
                std::lock_guard<std::mutex> cache_lock(m_cache_mutex);
                m_cached_search_root = search_root;
                m_cached_files = candidate_files;
            }
        }

        if (m_cancel_requested.load() || current_gen != m_search_generation.load()) return;

        // 2. Scan files in parallel using fast block read
        const unsigned int thread_count = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
        const std::size_t total_files = candidate_files.size();
        const std::size_t chunk_size = (total_files + thread_count - 1) / thread_count;

        std::vector<std::future<std::vector<FileSearchResult>>> futures;
        futures.reserve(thread_count);

        for (unsigned int t = 0; t < thread_count; ++t)
        {
            const std::size_t start_idx = t * chunk_size;
            const std::size_t end_idx = std::min(start_idx + chunk_size, total_files);
            if (start_idx >= end_idx) break;

            futures.push_back(std::async(std::launch::async, [this, current_gen, &candidate_files, start_idx, end_idx, &search_root, &query, match_case, match_word, use_regex, &regex_pattern, &lower_query]() {
                std::vector<FileSearchResult> local_results;
                std::error_code local_ec;

                for (std::size_t i = start_idx; i < end_idx; ++i)
                {
                    if (m_cancel_requested.load() || current_gen != m_search_generation.load()) break;

                    const auto& p = candidate_files[i];
                    std::ifstream file(p, std::ios::binary | std::ios::ate);
                    if (!file.is_open()) continue;

                    const auto file_pos = file.tellg();
                    if (file_pos <= 0 || file_pos > 4 * 1024 * 1024) continue;
                    const auto file_sz = static_cast<std::size_t>(file_pos);
                    file.seekg(0, std::ios::beg);

                    std::string content;
                    content.resize(file_sz);
                    file.read(content.data(), static_cast<std::streamsize>(file_sz));
                    file.close();

                    if (content.empty()) continue;

                    // Fast skip for raw binary files (check first 256 bytes for null character)
                    const std::size_t check_len = std::min(content.size(), std::size_t{256});
                    if (std::memchr(content.data(), 0, check_len) != nullptr) continue;

                    // Fast whole-file precheck
                    if (!use_regex)
                    {
                        if (match_case)
                        {
                            if (content.find(query) == std::string::npos) continue;
                        }
                        else
                        {
                            std::string lower_content = content;
                            for (char& c : lower_content)
                            {
                                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            }
                            if (lower_content.find(lower_query) == std::string::npos) continue;
                        }
                    }

                    std::vector<SearchMatch> file_matches;
                    std::size_t line_num = 1;
                    std::size_t line_start = 0;

                    while (line_start < content.size())
                    {
                        std::size_t line_end = content.find('\n', line_start);
                        if (line_end == std::string::npos) line_end = content.size();

                        std::string line = content.substr(line_start, line_end - line_start);
                        if (!line.empty() && line.back() == '\r') line.pop_back();

                        std::vector<SearchHighlightSpan> line_spans;
                        std::size_t first_col_start = 0;
                        std::size_t first_match_len = 0;
                        std::size_t first_prev_start = 0;

                        std::size_t trim_start = 0;
                        while (trim_start < line.size() && (line[trim_start] == ' ' || line[trim_start] == '\t'))
                        {
                            ++trim_start;
                        }
                        std::string preview_text = line.substr(trim_start);

                        if (use_regex)
                        {
                            auto words_begin = std::sregex_iterator(line.begin(), line.end(), regex_pattern);
                            auto words_end = std::sregex_iterator();
                            for (std::sregex_iterator r_it = words_begin; r_it != words_end; ++r_it)
                            {
                                const std::smatch& match = *r_it;
                                const auto col_start = static_cast<std::size_t>(match.position());
                                const auto match_len = static_cast<std::size_t>(match.length());
                                if (match_len == 0) continue;

                                std::size_t prev_match_start = (col_start >= trim_start) ? (col_start - trim_start) : 0;
                                if (line_spans.empty())
                                {
                                    first_col_start = col_start;
                                    first_match_len = match_len;
                                    first_prev_start = prev_match_start;
                                }
                                line_spans.push_back(SearchHighlightSpan{
                                    .start = prev_match_start,
                                    .length = match_len
                                });
                            }
                        }
                        else
                        {
                            std::string search_line = line;
                            if (!match_case)
                            {
                                for (char& c : search_line)
                                {
                                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                }
                            }

                            const std::string_view target_query = match_case ? query : lower_query;
                            std::size_t pos = search_line.find(target_query, 0);
                            while (pos != std::string::npos)
                            {
                                bool valid = true;
                                if (match_word)
                                {
                                    if (pos > 0 && is_word_char(line[pos - 1])) valid = false;
                                    const std::size_t after = pos + target_query.size();
                                    if (after < line.size() && is_word_char(line[after])) valid = false;
                                }

                                if (valid)
                                {
                                    std::size_t prev_match_start = (pos >= trim_start) ? (pos - trim_start) : 0;
                                    if (line_spans.empty())
                                    {
                                        first_col_start = pos;
                                        first_match_len = target_query.size();
                                        first_prev_start = prev_match_start;
                                    }
                                    line_spans.push_back(SearchHighlightSpan{
                                        .start = prev_match_start,
                                        .length = target_query.size()
                                    });
                                }

                                pos = search_line.find(target_query, pos + std::max(target_query.size(), std::size_t{1}));
                            }
                        }

                        if (!line_spans.empty())
                        {
                            file_matches.push_back(SearchMatch{
                                .line_number = line_num,
                                .column_start = first_col_start,
                                .match_length = first_match_len,
                                .line_content = std::move(preview_text),
                                .match_preview_start = first_prev_start,
                                .match_preview_length = first_match_len,
                                .spans = std::move(line_spans)
                            });
                        }

                        line_start = line_end + 1;
                        ++line_num;
                    }

                    if (!file_matches.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(p, search_root, local_ec);
                        std::string rel_dir = rel.parent_path().string();
                        if (rel_dir == ".") rel_dir.clear();

                        local_results.push_back(FileSearchResult{
                            .file_path = p,
                            .file_name = p.filename().string(),
                            .relative_dir = std::move(rel_dir),
                            .matches = std::move(file_matches),
                            .expanded = true
                        });
                    }
                }

                return local_results;
            }));
        }

        std::vector<FileSearchResult> aggregated;
        std::size_t match_count = 0;

        for (auto& fut : futures)
        {
            auto chunk_results = fut.get();
            for (auto& file_res : chunk_results)
            {
                match_count += file_res.matches.size();
                aggregated.push_back(std::move(file_res));
                if (match_count >= 5000) break;
            }
            if (match_count >= 5000) break;
        }

        if (m_cancel_requested.load() || current_gen != m_search_generation.load()) return;

        {
            std::lock_guard<std::mutex> lock(m_staging_mutex);
            m_staging_results = std::move(aggregated);
            m_staging_match_count = match_count;
            m_staging_error.clear();
            m_results_ready = true;
        }
    }).detach();
}

void WorkspaceSearchModel::rebuild_visible_rows()
{
    m_visible_rows.clear();
    for (std::size_t f_idx = 0; f_idx < m_results.size(); ++f_idx)
    {
        const auto& file = m_results[f_idx];
        m_visible_rows.push_back(SearchVisibleRow{
            .kind = SearchRowKind::FileHeader,
            .file_index = f_idx,
            .match_index = 0
        });

        if (file.expanded)
        {
            for (std::size_t m_idx = 0; m_idx < file.matches.size(); ++m_idx)
            {
                m_visible_rows.push_back(SearchVisibleRow{
                    .kind = SearchRowKind::MatchLine,
                    .file_index = f_idx,
                    .match_index = m_idx
                });
            }
        }
    }
}

std::optional<SearchNavigationTarget> WorkspaceSearchModel::activate_visible_row(std::size_t visible_row_index)
{
    if (visible_row_index >= m_visible_rows.size())
    {
        return std::nullopt;
    }

    const auto& row = m_visible_rows[visible_row_index];
    if (row.file_index >= m_results.size())
    {
        return std::nullopt;
    }

    if (row.kind == SearchRowKind::FileHeader)
    {
        toggle_file_expanded(row.file_index);
        return std::nullopt;
    }

    const auto& file = m_results[row.file_index];
    if (row.match_index >= file.matches.size())
    {
        return std::nullopt;
    }

    const auto& match = file.matches[row.match_index];
    return SearchNavigationTarget{
        .path = file.file_path,
        .line = match.line_number,
        .column = match.column_start
    };
}

bool WorkspaceSearchModel::replace_in_file(std::size_t file_index)
{
    if (file_index >= m_results.size() || m_search_query.empty())
    {
        return false;
    }

    const auto& file_res = m_results[file_index];
    std::ifstream in(file_res.file_path, std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();

    std::string content = buffer.str();
    if (m_use_regex)
    {
        try
        {
            auto flags = std::regex::ECMAScript;
            if (!m_match_case)
            {
                flags |= std::regex::icase;
            }
            std::regex reg(m_search_query, flags);
            content = std::regex_replace(content, reg, m_replace_query);
        }
        catch (...)
        {
            return false;
        }
    }
    else
    {
        std::size_t pos = 0;
        while ((pos = content.find(m_search_query, pos)) != std::string::npos)
        {
            content.replace(pos, m_search_query.length(), m_replace_query);
            pos += m_replace_query.length();
        }
    }

    std::ofstream out(file_res.file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }
    out << content;
    out.close();

    execute_search();
    return true;
}

bool WorkspaceSearchModel::replace_all()
{
    if (m_results.empty() || m_search_query.empty())
    {
        return false;
    }

    for (std::size_t i = 0; i < m_results.size(); ++i)
    {
        replace_in_file(i);
    }
    execute_search();
    return true;
}

bool WorkspaceSearchModel::scroll(std::ptrdiff_t delta, std::size_t viewport_rows) noexcept
{
    if (delta == 0)
    {
        return false;
    }

    if (m_visible_rows.size() <= viewport_rows)
    {
        if (m_scroll_offset != 0)
        {
            m_scroll_offset = 0;
            return true;
        }
        return false;
    }

    const std::size_t max_offset = m_visible_rows.size() - viewport_rows;
    const std::size_t previous = m_scroll_offset;
    if (delta > 0)
    {
        m_scroll_offset = std::min(max_offset, m_scroll_offset + static_cast<std::size_t>(delta));
    }
    else
    {
        const std::size_t amount = static_cast<std::size_t>(-delta);
        m_scroll_offset = (amount > m_scroll_offset) ? 0 : (m_scroll_offset - amount);
    }
    return m_scroll_offset != previous;
}

} // namespace Zenvra::UI::Editor
