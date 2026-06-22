#include "EditorDocument.hpp"

#include <fstream>
#include <iterator>
#include <utility>

namespace lambda_editor {

namespace {

std::string pathStatusText(std::filesystem::path const& path) {
  return path.string();
}

} // namespace

EditorDocument EditorDocument::untitled() {
  return EditorDocument{};
}

EditorDocument EditorDocument::opened(std::filesystem::path path, std::string text) {
  EditorDocument document;
  document.path_ = std::move(path);
  document.text_ = std::move(text);
  document.hasPath_ = true;
  document.dirty_ = false;
  return document;
}

bool EditorDocument::hasPath() const noexcept {
  return hasPath_;
}

bool EditorDocument::isDirty() const noexcept {
  return dirty_;
}

std::filesystem::path const& EditorDocument::path() const noexcept {
  return path_;
}

std::string EditorDocument::pathText() const {
  return hasPath_ ? path_.string() : std::string{};
}

std::string const& EditorDocument::text() const noexcept {
  return text_;
}

std::string EditorDocument::displayName() const {
  if (!hasPath_) return "Untitled";
  std::string name = path_.filename().string();
  return name.empty() ? path_.string() : name;
}

void EditorDocument::setText(std::string text) {
  if (text_ == text) return;
  text_ = std::move(text);
  dirty_ = true;
}

void EditorDocument::markSaved(std::filesystem::path path) {
  path_ = std::move(path);
  hasPath_ = true;
  dirty_ = false;
}

EditorDocumentResult openDocument(std::string const& pathText) {
  if (pathText.empty()) {
    return {.status = "Enter a file path to open.", .needsPath = true};
  }

  std::filesystem::path const path(pathText);
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {.status = "Could not open " + pathStatusText(path)};
  }

  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (!file.eof() && file.fail()) {
    return {.status = "Could not read " + pathStatusText(path)};
  }

  return {
      .document = EditorDocument::opened(path, std::move(text)),
      .status = "Opened " + pathStatusText(path),
      .ok = true,
  };
}

EditorDocumentResult saveDocument(EditorDocument const& document) {
  if (!document.hasPath()) {
    return {
        .document = document,
        .status = "Choose a file path to save.",
        .needsPath = true,
    };
  }
  return saveDocumentAs(document, document.pathText());
}

EditorDocumentResult saveDocumentAs(EditorDocument const& document, std::string const& pathText) {
  if (pathText.empty()) {
    return {
        .document = document,
        .status = "Choose a file path to save.",
        .needsPath = true,
    };
  }

  std::filesystem::path const path(pathText);
  std::filesystem::path const parent = path.parent_path();
  std::error_code ec;
  if (!parent.empty() && !std::filesystem::is_directory(parent, ec)) {
    std::string message = ec ? ec.message() : "folder does not exist";
    return {
        .document = document,
        .status = "Could not save " + pathStatusText(path) + ": " + message,
    };
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return {.document = document, .status = "Could not save " + pathStatusText(path)};
  }

  file.write(document.text().data(), static_cast<std::streamsize>(document.text().size()));
  if (!file) {
    return {.document = document, .status = "Could not write " + pathStatusText(path)};
  }

  EditorDocument saved = document;
  saved.markSaved(path);
  return {
      .document = std::move(saved),
      .status = "Saved " + pathStatusText(path),
      .ok = true,
  };
}

} // namespace lambda_editor
