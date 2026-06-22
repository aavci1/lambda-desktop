#pragma once

#include "EditorDocument.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace lambda_editor {

enum class EditorFileWatchEventKind : std::uint8_t {
  Modified,
  Missing,
};

struct EditorFileSignature {
  bool exists = false;
  std::uintmax_t size = 0;
  std::filesystem::file_time_type writeTime{};

  bool operator==(EditorFileSignature const& other) const noexcept {
    if (exists != other.exists) {
      return false;
    }
    if (!exists) {
      return true;
    }
    return size == other.size && writeTime == other.writeTime;
  }
};

struct EditorFileWatchEvent {
  EditorFileWatchEventKind kind = EditorFileWatchEventKind::Modified;
  std::filesystem::path path;
  EditorFileSignature signature;
  bool localDirty = false;

  bool operator==(EditorFileWatchEvent const&) const = default;
};

[[nodiscard]] EditorFileSignature editorFileSignature(std::filesystem::path const& path);

class EditorFileWatcher {
public:
  void reset(EditorDocument const& document);
  void clear();

  [[nodiscard]] std::optional<EditorFileWatchEvent> poll(EditorDocument const& document);

  void dismissPending();
  void clearPending();

  [[nodiscard]] std::optional<EditorFileWatchEvent> const& pending() const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path> const& watchedPath() const noexcept;

private:
  std::optional<std::filesystem::path> watchedPath_;
  EditorFileSignature baseline_{};
  std::optional<EditorFileWatchEvent> pending_;
  std::optional<EditorFileWatchEvent> dismissed_;
};

} // namespace lambda_editor
