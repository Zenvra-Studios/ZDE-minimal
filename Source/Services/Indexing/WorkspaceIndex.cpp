#include "WorkspaceIndex.h"

#include <fstream>
#include <regex>
#include <thread>

namespace Zenvra::Services::Indexing
{

WorkspaceIndex::WorkspaceIndex() = default;
WorkspaceIndex::~WorkspaceIndex() = default;

void WorkspaceIndex::set_workspace_root(std::filesystem::path root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_root = std::move(root);
    m_symbol_index.clear();
}

std::future<void> WorkspaceIndex::start_full_indexing()
{
    return std::async(std::launch::async, [this]() {
        perform_indexing();
    });
}

void WorkspaceIndex::perform_indexing()
{
    std::filesystem::path root;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        root = m_root;
    }

    if (root.empty() || !std::filesystem::exists(root))
        return;

    m_is_indexing = true;
    m_files_indexed = 0;

    std::vector<std::filesystem::path> files;
    std::error_code ec;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
    {
        if (entry.is_regular_file(ec))
        {
            std::string path_str = entry.path().string();
            if (path_str.find(".git") != std::string::npos ||
                path_str.find("node_modules") != std::string::npos ||
                path_str.find("build") != std::string::npos ||
                path_str.find("target") != std::string::npos)
            {
                continue;
            }

            files.push_back(entry.path());
        }
    }

    m_total_files = files.size();

    for (const auto& file : files)
    {
        index_file(file);
        ++m_files_indexed;
    }

    m_is_indexing = false;
}

void WorkspaceIndex::index_file(const std::filesystem::path& file_path)
{
    std::ifstream stream(file_path);
    if (!stream.is_open())
        return;

    std::vector<IndexedSymbol> discovered;
    std::string line;
    uint32_t line_number = 0;

    // Fast regex for functions, classes, structs
    static const std::regex class_regex(R"(\b(class|struct|interface|enum)\s+([A-Za-z_][A-Za-z0-9_]*))");
    static const std::regex fn_regex(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*(\{|;|\->))");

    while (std::getline(stream, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, class_regex))
        {
            IndexedSymbol sym;
            std::string keyword = match[1].str();
            sym.name = match[2].str();
            sym.file_path = file_path;
            sym.line = line_number;
            sym.character = static_cast<uint32_t>(match.position(2));

            if (keyword == "class") sym.kind = SymbolKind::Class;
            else if (keyword == "struct") sym.kind = SymbolKind::Struct;
            else if (keyword == "interface") sym.kind = SymbolKind::Interface;
            else if (keyword == "enum") sym.kind = SymbolKind::Enum;

            discovered.push_back(std::move(sym));
        }

        if (std::regex_search(line, match, fn_regex))
        {
            std::string name = match[1].str();
            if (name != "if" && name != "while" && name != "for" && name != "switch")
            {
                IndexedSymbol sym;
                sym.name = std::move(name);
                sym.file_path = file_path;
                sym.line = line_number;
                sym.character = static_cast<uint32_t>(match.position(1));
                sym.kind = SymbolKind::Function;
                discovered.push_back(std::move(sym));
            }
        }

        ++line_number;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_symbol_index.clear_file(file_path);
        for (auto& sym : discovered)
        {
            m_symbol_index.add_symbol(std::move(sym));
        }
    }
}

void WorkspaceIndex::remove_file(const std::filesystem::path& file_path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_symbol_index.clear_file(file_path);
}

std::vector<IndexedSymbol> WorkspaceIndex::search_symbols(std::string_view query, std::size_t limit) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_symbol_index.query(query, limit);
}

IndexingProgress WorkspaceIndex::get_progress() const
{
    IndexingProgress p;
    p.files_indexed = m_files_indexed.load();
    p.total_files = m_total_files.load();
    p.is_indexing = m_is_indexing.load();
    return p;
}

} // namespace Zenvra::Services::Indexing

