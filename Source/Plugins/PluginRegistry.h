#pragma once

#include "Plugins/Plugin.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Plugins
{

class PluginRegistry
{
public:
    PluginRegistry() = default;

    void register_plugin(std::shared_ptr<Plugin> plugin);
    void unregister_plugin(std::string_view id);

    bool enable_plugin(std::string_view id);
    bool disable_plugin(std::string_view id);

    [[nodiscard]] std::shared_ptr<Plugin> get_plugin(std::string_view id) const;
    [[nodiscard]] bool has_plugin(std::string_view id) const;

    [[nodiscard]] std::vector<std::shared_ptr<Plugin>> get_all_plugins() const;
    [[nodiscard]] std::vector<std::shared_ptr<Plugin>> get_enabled_plugins() const;

    bool load_from_disk(const std::filesystem::path& registry_file);
    bool save_to_disk(const std::filesystem::path& registry_file) const;

    void clear() noexcept;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<Plugin>> m_plugins;
};

} // namespace Zenvra::Plugins
