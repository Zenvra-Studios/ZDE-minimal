#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Platform::Cocoa::Components
{

struct ItemTemplate
{
    std::string id;
    std::string name;             // e.g. "C++ File (.cpp)"
    std::string default_filename; // e.g. "Source.cpp"
    std::string extension;        // e.g. ".cpp"
    std::string category;         // e.g. "C/C++"
    std::string description;      // e.g. "Creates a file containing C++ source code."
    std::string icon_path;
    std::string default_content;
};

struct TemplateCategory
{
    std::string id;
    std::string name;
    std::string icon_path;
    std::vector<ItemTemplate> templates;
};

enum class PromptDialogMode
{
    AddNewItem,
    NewFolder,
    Rename,
    ConfirmDelete
};

class CocoaPromptDialog
{
public:
    CocoaPromptDialog();
    ~CocoaPromptDialog();

    CocoaPromptDialog(const CocoaPromptDialog&) = delete;
    CocoaPromptDialog& operator=(const CocoaPromptDialog&) = delete;

    bool open_new_file(
        const std::filesystem::path& target_dir,
        std::function<void(const std::string& filename, const std::string& initial_content)> on_confirm);

    bool open_new_folder(
        const std::filesystem::path& target_dir,
        std::function<void(const std::string&)> on_confirm);

    bool open_rename(
        const std::filesystem::path& item_path,
        std::function<void(const std::string&)> on_confirm);

    bool open_delete(
        const std::filesystem::path& item_path,
        std::function<void()> on_confirm);

    void close() noexcept;
    void notify_window_closed() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

private:
    void* m_native_window = nullptr;   // NSWindow*
    void* m_native_view = nullptr;     // NSView*
    void* m_native_delegate = nullptr; // ZDEPromptWindowDelegate*
};

} // namespace Zenvra::Platform::Cocoa::Components
