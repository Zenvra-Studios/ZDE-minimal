#include "SymbolIndex.h"

#include <algorithm>

namespace Zenvra::Services::Indexing
{

namespace
{
bool case_insensitive_contains(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > haystack.size())
        return false;

    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) {
            return std::tolower(static_cast<unsigned char>(ch1)) ==
                   std::tolower(static_cast<unsigned char>(ch2));
        });
    return it != haystack.end();
}
} // namespace

void SymbolIndex::add_symbol(IndexedSymbol symbol)
{
    m_symbols.push_back(std::move(symbol));
}

void SymbolIndex::clear_file(const std::filesystem::path& file_path)
{
    m_symbols.erase(
        std::remove_if(m_symbols.begin(), m_symbols.end(),
                       [&file_path](const IndexedSymbol& s) {
                           return s.file_path == file_path;
                       }),
        m_symbols.end());
}

void SymbolIndex::clear()
{
    m_symbols.clear();
}

std::vector<IndexedSymbol> SymbolIndex::query(std::string_view query, std::size_t max_results) const
{
    std::vector<IndexedSymbol> results;
    results.reserve(std::min(m_symbols.size(), max_results));

    for (const auto& sym : m_symbols)
    {
        if (case_insensitive_contains(sym.name, query) ||
            case_insensitive_contains(sym.container_name, query))
        {
            results.push_back(sym);
            if (results.size() >= max_results)
                break;
        }
    }

    return results;
}

} // namespace Zenvra::Services::Indexing

