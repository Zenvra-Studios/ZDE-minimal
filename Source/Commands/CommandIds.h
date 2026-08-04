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
inline constexpr std::string_view edit_toggle_comment = "zde.edit.toggleComment";
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
inline constexpr std::string_view build_debug = "zde.build.debug";
inline constexpr std::string_view build_release = "zde.build.release";
inline constexpr std::string_view edit_profiles = "zde.build.editProfiles";
inline constexpr std::string_view platform_x64 = "zde.platform.x64";
inline constexpr std::string_view platform_x86 = "zde.platform.x86";
inline constexpr std::string_view platform_win32 = "zde.platform.win32";
inline constexpr std::string_view platform_arm64 = "zde.platform.arm64";
inline constexpr std::string_view platform_aarch64 = "zde.platform.aarch64";
inline constexpr std::string_view platform_apple_arm = "zde.platform.appleArm";
inline constexpr std::string_view run_zde = "zde.run.zde";
inline constexpr std::string_view run_tests = "zde.run.tests";
inline constexpr std::string_view open_settings = "zde.settings.open";
inline constexpr std::string_view open_themes = "zde.themes.open";
inline constexpr std::string_view open_plugins = "zde.plugins.open";
inline constexpr std::string_view more_tools = "zde.tools.more";

} // namespace Zenvra::Commands::CommandIds
