#include "Platform/PlatformDialogs.h"

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

namespace Zenvra::Platform {

namespace {

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
  dialog->SetTitle(L"Select Folder");

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

} // namespace

bool folder_dialog_available() { return true; }

std::optional<std::filesystem::path> open_folder_dialog() {
  std::optional<std::filesystem::path> selected;
  std::atomic<bool> completed = false;

  std::thread dialog_thread([&]() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                     COINIT_DISABLE_OLE1DDE);
    const bool com_initialized = SUCCEEDED(init);

    if (com_initialized || init == RPC_E_CHANGED_MODE) {
      selected = show_modern_dialog();
    }

    if (com_initialized) {
      CoUninitialize();
    }
    completed = true;
  });

  // Modal wait loop: keep pumping Windows messages on the main UI thread so it
  // never freezes
  while (!completed.load(std::memory_order_relaxed)) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        PostQuitMessage(static_cast<int>(msg.wParam));
        completed = true;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }

  if (dialog_thread.joinable()) {
    dialog_thread.join();
  }

  return selected;
}

} // namespace Zenvra::Platform