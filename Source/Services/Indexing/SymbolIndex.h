#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Indexing
{

enum class SymbolKind
{
    File,
    Module,
    Namespace,
    Package,
    Class,
    Method,
    Property,
    Field,
    Constructor,
    Enum,
    Interface,
    Function,
    Variable,
    Constant,
    String,
    Number,
    Boolean,
    Array,
    Object,
    Key,
    Null,
    EnumMember,
    Struct,
    Event,
    Operator,
    TypeParameter
};

struct IndexedSymbol
{
    std::string name;
    std::string container_name;
    SymbolKind kind{SymbolKind::Variable};
    std::filesystem::path file_path;
    uint32_t line{0};
    uint32_t character{0};
};

class SymbolIndex
{
public:
    SymbolIndex() = default;

    void add_symbol(IndexedSymbol symbol);

    void clear_file(const std::filesystem::path& file_path);

    void clear();

    [[nodiscard]] std::vector<IndexedSymbol> query(std::string_view query, std::size_t max_results = 50) const;

    [[nodiscard]] std::size_t size() const noexcept { return m_symbols.size(); }

private:
    std::vector<IndexedSymbol> m_symbols;
};

} // namespace Zenvra::Services::Indexing

