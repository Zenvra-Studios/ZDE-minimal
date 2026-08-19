#include <gtest/gtest.h>

#include "SourceControl/GitFlow.h"
#include "SourceControl/GitRepository.h"
#include "SourceControl/GitTypes.h"
#include "UI/Editor/WorkspaceSourceControlModel.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

class TempDirectoryGuard
{
public:
    TempDirectoryGuard()
    {
        const auto temp_dir = std::filesystem::temp_directory_path();
        const auto unique_name = "zde_git_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        m_path = temp_dir / unique_name;
        std::filesystem::create_directories(m_path);
    }

    ~TempDirectoryGuard()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    void write_file(const std::string& relative_path, const std::string& content)
    {
        const auto full_path = m_path / relative_path;
        std::filesystem::create_directories(full_path.parent_path());
        std::ofstream ofs(full_path);
        ofs << content;
        ofs.close();
    }

    [[nodiscard]] std::string read_file(const std::string& relative_path)
    {
        const auto full_path = m_path / relative_path;
        std::ifstream ifs(full_path);
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

private:
    std::filesystem::path m_path;
};

} // namespace

TEST(SourceControlTests, GitTypesStatusLetterMapping)
{
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Modified), "M");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Added), "A");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Deleted), "D");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Renamed), "R");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::TypeChange), "T");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Untracked), "U");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Ignored), "I");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Conflicted), "C");
    EXPECT_EQ(Zenvra::Git::git_file_status_letter(Zenvra::Git::GitFileStatus::Unmodified), "");
}

TEST(SourceControlTests, GitRepositoryInitOpenClose)
{
    TempDirectoryGuard temp;
    Zenvra::Git::GitRepository repo;

    EXPECT_FALSE(repo.is_open());
    EXPECT_TRUE(repo.init_repository(temp.path()));
    EXPECT_TRUE(repo.is_open());
    EXPECT_EQ(repo.get_workdir(), temp.path().string() + "/");
    EXPECT_FALSE(repo.get_git_dir().empty());

    repo.close();
    EXPECT_FALSE(repo.is_open());

    EXPECT_TRUE(repo.open(temp.path()));
    EXPECT_TRUE(repo.is_open());
}

TEST(SourceControlTests, GitRepositoryStageCommitAndHistory)
{
    TempDirectoryGuard temp;
    Zenvra::Git::GitRepository repo;
    ASSERT_TRUE(repo.init_repository(temp.path()));

    // Create a new file
    temp.write_file("hello.txt", "Hello World\nInitial commit content\n");

    // Status: should be untracked
    auto status = repo.get_status();
    EXPECT_EQ(status.untracked_items.size(), 1U);
    EXPECT_EQ(status.untracked_items[0].path, "hello.txt");
    EXPECT_EQ(status.untracked_items[0].status, Zenvra::Git::GitFileStatus::Untracked);
    EXPECT_TRUE(status.staged_items.empty());
    EXPECT_TRUE(status.unstaged_items.empty());

    // Stage the file
    EXPECT_TRUE(repo.stage_file("hello.txt"));
    status = repo.get_status();
    EXPECT_TRUE(status.untracked_items.empty());
    EXPECT_EQ(status.staged_items.size(), 1U);
    EXPECT_EQ(status.staged_items[0].path, "hello.txt");
    EXPECT_EQ(status.staged_items[0].status, Zenvra::Git::GitFileStatus::Added);

    // Unstage the file
    EXPECT_TRUE(repo.unstage_file("hello.txt"));
    status = repo.get_status();
    EXPECT_EQ(status.untracked_items.size(), 1U);
    EXPECT_TRUE(status.staged_items.empty());

    // Stage again and commit
    EXPECT_TRUE(repo.stage_file("hello.txt"));
    EXPECT_TRUE(repo.commit("Initial commit", "Test Author", "test@zenvra.dev"));

    // Status after commit: should be completely clean
    status = repo.get_status();
    EXPECT_TRUE(status.staged_items.empty());
    EXPECT_TRUE(status.unstaged_items.empty());
    EXPECT_TRUE(status.untracked_items.empty());
    EXPECT_FALSE(status.head_commit_sha.empty());
    EXPECT_EQ(status.head_commit_summary, "Initial commit");

    // Verify history
    auto history = repo.get_history(10);
    ASSERT_EQ(history.size(), 1U);
    EXPECT_EQ(history[0].message, "Initial commit");
    EXPECT_EQ(history[0].author_name, "Test Author");
    EXPECT_EQ(history[0].author_email, "test@zenvra.dev");
    EXPECT_EQ(history[0].sha, status.head_commit_sha);
}

TEST(SourceControlTests, GitRepositoryDiffAndDiscard)
{
    TempDirectoryGuard temp;
    Zenvra::Git::GitRepository repo;
    ASSERT_TRUE(repo.init_repository(temp.path()));

    temp.write_file("main.cpp", "#include <iostream>\nint main() { return 0; }\n");
    ASSERT_TRUE(repo.stage_file("main.cpp"));
    ASSERT_TRUE(repo.commit("feat: initial main.cpp"));

    // Modify file
    temp.write_file("main.cpp", "#include <iostream>\nint main() {\n    std::cout << \"Hello!\\n\";\n    return 0;\n}\n");

    auto status = repo.get_status();
    ASSERT_EQ(status.unstaged_items.size(), 1U);
    EXPECT_EQ(status.unstaged_items[0].status, Zenvra::Git::GitFileStatus::Modified);

    // Diff inspection
    auto diff = repo.get_file_diff("main.cpp", false);
    EXPECT_EQ(diff.path, "main.cpp");
    EXPECT_GT(diff.additions, 0U);
    EXPECT_FALSE(diff.hunks.empty());

    std::string unified = repo.get_file_diff_unified("main.cpp", false);
    EXPECT_NE(unified.find("std::cout"), std::string::npos);

    // Discard unstaged changes
    EXPECT_TRUE(repo.discard_file_changes("main.cpp"));
    status = repo.get_status();
    EXPECT_TRUE(status.unstaged_items.empty());
    EXPECT_EQ(temp.read_file("main.cpp"), "#include <iostream>\nint main() { return 0; }\n");
}

TEST(SourceControlTests, GitRepositoryBranchManagement)
{
    TempDirectoryGuard temp;
    Zenvra::Git::GitRepository repo;
    ASSERT_TRUE(repo.init_repository(temp.path()));

    temp.write_file("README.md", "# Test Project\n");
    ASSERT_TRUE(repo.stage_all());
    ASSERT_TRUE(repo.commit("Initial commit"));

    // Create and list branches
    EXPECT_TRUE(repo.create_branch("feature/awesome"));
    auto branches = repo.list_branches();
    EXPECT_GE(branches.size(), 2U);

    bool found_feature = false;
    for (const auto& b : branches)
    {
        if (b.name == "feature/awesome")
        {
            found_feature = true;
            EXPECT_FALSE(b.is_head);
        }
    }
    EXPECT_TRUE(found_feature);

    // Checkout feature branch
    EXPECT_TRUE(repo.checkout_branch("feature/awesome"));
    EXPECT_EQ(repo.get_active_branch(), "feature/awesome");

    // Checkout master/main
    EXPECT_TRUE(repo.checkout_branch("master") || repo.checkout_branch("main"));

    // Delete branch
    EXPECT_TRUE(repo.delete_branch("feature/awesome", true));
    branches = repo.list_branches();
    for (const auto& b : branches)
    {
        EXPECT_NE(b.name, "feature/awesome");
    }
}

TEST(SourceControlTests, GitFlowLifecycle)
{
    TempDirectoryGuard temp;
    Zenvra::Git::GitRepository repo;
    ASSERT_TRUE(repo.init_repository(temp.path()));

    temp.write_file("app.py", "print('hello')\n");
    ASSERT_TRUE(repo.stage_all());
    ASSERT_TRUE(repo.commit("Initial base commit"));

    Zenvra::Git::GitFlow flow(repo);

    // Feature flow
    EXPECT_TRUE(flow.start_feature("editor-ui"));
    EXPECT_EQ(repo.get_active_branch(), "feature/editor-ui");

    temp.write_file("ui.py", "class UI: pass\n");
    EXPECT_TRUE(repo.stage_all());
    EXPECT_TRUE(repo.commit("feat: add UI class"));

    EXPECT_TRUE(flow.finish_feature("editor-ui"));

    // Release flow
    EXPECT_TRUE(flow.start_release("1.0.0"));
    EXPECT_EQ(repo.get_active_branch(), "release/1.0.0");
    EXPECT_TRUE(flow.finish_release("1.0.0"));

    // Hotfix flow
    EXPECT_TRUE(flow.start_hotfix("1.0.1"));
    EXPECT_EQ(repo.get_active_branch(), "hotfix/1.0.1");
    EXPECT_TRUE(flow.finish_hotfix("1.0.1"));
}

TEST(SourceControlTests, WorkspaceSourceControlModelInteractions)
{
    TempDirectoryGuard temp;
    Zenvra::Git::GitRepository repo;
    ASSERT_TRUE(repo.init_repository(temp.path()));

    temp.write_file("file1.txt", "content 1\n");
    temp.write_file("file2.txt", "content 2\n");
    ASSERT_TRUE(repo.stage_all());
    ASSERT_TRUE(repo.commit("feat: add initial files"));

    Zenvra::UI::Editor::WorkspaceSourceControlModel model;
    model.set_workspace_root(temp.path());

    EXPECT_TRUE(model.is_git_repository());
    EXPECT_EQ(model.get_workspace_root(), temp.path());
    EXPECT_FALSE(model.get_active_branch().empty());
    EXPECT_EQ(model.get_total_changes_count(), 0U);

    // Add changes
    temp.write_file("file1.txt", "content 1 modified\n");
    temp.write_file("untracked.txt", "brand new file\n");
    model.refresh_status();

    EXPECT_EQ(model.get_total_changes_count(), 2U);

    // Visible rows inspection
    const auto& rows = model.get_visible_rows();
    EXPECT_FALSE(rows.empty());
    EXPECT_EQ(rows[0].kind, Zenvra::UI::Editor::SourceControlRowKind::RepositoryHeader);

    // Test Commit message input box interactions
    model.set_input_focused(true);
    model.insert_text("feat: update ");
    model.insert_text("file1");
    EXPECT_EQ(model.get_commit_message(), "feat: update file1");
    EXPECT_EQ(model.get_caret(), 18U);

    model.handle_backspace();
    EXPECT_EQ(model.get_commit_message(), "feat: update file");

    model.handle_home(false);
    EXPECT_EQ(model.get_caret(), 0U);

    model.handle_end(true);
    EXPECT_TRUE(model.has_selection());
    EXPECT_EQ(model.get_selected_text(), "feat: update file");

    // Commit through UI model
    EXPECT_TRUE(model.commit());
    EXPECT_TRUE(model.get_commit_message().empty());
    EXPECT_EQ(model.get_total_changes_count(), 0U);

    // Collapsing sections
    model.toggle_repo_collapsed();
    EXPECT_TRUE(model.is_repo_collapsed());
    model.toggle_repo_collapsed();
    EXPECT_FALSE(model.is_repo_collapsed());

    model.toggle_git_graph_collapsed();
    EXPECT_TRUE(model.is_git_graph_collapsed());
}
