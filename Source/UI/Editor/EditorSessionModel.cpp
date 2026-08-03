#include "UI/Editor/EditorSessionModel.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace Zenvra::UI::Editor
{

bool EditorSessionModel::open_file(const std::filesystem::path& path)
{
    const std::optional<TextFileSnapshot> snapshot = m_crud.read(path);
    if (!snapshot)
    {
        return false;
    }
    const auto existing = std::find_if(
        m_documents.begin(), m_documents.end(), [&snapshot](const EditorSessionDocument& item) {
            return item.path == snapshot->absolute_path;
        });
    if (existing != m_documents.end())
    {
        m_active_index = static_cast<std::size_t>(existing - m_documents.begin());
        return true;
    }

    if (m_active_index)
    {
        const EditorSessionDocument& active = m_documents[*m_active_index];
        if (active.temporary && !active.text.is_dirty() &&
            active.text.get_line_count() == 1 && active.text.get_line(0).empty())
        {
            remove_from_session(*m_active_index);
        }
    }

    EditorSessionDocument item;
    item.path = snapshot->absolute_path;
    item.text.replace_contents(
        snapshot->lines,
        snapshot->absolute_path.filename().string(),
        snapshot->breadcrumbs,
        snapshot->line_ending,
        snapshot->read_only);
    m_documents.push_back(std::move(item));
    m_active_index = m_documents.size() - 1;
    return true;
}

bool EditorSessionModel::create_buffer()
{
    ++m_untitled_counter;
    const std::string name = m_untitled_counter == 1
        ? "Untitled"
        : "Untitled-" + std::to_string(m_untitled_counter);

    EditorSessionDocument item;
    item.path = name;
    item.temporary = true;
    item.text.replace_contents({std::string{}}, name, {name}, "LF");
    m_documents.push_back(std::move(item));
    m_active_index = m_documents.size() - 1;
    return true;
}

bool EditorSessionModel::create_file()
{
    std::error_code error;
    std::filesystem::path directory;
    std::string extension = ".txt";
    if (const std::filesystem::path* active_path = get_active_path())
    {
        directory = active_path->parent_path();
        if (!active_path->extension().empty())
        {
            extension = active_path->extension().string();
        }
    }
    else
    {
        const std::filesystem::path current = std::filesystem::current_path(error);
        if (error)
        {
            return false;
        }
        directory = EditorFileSystem::find_project_root(current).value_or(current);
    }

    const std::filesystem::path new_path = m_crud.next_available_path(directory, extension);
    const std::optional<TextFileSnapshot> snapshot = m_crud.create(new_path);
    return snapshot.has_value() && open_file(snapshot->absolute_path);
}

bool EditorSessionModel::save_active_file()
{
    return m_active_index && save_file(*m_active_index);
}

bool EditorSessionModel::close_active_file()
{
    return m_active_index && close_file(*m_active_index);
}

bool EditorSessionModel::close_file(std::size_t index)
{
    if (index >= m_documents.size())
    {
        return false;
    }
    if (!m_documents[index].temporary &&
        m_documents[index].text.is_dirty() && !save_file(index))
    {
        return false;
    }
    remove_from_session(index);
    return true;
}

bool EditorSessionModel::delete_active_file()
{
    if (!m_active_index)
    {
        return false;
    }
    if (!m_documents[*m_active_index].temporary &&
        !m_crud.remove(m_documents[*m_active_index].path))
    {
        return false;
    }
    remove_from_session(*m_active_index);
    return true;
}

bool EditorSessionModel::rename_active_file(const std::filesystem::path& destination)
{
    if (!m_active_index || m_documents[*m_active_index].temporary)
    {
        return false;
    }
    EditorSessionDocument& item = m_documents[*m_active_index];
    if (item.text.is_dirty() && !save_file(*m_active_index))
    {
        return false;
    }
    if (!m_crud.rename(item.path, destination))
    {
        return false;
    }
    const std::optional<TextFileSnapshot> snapshot = m_crud.read(destination);
    if (!snapshot)
    {
        return false;
    }
    item.path = snapshot->absolute_path;
    item.text.replace_contents(
        snapshot->lines,
        snapshot->absolute_path.filename().string(),
        snapshot->breadcrumbs,
        snapshot->line_ending,
        snapshot->read_only);
    return true;
}

bool EditorSessionModel::activate_file(std::size_t index) noexcept
{
    if (index >= m_documents.size())
    {
        return false;
    }
    const bool changed = !m_active_index || *m_active_index != index;
    m_active_index = index;
    return changed;
}

TextDocumentModel* EditorSessionModel::get_active_document() noexcept
{
    return m_active_index ? &m_documents[*m_active_index].text : nullptr;
}

const TextDocumentModel* EditorSessionModel::get_active_document() const noexcept
{
    return m_active_index ? &m_documents[*m_active_index].text : nullptr;
}

const std::filesystem::path* EditorSessionModel::get_active_path() const noexcept
{
    return m_active_index && !m_documents[*m_active_index].temporary
        ? &m_documents[*m_active_index].path
        : nullptr;
}

std::optional<std::size_t> EditorSessionModel::get_active_index() const noexcept
{
    return m_active_index;
}

std::span<const EditorSessionDocument> EditorSessionModel::get_documents() const noexcept
{
    return m_documents;
}

bool EditorSessionModel::save_file(std::size_t index)
{
    if (index >= m_documents.size())
    {
        return false;
    }
    EditorSessionDocument& item = m_documents[index];
    if (item.text.is_read_only())
    {
        return false;
    }
    if (item.temporary)
    {
        std::error_code error;
        std::filesystem::path directory;
        const auto persisted = std::find_if(
            m_documents.begin(), m_documents.end(), [](const EditorSessionDocument& document) {
                return !document.temporary;
            });
        if (persisted != m_documents.end())
        {
            directory = persisted->path.parent_path();
        }
        else
        {
            const std::filesystem::path current = std::filesystem::current_path(error);
            if (error)
            {
                return false;
            }
            directory = EditorFileSystem::find_project_root(current).value_or(current);
        }

        const std::filesystem::path new_path = m_crud.next_available_path(directory, ".txt");
        const std::optional<TextFileSnapshot> snapshot = m_crud.create(new_path);
        if (!snapshot)
        {
            return false;
        }
        item.path = snapshot->absolute_path;
        item.temporary = false;
        item.text.update_file_identity(
            snapshot->absolute_path.filename().string(),
            snapshot->breadcrumbs,
            snapshot->line_ending);
    }
    const FooterEditorStatus status = item.text.get_status();
    if (!m_crud.update(item.path, item.text.get_lines(), status.line_ending))
    {
        return false;
    }
    item.text.mark_saved();
    return true;
}

void EditorSessionModel::remove_from_session(std::size_t index)
{
    m_documents.erase(m_documents.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_documents.empty())
    {
        m_active_index.reset();
    }
    else if (index >= m_documents.size())
    {
        m_active_index = m_documents.size() - 1;
    }
    else
    {
        m_active_index = index;
    }
}

} // namespace Zenvra::UI::Editor
