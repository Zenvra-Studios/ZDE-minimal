#include <gtest/gtest.h>

#include "Terminal/TerminalExitDecoder.h"
#include "Terminal/TerminalPanelModel.h"
#include "Terminal/TerminalSession.h"
#include "Terminal/WindowsSecurityProbe.h"
#include "Utility/Doctor.h"

using namespace Zenvra::Terminal;
using namespace Zenvra::Utility;

TEST(TerminalExitDecoderTests, NormalExitCodeZero)
{
    const auto result = decode_terminal_exit(0, "C:\\Windows\\System32\\cmd.exe", true);
    EXPECT_TRUE(result.is_normal);
    EXPECT_FALSE(result.is_ntstatus);
    EXPECT_EQ(result.exit_code, 0U);
    EXPECT_EQ(result.hex_code, "0x00000000");
    EXPECT_NE(result.formatted_message.find("exited with code 0"), std::string::npos);
}

TEST(TerminalExitDecoderTests, NormalPseudoconsoleClose)
{
    const auto result = decode_terminal_exit(0xC0000B5B, "powershell.exe", true);
    EXPECT_TRUE(result.is_normal);
    EXPECT_TRUE(result.is_ntstatus);
    EXPECT_EQ(result.hex_code, "0xC0000B5B");
    EXPECT_NE(result.formatted_message.find("pseudoconsole closed"), std::string::npos);
}

TEST(TerminalExitDecoderTests, SmallExitCodeIsNotMisleadingAccessDenied)
{
    const auto result = decode_terminal_exit(5, "powershell.exe", true);
    EXPECT_FALSE(result.is_normal);
    EXPECT_FALSE(result.is_ntstatus);
    EXPECT_EQ(result.exit_code, 5U);
    EXPECT_EQ(result.hex_code, "0x00000005");
    // Crucial requirement: Must NOT claim ERROR_ACCESS_DENIED or check UAC
    EXPECT_EQ(result.formatted_message.find("ERROR_ACCESS_DENIED"), std::string::npos);
    EXPECT_EQ(result.formatted_message.find("check UAC"), std::string::npos);
    EXPECT_NE(result.formatted_message.find("[Process exited with code 5]"), std::string::npos);
    EXPECT_NE(result.formatted_message.find("ConPTY mode"), std::string::npos);
}

TEST(TerminalExitDecoderTests, AccessViolationNTSTATUS)
{
    const auto result = decode_terminal_exit(0xC0000005, "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", true);
    EXPECT_FALSE(result.is_normal);
    EXPECT_TRUE(result.is_ntstatus);
    EXPECT_EQ(result.hex_code, "0xC0000005");
    EXPECT_NE(result.summary.find("STATUS_ACCESS_VIOLATION"), std::string::npos);
    EXPECT_NE(result.detail.find("access violation"), std::string::npos);
    EXPECT_NE(result.formatted_message.find("0xC0000005"), std::string::npos);
    EXPECT_NE(result.formatted_message.find("powershell.exe"), std::string::npos);
}

TEST(TerminalExitDecoderTests, StackBufferOverrunNTSTATUS)
{
    const auto result = decode_terminal_exit(0xC0000409, "pwsh.exe", false);
    EXPECT_FALSE(result.is_normal);
    EXPECT_TRUE(result.is_ntstatus);
    EXPECT_EQ(result.hex_code, "0xC0000409");
    EXPECT_NE(result.summary.find("STATUS_STACK_BUFFER_OVERRUN"), std::string::npos);
    EXPECT_NE(result.formatted_message.find("Pipe mode"), std::string::npos);
}

TEST(TerminalExitDecoderTests, DllNotFoundNTSTATUS)
{
    const auto result = decode_terminal_exit(0xC0000135, "cmd.exe", true);
    EXPECT_FALSE(result.is_normal);
    EXPECT_TRUE(result.is_ntstatus);
    EXPECT_EQ(result.hex_code, "0xC0000135");
    EXPECT_NE(result.summary.find("STATUS_DLL_NOT_FOUND"), std::string::npos);
}

TEST(TerminalExitDecoderTests, DllInitFailedNTSTATUS)
{
    const auto result = decode_terminal_exit(0xC0000142, "cmd.exe", true);
    EXPECT_FALSE(result.is_normal);
    EXPECT_TRUE(result.is_ntstatus);
    EXPECT_EQ(result.hex_code, "0xC0000142");
    EXPECT_NE(result.summary.find("STATUS_DLL_INIT_FAILED"), std::string::npos);
}

TEST(TerminalExitDecoderTests, CodeIntegrityAccessDeniedNTSTATUS)
{
    const auto result = decode_terminal_exit(0xC0000022, "powershell.exe", true);
    EXPECT_FALSE(result.is_normal);
    EXPECT_TRUE(result.is_ntstatus);
    EXPECT_EQ(result.hex_code, "0xC0000022");
    EXPECT_NE(result.summary.find("STATUS_ACCESS_DENIED"), std::string::npos);
    EXPECT_NE(result.detail.find("Smart App Control"), std::string::npos);
}

TEST(TerminalExitDecoderTests, StackOverflowAndHeapCorruption)
{
    const auto stack_res = decode_terminal_exit(0xC00000FD);
    EXPECT_NE(stack_res.summary.find("STATUS_STACK_OVERFLOW"), std::string::npos);

    const auto heap_res = decode_terminal_exit(0xC0000374);
    EXPECT_NE(heap_res.summary.find("STATUS_HEAP_CORRUPTION"), std::string::npos);
}

TEST(WindowsSecurityProbeTests, ProbeExecutionAndCache)
{
    const auto& info1 = WindowsSecurityProbe::get_cached_info();
    const auto& info2 = WindowsSecurityProbe::get_cached_info();
    EXPECT_EQ(&info1, &info2); // Verifies reference identity of cached instance
}

TEST(WindowsSecurityProbeTests, StringConversions)
{
    EXPECT_EQ(WindowsSecurityProbe::sac_state_to_string(SACState::Disabled), "OFF (Disabled)");
    EXPECT_EQ(WindowsSecurityProbe::sac_state_to_string(SACState::Evaluation), "EVALUATION (Audit)");
    EXPECT_EQ(WindowsSecurityProbe::sac_state_to_string(SACState::Enforced), "ON (Enforced)");

    EXPECT_EQ(WindowsSecurityProbe::classification_to_string(SystemHealthClassification::Healthy), "HEALTHY");
    EXPECT_EQ(WindowsSecurityProbe::classification_to_string(SystemHealthClassification::Debloated), "DEBLOATED (Mod/Stripped)");
    EXPECT_EQ(WindowsSecurityProbe::classification_to_string(SystemHealthClassification::Broken), "BROKEN (Components Missing)");
}

TEST(DoctorTests, GenerateReportContent)
{
    const std::string report = Doctor::generate_report();
    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("ZDE DOCTOR: SYSTEM & RUNTIME REPORT"), std::string::npos);
    EXPECT_NE(report.find("[ZDE Information]"), std::string::npos);
    EXPECT_NE(report.find("[Operating System & Security Posture]"), std::string::npos);
    EXPECT_NE(report.find("[Terminal Subsystem]"), std::string::npos);
}

TEST(TerminalInputTests, BackspaceKeyMapping)
{
    TerminalPanelModel model;
    EXPECT_TRUE(model.create_session());

    TerminalSession* session = model.get_active_session();
    ASSERT_NE(session, nullptr);
    EXPECT_TRUE(session->is_running());

    // Send single character inputs
    EXPECT_TRUE(model.send_text("a"));
    EXPECT_TRUE(model.send_text("b"));
    EXPECT_TRUE(model.send_text("c"));

    // Send backspace (must be dispatched as single character deletion)
    EXPECT_TRUE(model.send_key(TerminalInputKey::Backspace));
    EXPECT_TRUE(model.send_key(TerminalInputKey::Backspace));

    // Send text "clear" (must be sent as literal characters, not clearing the entire terminal)
    EXPECT_TRUE(model.send_text("clear"));
    model.shutdown();
}

TEST(TerminalInputTests, PerCharacterVTDeletion)
{
    TerminalSession session;
    // Simulate terminal receiving output text: "hello world"
    session.consume_output("hello world");
    EXPECT_EQ(session.get_lines().size(), 1);
    EXPECT_EQ(session.get_lines()[0], "hello world");
    EXPECT_EQ(session.get_cursor_column(), 11);

    // Shell sends single backspace: \b \b (BS, Space, BS)
    session.consume_output("\b \b");
    EXPECT_EQ(session.get_cursor_column(), 10);
    EXPECT_EQ(session.get_lines()[0], "hello worl ");

    // Shell sends another single backspace: \b \b
    session.consume_output("\b \b");
    EXPECT_EQ(session.get_cursor_column(), 9);
    EXPECT_EQ(session.get_lines()[0], "hello wor  ");

    // Shell sends Erase in Line: \x1b[K
    session.consume_output("\x1b[K");
    EXPECT_EQ(session.get_cursor_column(), 9);
    EXPECT_EQ(session.get_lines()[0], "hello wor");

    // Shell sends Delete Character: \x1b[1P at current position
    session.consume_output("\b\x1b[1P");
    EXPECT_EQ(session.get_cursor_column(), 8);
    EXPECT_EQ(session.get_lines()[0], "hello wo");

    session.stop();
}



