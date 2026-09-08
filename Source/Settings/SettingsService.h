#pragma once

#include "Settings/JsonSettingsStore.h"
#include "Settings/SettingsDefinition.h"
#include "Settings/SettingsEvent.h"
#include "Settings/SettingsSchema.h"
#include "Settings/SettingsStore.h"
#include "Settings/SettingsValue.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Settings
{

class SettingsService
{
public:
    static SettingsService& instance() noexcept;

    SettingsService();
    ~SettingsService();

    SettingsService(const SettingsService&) = delete;
    SettingsService& operator=(const SettingsService&) = delete;

    void initialize(
        const std::filesystem::path& user_settings_path = {},
        const std::filesystem::path& workspace_settings_path = {});

    void set_workspace_path(const std::filesystem::path& workspace_root);
    void close_workspace();

    [[nodiscard]] SettingsValue get(std::string_view id) const;

    template<typename T>
    [[nodiscard]] T get(std::string_view id) const
    {
        const SettingsValue val = get(id);
        if constexpr (std::is_same_v<T, bool>) {
            return val.as_bool();
        } else if constexpr (std::is_integral_v<T>) {
            return static_cast<T>(val.as_int());
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(val.as_float());
        } else if constexpr (std::is_same_v<T, std::string>) {
            return val.as_string();
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            return val.as_array();
        } else {
            return T{};
        }
    }

    void set(std::string_view id, SettingsValue value, SettingsScope scope = SettingsScope::User);
    [[nodiscard]] bool has(std::string_view id) const;
    [[nodiscard]] bool is_modified(std::string_view id, SettingsScope scope = SettingsScope::User) const;
    [[nodiscard]] SettingsScope get_effective_scope(std::string_view id) const;

    void reset(std::string_view id, SettingsScope scope = SettingsScope::User);
    void reset_category(std::string_view category, SettingsScope scope = SettingsScope::User);
    void reset_all(SettingsScope scope = SettingsScope::User);

    void register_setting(SettingDefinition definition);

    [[nodiscard]] SettingsSchema& get_schema() noexcept { return m_schema; }
    [[nodiscard]] const SettingsSchema& get_schema() const noexcept { return m_schema; }

    [[nodiscard]] std::size_t subscribe(SettingsChangeCallback callback);
    [[nodiscard]] std::size_t subscribe(std::string_view id, SettingsChangeCallback callback);
    void unsubscribe(std::size_t subscription_id);

    [[nodiscard]] static std::filesystem::path get_default_user_settings_path();

private:
    void register_default_settings();
    void notify_listeners(const SettingsChangedEvent& event);

    SettingsSchema m_schema;
    std::unique_ptr<SettingsStore> m_user_store;
    std::unique_ptr<SettingsStore> m_workspace_store;

    struct Subscription
    {
        std::size_t id = 0;
        std::optional<std::string> target_setting_id;
        SettingsChangeCallback callback;
    };

    mutable std::mutex m_mutex;
    std::vector<Subscription> m_subscriptions;
    std::size_t m_next_subscription_id = 1;
};

} // namespace Zenvra::Settings
