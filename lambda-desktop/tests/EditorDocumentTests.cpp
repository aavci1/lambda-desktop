#include "EditorDocument.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

using namespace lambda_editor;

namespace {

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::string readFile(std::filesystem::path const& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("EditorDocument tracks untitled dirty and saved state") {
  EditorDocument document = EditorDocument::untitled();

  CHECK_FALSE(document.hasPath());
  CHECK_FALSE(document.isDirty());
  CHECK(document.displayName() == "Untitled");
  CHECK(document.pathText().empty());

  document.setText("hello");
  CHECK(document.isDirty());
  CHECK(document.text() == "hello");

  std::filesystem::path const path = "/tmp/example.txt";
  document.markSaved(path);
  CHECK(document.hasPath());
  CHECK_FALSE(document.isDirty());
  CHECK(document.path() == path);
  CHECK(document.pathText() == path.string());
  CHECK(document.displayName() == "example.txt");
}

TEST_CASE("EditorDocument opens and saves plain text files") {
  std::filesystem::path const root = tempRoot("lambda-editor-document-tests");
  std::filesystem::path const path = root / "note.txt";
  {
    std::ofstream file(path, std::ios::binary);
    file << "one\ntwo";
  }

  EditorDocumentResult opened = openDocument(path.string());
  REQUIRE(opened.ok);
  CHECK_FALSE(opened.document.isDirty());
  CHECK(opened.document.hasPath());
  CHECK(opened.document.path() == path);
  CHECK(opened.document.text() == "one\ntwo");

  EditorDocument changed = opened.document;
  changed.setText("updated");
  CHECK(changed.isDirty());

  EditorDocumentResult saved = saveDocument(changed);
  REQUIRE(saved.ok);
  CHECK_FALSE(saved.document.isDirty());
  CHECK(saved.document.path() == path);
  CHECK(readFile(path) == "updated");

  std::filesystem::remove_all(root);
}

TEST_CASE("EditorDocument save-as assigns path and rejects missing folders") {
  std::filesystem::path const root = tempRoot("lambda-editor-save-as-tests");
  std::filesystem::path const target = root / "saved.txt";

  EditorDocument document = EditorDocument::untitled();
  document.setText("draft");

  EditorDocumentResult saved = saveDocumentAs(document, target.string());
  REQUIRE(saved.ok);
  CHECK(saved.document.hasPath());
  CHECK_FALSE(saved.document.isDirty());
  CHECK(saved.document.path() == target);
  CHECK(readFile(target) == "draft");

  EditorDocument next = saved.document;
  next.setText("nope");
  std::filesystem::path const missingTarget = root / "missing" / "file.txt";
  EditorDocumentResult rejected = saveDocumentAs(next, missingTarget.string());
  CHECK_FALSE(rejected.ok);
  CHECK(rejected.document.isDirty());
  CHECK_FALSE(std::filesystem::exists(root / "missing"));

  std::filesystem::remove_all(root);
}

TEST_CASE("EditorDocument reports path-required operations") {
  EditorDocument document = EditorDocument::untitled();

  EditorDocumentResult open = openDocument("");
  CHECK_FALSE(open.ok);
  CHECK(open.needsPath);

  EditorDocumentResult save = saveDocument(document);
  CHECK_FALSE(save.ok);
  CHECK(save.needsPath);

  EditorDocumentResult saveAs = saveDocumentAs(document, "");
  CHECK_FALSE(saveAs.ok);
  CHECK(saveAs.needsPath);
}
