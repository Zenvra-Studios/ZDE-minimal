#include "Settings/JsonSettingsStore.h"

#include <fstream>
#include <iostream>
#include <system_error>

namespace Zenvra::Settings
{

JsonSettingsStore::JsonSettingsStore(std::filesystem::path file_path)
    : m_file_path(std::move(file_path))
{
}

bool JsonSettingsStore::load()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();

    std::error_code ec;
    if (!std::filesystem::exists(m_file_path, ec)) {
        return true;
    }

    std::ifstream file(m_file_path);
    if (!file.is_open()) {
        return false;
    }

    try {
        nlohmann::json root;
        file >> root;

        if (root.is_object()) {
            for (auto it = root.begin(); it != root.end(); ++it) {
                m_cache[it.key()] = SettingsValue::from_json(it.value());
            }
        }
        return true;
    }
    catch (const std::exception& e) {
        // Handle corrupt file gracefully: create a backup copy and reset
        file.close();
        std::error_code copy_ec;
        const auto backup_path = m_file_path.string() + ".bak";
        std::filesystem::copy_file(
            m_file_path,
            backup_path,
            std::filesystem::copy_options::overwrite_existing,
            copy_ec
        );

        m_cache.clear();
        return false;
    }
}

bool JsonSettingsStore::save()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        const auto parent = m_file_path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        nlohmann::json root = nlohmann::json::object();
        for (const auto& [key, value] : m_cache) {
            root[key] = value.to_json();
        }

        const auto temp_path = m_file_path.string() + ".tmp";
        {
            std::ofstream temp_file(temp_path, std::ios::trunc);
            if (!temp_file.is_open()) {
                return false;
            }
            temp_file << root.dump(4);
        }

        std::error_code rename_ec;
        std::filesystem::rename(temp_path, m_file_path, rename_ec);
        if (rename_ec) {
            // Fallback to overwrite copy if rename fails across partitions
            std::error_code copy_ec;
            std::filesystem::copy_file(
                temp_path,
                m_file_path,
                std::filesystem::copy_options::overwrite_existing,
                copy_ec
            );
            std::filesystem::remove(temp_path, copy_ec);
            return !copy_ec;
        }

        return true;
    }
    catch (...) {
        return false;
    }
}

bool JsonSettingsStore::contains(std::string_view key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.find(std::string(key)) != m_cache.end();
}

std::optional<SettingsValue> JsonSettingsStore::get(std::string_view key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_cache.find(std::string(key));
    if (it != m_cache.end()) {
        return it->second;
    }
    return std::nullopt;
}

void JsonSettingsStore::set(std::string_view key, SettingsValue value)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache[std::string(key)] = std::move(value);
    }
    save();
}

void JsonSettingsStore::remove(std::string_view key)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.erase(std::string(key));
    }
    save();
}

void JsonSettingsStore::clear()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
    }
    save();
}

std::vector<std::string> JsonSettingsStore::get_keys() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> keys;
    keys.reserve(m_cache.size());
    for (const auto& [key, _] : m_cache) {
        keys.push_back(key);
    }
    return keys;
}

} // namespace Zenvra::Settings
