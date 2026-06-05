#pragma once

/// \file Lambda/UI/Views/FileDialog.hpp
///
/// Framework-provided file open/save dialog content.

#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace lambda {

enum class FileDialogMode {
  Open,
  Save,
};

struct FileDialog : ViewModifiers<FileDialog> {
  FileDialogMode mode = FileDialogMode::Open;
  std::filesystem::path initialDirectory;
  std::string initialName;
  /// Return true when the selected path was accepted. Returning false keeps the dialog open.
  std::function<bool(std::filesystem::path)> onAccept;
  std::function<void()> onCancel;

  Element body() const;
};

} // namespace lambda
