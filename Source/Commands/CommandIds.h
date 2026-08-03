#pragma once

#include <string_view>

namespace Zenvra::Commands::CommandIds
{

inline constexpr std::string_view file_new = "zde.file.new";
inline constexpr std::string_view file_open = "zde.file.open";
inline constexpr std::string_view file_save = "zde.file.save";
inline constexpr std::string_view file_close = "zde.file.close";
inline constexpr std::string_view file_delete = "zde.file.delete";
inline constexpr std::string_view file_exit = "zde.file.exit";
inline constexpr std::string_view edit_undo = "zde.edit.undo";
inline constexpr std::string_view edit_redo = "zde.edit.redo";
inline constexpr std::string_view edit_cut = "zde.edit.cut";
inline constexpr std::string_view edit_copy = "zde.edit.copy";
inline constexpr std::string_view edit_paste = "zde.edit.paste";
inline constexpr std::string_view selection_select_all = "zde.selection.selectAll";
inline constexpr std::string_view view_explorer = "zde.view.explorer";
inline constexpr std::string_view view_search = "zde.view.search";
inline constexpr std::string_view view_output = "zde.view.output";
inline constexpr std::string_view view_problems = "zde.view.problems";
inline constexpr std::string_view window_reset_layout = "zde.window.resetLayout";
inline constexpr std::string_view window_toggle_fullscreen = "zde.window.toggleFullscreen";
inline constexpr std::string_view project_open = "zde.project.open";
inline constexpr std::string_view project_close = "zde.project.close";
inline constexpr std::string_view build_build_project = "zde.build.buildProject";
inline constexpr std::string_view run_start = "zde.run.start";
inline constexpr std::string_view help_about = "zde.help.about";

} // namespace Zenvra::Commands::CommandIds
