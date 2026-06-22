#include "EditorFileWatcher.hpp"

#include <doctest/doctest.h>

#include <chrono>
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

void writeFile(std::filesystem::path const& path, std::string const& text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
  file.close();
  REQUIRE(file);
}

void bumpWriteTime(std::filesystem::path const& path, int seconds) {
  std::error_code ec;
  auto const next = std::filesystem::file_time_type::clock::now() + std::chrono::seconds(seconds);
  std::filesystem::last_write_time(path, next, ec);
  REQUIRE_FALSE(ec);
}

} // namespace

TEST_CASE("EditorFileWatcher detects external modifications and coalesces prompts") {
  std::filesystem::path const root = tempRoot("lambda-editor-file-watch-modified");
  std::filesystem::path const path = root / "note.txt";
  writeFile(path, "alpha\n");

  EditorDocumentResult opened = openDocument(path.string());
  REQUIRE(opened.ok);

  EditorFileWatcher watcher;
  watcher.reset(opened.document);
  CHECK(watcher.watchedPath() == path);
  CHECK_FALSE(watcher.poll(opened.document));

  writeFile(path, "beta\n");
  bumpWriteTime(path, 10);

  std::optional<EditorFileWatchEvent> event = watcher.poll(opened.document);
  REQUIRE(event);
  CHECK(event->kind == EditorFileWatchEventKind::Modified);
  CHECK(event->path == path);
  CHECK_FALSE(event->localDirty);

  CHECK_FALSE(watcher.poll(opened.document));

  watcher.dismissPending();
  CHECK_FALSE(watcher.pending());
  CHECK_FALSE(watcher.poll(opened.document));

  writeFile(path, "gamma\n");
  bumpWriteTime(path, 20);
  event = watcher.poll(opened.document);
  REQUIRE(event);
  CHECK(event->kind == EditorFileWatchEventKind::Modified);

  std::filesystem::remove_all(root);
}

TEST_CASE("EditorFileWatcher includes local dirty state without duplicating disk events") {
  std::filesystem::path const root = tempRoot("lambda-editor-file-watch-dirty");
  std::filesystem::path const path = root / "note.txt";
  writeFile(path, "alpha\n");

  EditorDocumentResult opened = openDocument(path.string());
  REQUIRE(opened.ok);

  EditorFileWatcher watcher;
  watcher.reset(opened.document);

  EditorDocument dirty = opened.document;
  dirty.setText("local edit\n");
  writeFile(path, "external edit\n");
  bumpWriteTime(path, 10);

  std::optional<EditorFileWatchEvent> event = watcher.poll(dirty);
  REQUIRE(event);
  CHECK(event->kind == EditorFileWatchEventKind::Modified);
  CHECK(event->localDirty);

  CHECK_FALSE(watcher.poll(dirty));

  std::filesystem::remove_all(root);
}

TEST_CASE("EditorFileWatcher reports missing files and resets stale watches") {
  std::filesystem::path const root = tempRoot("lambda-editor-file-watch-missing");
  std::filesystem::path const path = root / "note.txt";
  std::filesystem::path const otherPath = root / "other.txt";
  writeFile(path, "alpha\n");
  writeFile(otherPath, "other\n");

  EditorDocumentResult opened = openDocument(path.string());
  REQUIRE(opened.ok);

  EditorFileWatcher watcher;
  watcher.reset(opened.document);
  std::filesystem::remove(path);

  std::optional<EditorFileWatchEvent> event = watcher.poll(opened.document);
  REQUIRE(event);
  CHECK(event->kind == EditorFileWatchEventKind::Missing);
  CHECK(event->path == path);

  watcher.dismissPending();
  CHECK_FALSE(watcher.poll(opened.document));

  watcher.reset(EditorDocument::untitled());
  CHECK_FALSE(watcher.watchedPath());
  CHECK_FALSE(watcher.poll(EditorDocument::untitled()));

  EditorDocumentResult other = openDocument(otherPath.string());
  REQUIRE(other.ok);
  watcher.reset(other.document);
  CHECK(watcher.watchedPath() == otherPath);
  CHECK_FALSE(watcher.poll(other.document));

  std::filesystem::remove_all(root);
}

TEST_CASE("EditorFileWatcher ignores saves made by the same editor instance after reset") {
  std::filesystem::path const root = tempRoot("lambda-editor-file-watch-save");
  std::filesystem::path const path = root / "note.txt";
  writeFile(path, "alpha\n");

  EditorDocumentResult opened = openDocument(path.string());
  REQUIRE(opened.ok);

  EditorFileWatcher watcher;
  watcher.reset(opened.document);

  EditorDocument changed = opened.document;
  changed.setText("saved from this editor\n");
  EditorDocumentResult saved = saveDocument(changed);
  REQUIRE(saved.ok);
  watcher.reset(saved.document);

  CHECK_FALSE(watcher.poll(saved.document));

  std::filesystem::remove_all(root);
}
