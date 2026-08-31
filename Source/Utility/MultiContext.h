#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Platform
{
class IPlatformWindow;
}

namespace Zenvra::Application::ViewModels
{
class StudioViewModel;
}

namespace Zenvra::Utility
{

/**
 * @brief Representation of an individual window buffer execution context.
 */
struct WindowBufferContext
{
    std::uint64_t context_id = 0;
    std::string title = "ZDE Window";
    std::optional<std::filesystem::path> workspace_root;
    std::optional<std::filesystem::path> active_document_path;
    Platform::IPlatformWindow* window = nullptr;
    Application::ViewModels::StudioViewModel* view_model = nullptr;
    bool is_focused = false;
    bool is_closing = false;
};

/**
 * @brief Thread-safe MultiContext manager providing unlimited concurrent window contexts.
 */
class MultiContextManager
{
public:
    using ContextCallback = std::function<void(const WindowBufferContext&)>;

    [[nodiscard]] static MultiContextManager& instance() noexcept
    {
        static auto* s_instance = new MultiContextManager();
        return *s_instance;
    }

    /**
     * @brief Registers a new window buffer context.
     * @return Unique 64-bit context identifier.
     */
    std::uint64_t register_context(
        Platform::IPlatformWindow* window,
        Application::ViewModels::StudioViewModel* view_model = nullptr,
        std::optional<std::filesystem::path> workspace_root = std::nullopt)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::uint64_t id = ++m_next_context_id;

        WindowBufferContext ctx;
        ctx.context_id = id;
        ctx.window = window;
        ctx.view_model = view_model;
        ctx.workspace_root = std::move(workspace_root);
        ctx.is_focused = true;
        ctx.is_closing = false;

        m_contexts[id] = ctx;
        if (window != nullptr)
        {
            m_window_to_id[window] = id;
        }

        if (m_on_created)
        {
            m_on_created(ctx);
        }

        return id;
    }

    /**
     * @brief Unregisters a window buffer context by ID.
     */
    bool unregister_context(std::uint64_t context_id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_contexts.find(context_id);
        if (it == m_contexts.end())
        {
            return false;
        }

        WindowBufferContext copy = it->second;
        if (copy.window != nullptr)
        {
            m_window_to_id.erase(copy.window);
        }
        m_contexts.erase(it);

        if (m_on_destroyed)
        {
            m_on_destroyed(copy);
        }

        return true;
    }

    /**
     * @brief Unregisters a context associated with a specific platform window.
     */
    bool unregister_by_window(Platform::IPlatformWindow* window)
    {
        if (window == nullptr)
        {
            return false;
        }

        std::uint64_t found_id = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_window_to_id.find(window);
            if (it == m_window_to_id.end())
            {
                return false;
            }
            found_id = it->second;
        }

        return unregister_context(found_id);
    }

    /**
     * @brief Retrieves a copy of the context by ID if present.
     */
    [[nodiscard]] std::optional<WindowBufferContext> get_context(std::uint64_t context_id) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_contexts.find(context_id);
        if (it != m_contexts.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Retrieves a copy of the context by Platform window handle.
     */
    [[nodiscard]] std::optional<WindowBufferContext> get_context_by_window(
        Platform::IPlatformWindow* window) const
    {
        if (window == nullptr)
        {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_window_to_id.find(window);
        if (it != m_window_to_id.end())
        {
            auto ctx_it = m_contexts.find(it->second);
            if (ctx_it != m_contexts.end())
            {
                return ctx_it->second;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Updates the active workspace root of a context.
     */
    void set_workspace_root(std::uint64_t context_id, const std::filesystem::path& root)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_contexts.find(context_id);
        if (it != m_contexts.end())
        {
            it->second.workspace_root = root;
        }
    }

    /**
     * @brief Returns the count of all active window contexts.
     */
    [[nodiscard]] std::size_t count() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_contexts.size();
    }

    /**
     * @brief Returns a snapshot of all active window contexts.
     */
    [[nodiscard]] std::vector<WindowBufferContext> all_contexts() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<WindowBufferContext> result;
        result.reserve(m_contexts.size());
        for (const auto& [id, ctx] : m_contexts)
        {
            result.push_back(ctx);
        }
        return result;
    }

    /**
     * @brief Sets event listener for context creation.
     */
    void on_context_created(ContextCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_on_created = std::move(callback);
    }

    /**
     * @brief Sets event listener for context destruction.
     */
    void on_context_destroyed(ContextCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_on_destroyed = std::move(callback);
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_contexts.clear();
        m_window_to_id.clear();
        m_next_context_id = 0;
    }

private:
    MultiContextManager() = default;
    ~MultiContextManager() = default;

    mutable std::mutex m_mutex;
    std::uint64_t m_next_context_id = 0;
    std::unordered_map<std::uint64_t, WindowBufferContext> m_contexts;
    std::unordered_map<Platform::IPlatformWindow*, std::uint64_t> m_window_to_id;
    ContextCallback m_on_created;
    ContextCallback m_on_destroyed;
};

} // namespace Zenvra::Utility
