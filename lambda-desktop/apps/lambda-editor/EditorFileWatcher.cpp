#include "EditorFileWatcher.hpp"

#include <system_error>
#include <utility>

namespace lambda_editor {

namespace {

bool sameDiskChange(EditorFileWatchEvent const& lhs, EditorFileWatchEvent const& rhs) {
  return lhs.kind == rhs.kind && lhs.path == rhs.path && lhs.signature == rhs.signature;
}

} // namespace

EditorFileSignature editorFileSignature(std::filesystem::path const& path) {
  EditorFileSignature signature;
  std::error_code ec;
  std::filesystem::file_status const status = std::filesystem::symlink_status(path, ec);
  if (ec || !std::filesystem::exists(status)) {
    return signature;
  }

  signature.exists = true;
  signature.writeTime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    signature.writeTime = {};
    ec.clear();
  }

  if (std::filesystem::is_regular_file(status)) {
    signature.size = std::filesystem::file_size(path, ec);
    if (ec) {
      signature.size = 0;
    }
  }

  return signature;
}

void EditorFileWatcher::reset(EditorDocument const& document) {
  pending_.reset();
  dismissed_.reset();
  if (!document.hasPath()) {
    watchedPath_.reset();
    baseline_ = {};
    return;
  }

  watchedPath_ = document.path();
  baseline_ = editorFileSignature(*watchedPath_);
}

void EditorFileWatcher::clear() {
  watchedPath_.reset();
  baseline_ = {};
  pending_.reset();
  dismissed_.reset();
}

std::optional<EditorFileWatchEvent> EditorFileWatcher::poll(EditorDocument const& document) {
  if (!document.hasPath()) {
    clear();
    return std::nullopt;
  }

  if (!watchedPath_ || *watchedPath_ != document.path()) {
    reset(document);
    return std::nullopt;
  }

  EditorFileSignature const current = editorFileSignature(*watchedPath_);
  if (current == baseline_) {
    pending_.reset();
    dismissed_.reset();
    return std::nullopt;
  }

  EditorFileWatchEvent event{
      .kind = current.exists ? EditorFileWatchEventKind::Modified
                             : EditorFileWatchEventKind::Missing,
      .path = *watchedPath_,
      .signature = current,
      .localDirty = document.isDirty(),
  };

  if (dismissed_ && sameDiskChange(*dismissed_, event)) {
    return std::nullopt;
  }

  if (pending_ && *pending_ == event) {
    return std::nullopt;
  }

  pending_ = event;
  return event;
}

void EditorFileWatcher::dismissPending() {
  if (pending_) {
    dismissed_ = pending_;
    pending_.reset();
  }
}

void EditorFileWatcher::clearPending() {
  pending_.reset();
}

std::optional<EditorFileWatchEvent> const& EditorFileWatcher::pending() const noexcept {
  return pending_;
}

std::optional<std::filesystem::path> const& EditorFileWatcher::watchedPath() const noexcept {
  return watchedPath_;
}

} // namespace lambda_editor
