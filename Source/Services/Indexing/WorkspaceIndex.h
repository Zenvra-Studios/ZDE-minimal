#pragma once

#include "SymbolIndex.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace Zenvra::Services::Indexing
{

struct IndexingProgress
{
    std::size_t files_indexed{0};
    std::size_t total_files{0};
    bool is_indexing{false};
};

class WorkspaceIndex
{
public:
    WorkspaceIndex();
    ~WorkspaceIndex();

    void set_workspace_root(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept { return m_root; }

    std::future<void> start_full_indexing();

    void index_file(const std::filesystem::path& file_path);

    void remove_file(const std::filesystem::path& file_path);

    [[nodiscard]] std::vector<IndexedSymbol> search_symbols(std::string_view query, std::size_t limit = 50) const;

    [[nodiscard]] IndexingProgress get_progress() const;

private:
    void perform_indexing();

    std::filesystem::path m_root;
    mutable std::mutex m_mutex;
    SymbolIndex m_symbol_index;
    std::atomic<bool> m_is_indexing{false};
    std::atomic<std::size_t> m_files_indexed{0};
    std::atomic<std::size_t> m_total_files{0};
};

} // namespace Zenvra::Services::Indexing

