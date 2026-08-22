#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Zenvra::Platform
{

/// Opens a native folder selection dialog and returns the selected path.
/// Returns std::nullopt if the user cancels the dialog or if an error occurs.
[[nodiscard]] std::optional<std::filesystem::path> open_folder_dialog();

/// Opens a native text input dialog with a title and label.
/// Returns the user input string, or std::nullopt if cancelled.
[[nodiscard]] std::optional<std::string> input_dialog(
    const std::string& title, const std::string& label,
    const std::string& default_value = {});

/// Opens an input dialog for a git URL, then a folder picker for the destination,
/// runs `git clone`, and returns the resulting project folder path.
/// Returns std::nullopt if the user cancels or git clone fails.
[[nodiscard]] std::optional<std::filesystem::path> clone_repository_dialog();

/// Returns true when a native folder dialog backend is available on this
/// platform (e.g. kdialog / zenity / yad on Linux).
[[nodiscard]] bool folder_dialog_available();

} // namespace Zenvra::Platform
