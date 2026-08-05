#pragma once

#include <filesystem>
#include <optional>

namespace Zenvra::Platform
{

/// Opens a native folder selection dialog and returns the selected path.
/// Returns std::nullopt if the user cancels the dialog or if an error occurs.
[[nodiscard]] std::optional<std::filesystem::path> open_folder_dialog();

/// Returns true when a native folder dialog backend is available on this
/// platform (e.g. kdialog / zenity / yad on Linux).
[[nodiscard]] bool folder_dialog_available();

} // namespace Zenvra::Platform
