#pragma once

#include <filesystem>
#include <string>

namespace lambda_editor {

class EditorDocument {
public:
  static EditorDocument untitled();
  static EditorDocument opened(std::filesystem::path path, std::string text);

  [[nodiscard]] bool hasPath() const noexcept;
  [[nodiscard]] bool isDirty() const noexcept;
  [[nodiscard]] std::filesystem::path const& path() const noexcept;
  [[nodiscard]] std::string pathText() const;
  [[nodiscard]] std::string const& text() const noexcept;
  [[nodiscard]] std::string displayName() const;

  void setText(std::string text);
  void markSaved(std::filesystem::path path);

  bool operator==(EditorDocument const&) const = default;

private:
  std::filesystem::path path_;
  std::string text_;
  bool hasPath_ = false;
  bool dirty_ = false;
};

struct EditorDocumentResult {
  EditorDocument document = EditorDocument::untitled();
  std::string status;
  bool ok = false;
  bool needsPath = false;
};

[[nodiscard]] EditorDocumentResult openDocument(std::string const& pathText);
[[nodiscard]] EditorDocumentResult saveDocument(EditorDocument const& document);
[[nodiscard]] EditorDocumentResult saveDocumentAs(EditorDocument const& document,
                                                  std::string const& pathText);

} // namespace lambda_editor
