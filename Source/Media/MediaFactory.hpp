#pragma once

#include "MediaPlayer.hpp"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace Zenvra::Media {

class MediaFactory {
public:
    using PlayerCreator = std::function<MediaPlayerPtr()>;

    // Register a backend implementation (e.g. called by Drivers/Media/FFmpeg)
    static void register_backend(MediaBackendType type, PlayerCreator creator) {
        std::lock_guard<std::mutex> lock(get_mutex());
        get_registry()[type] = std::move(creator);
    }

    // Check if a specific backend is registered and available
    static bool is_backend_available(MediaBackendType type) {
        std::lock_guard<std::mutex> lock(get_mutex());
        if (type == MediaBackendType::Auto) {
            return !get_registry().empty();
        }
        return get_registry().contains(type);
    }

    // List all registered backends
    static std::vector<MediaBackendType> get_available_backends() {
        std::lock_guard<std::mutex> lock(get_mutex());
        std::vector<MediaBackendType> backends;
        for (const auto& [type, _] : get_registry()) {
            backends.push_back(type);
        }
        return backends;
    }

    // Create player instance
    static MediaPlayerPtr create_player(MediaBackendType type = MediaBackendType::Auto) {
        std::lock_guard<std::mutex> lock(get_mutex());
        auto& registry = get_registry();
        if (registry.empty()) {
            return nullptr;
        }

        if (type == MediaBackendType::Auto) {
            // Prioritize FFmpeg > MPV > WindowsMedia > AVFoundation
            const MediaBackendType priority[] = {
                MediaBackendType::FFmpeg,
                MediaBackendType::MPV,
                MediaBackendType::WindowsMedia,
                MediaBackendType::AVFoundation
            };
            for (auto candidate : priority) {
                if (registry.contains(candidate)) {
                    return registry[candidate]();
                }
            }
            return registry.begin()->second();
        }

        auto it = registry.find(type);
        if (it != registry.end()) {
            return it->second();
        }
        return nullptr;
    }

private:
    static std::map<MediaBackendType, PlayerCreator>& get_registry() {
        static std::map<MediaBackendType, PlayerCreator> s_registry;
        return s_registry;
    }

    static std::mutex& get_mutex() {
        static std::mutex s_mutex;
        return s_mutex;
    }
};

} // namespace Zenvra::Media
