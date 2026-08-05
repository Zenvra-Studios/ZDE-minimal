#include "Platform/PlatformDialogs.h"

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <thread>

namespace Zenvra::Platform {

namespace {

std::wstring user_profile_directory() {
  wchar_t buffer[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr,
                                 SHGFP_TYPE_CURRENT, buffer))) {
    return buffer;
  }
  return L"C:\\";
}

/// Modern Vista+ folder picker. Requires COM to be initialized on the
/// calling thread.
std::optional<std::filesystem::path> show_modern_dialog() {
  IFileDialog *dialog = nullptr;
  HRESULT result =
      CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&dialog));
  if (FAILED(result) || dialog == nullptr) {
    if (dialog != nullptr) {
      dialog->Release();
    }
    return std::nullopt;
  }

  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    options |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
               FOS_FILEMUSTEXIST;
    static_cast<void>(dialog->SetOptions(options));
  }

  const HRESULT shown = dialog->Show(nullptr);
  if (FAILED(shown)) {
    dialog->Release();
    return std::nullopt;
  }

  IShellItem *item = nullptr;
  result = dialog->GetResult(&item);
  dialog->Release();
  if (FAILED(result) || item == nullptr) {
    if (item != nullptr) {
      item->Release();
    }
    return std::nullopt;
  }

  PWSTR path = nullptr;
  result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
  item->Release();
  if (FAILED(result) || path == nullptr) {
    if (path != nullptr) {
      CoTaskMemFree(path);
    }
    return std::nullopt;
  }

  std::filesystem::path selected{path};
  CoTaskMemFree(path);
  return selected;
}

struct BrowseData {
  std::wstring initial_directory;
};

int CALLBACK browse_directory_callback(HWND dialog_handle, UINT message,
                                       LPARAM l_param, LPARAM data) {
  static_cast<void>(l_param);
  if (message == BFFM_INITIALIZED && data != 0) {
    const BrowseData *browse_data = reinterpret_cast<const BrowseData *>(data);
    SendMessageW(
        dialog_handle, BFFM_SETSELECTIONW, TRUE,
        reinterpret_cast<LPARAM>(browse_data->initial_directory.c_str()));
  }
  return 0;
}

/// Legacy fallback tree dialog. Does not require client-side COM state.
std::optional<std::filesystem::path> show_classic_dialog() {
  BrowseData browse_data{};
  browse_data.initial_directory = user_profile_directory();

  BROWSEINFOW browse_info{};
  browse_info.hwndOwner = nullptr;
  browse_info.lpfn = browse_directory_callback;
  browse_info.lParam = reinterpret_cast<LPARAM>(&browse_data);
  browse_info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  browse_info.lpszTitle = L"Open a workspace folder";

  const PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse_info);
  if (pidl == nullptr) {
    return std::nullopt;
  }

  wchar_t path[MAX_PATH]{};
  const bool resolved = SHGetPathFromIDListW(pidl, path);
  CoTaskMemFree(pidl);
  if (!resolved) {
    return std::nullopt;
  }
  return std::filesystem::path{path};
}

} // namespace

bool folder_dialog_available() { return true; }

std::optional<std::filesystem::path> open_folder_dialog() {
  std::optional<std::filesystem::path> selected;

  std::thread dialog_thread([&]() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool com_initialized = SUCCEEDED(init);

    // Always use modern dialog, do not fallback to classic
    if (com_initialized || init == RPC_E_CHANGED_MODE) {
      selected = show_modern_dialog();
      if (com_initialized) {
        CoUninitialize();
      }
    }
  });
  dialog_thread.join();

//   if (!selected) {
//     selected = show_classic_dialog();
//   }
  return selected;
}

} // namespace Zenvra::Platform