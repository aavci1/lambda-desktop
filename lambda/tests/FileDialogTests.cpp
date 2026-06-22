#include <doctest/doctest.h>

#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>
#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/FileDialog.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct ScopedTempDir {
  std::filesystem::path path;

  explicit ScopedTempDir(std::string const& prefix) {
    auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    // FileDialog canonicalizes directories (e.g. /var -> /private/var on
    // macOS); canonicalize here so accepted paths compare equal.
    std::error_code ec;
    auto const canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec && !canonical.empty()) {
      path = canonical;
    }
  }

  ~ScopedTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

class FakeTextSystem final : public lambda::TextSystem {
public:
  std::shared_ptr<lambda::TextLayout const>
  layout(lambda::AttributedString const& text, float maxWidth,
         lambda::TextLayoutOptions const& options) override {
    return makeLayout(text.utf8, maxWidth, options);
  }

  std::shared_ptr<lambda::TextLayout const>
  layout(std::string_view text, lambda::Font const&, lambda::Color const&, float maxWidth,
         lambda::TextLayoutOptions const& options) override {
    return makeLayout(text, maxWidth, options);
  }

  lambda::Size measure(lambda::AttributedString const& text, float maxWidth,
                       lambda::TextLayoutOptions const& options) override {
    return measuredSize(text.utf8, maxWidth, options);
  }

  lambda::Size measure(std::string_view text, lambda::Font const&, lambda::Color const&,
                       float maxWidth, lambda::TextLayoutOptions const& options) override {
    return measuredSize(text, maxWidth, options);
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override {
    return 0;
  }

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t, std::uint32_t, float,
                                           std::uint32_t& outWidth,
                                           std::uint32_t& outHeight,
                                           lambda::Point& outBearing) override {
    outWidth = 0;
    outHeight = 0;
    outBearing = {};
    return {};
  }

private:
  static lambda::Size measuredSize(std::string_view text, float maxWidth,
                                   lambda::TextLayoutOptions const&) {
    float width = std::max(4.f, static_cast<float>(text.size()) * 7.f);
    if (maxWidth > 0.f) {
      width = std::min(width, maxWidth);
    }
    return {width, 16.f};
  }

  static std::shared_ptr<lambda::TextLayout const>
  makeLayout(std::string_view text, float maxWidth, lambda::TextLayoutOptions const& options) {
    auto layout = std::make_shared<lambda::TextLayout>();
    layout->measuredSize = measuredSize(text, maxWidth, options);
    return layout;
  }
};

lambda::EnvironmentBinding testEnvironment() {
  return lambda::EnvironmentBinding{}.withValue<lambda::ThemeKey>(lambda::Theme::light());
}

struct FileDialogRoot {
  lambda::FileDialogMode mode = lambda::FileDialogMode::Open;
  std::filesystem::path initialDirectory;
  std::string initialName;
  std::vector<std::filesystem::path>* accepted = nullptr;

  lambda::Element body() const {
    return lambda::FileDialog{
               .mode = mode,
               .initialDirectory = initialDirectory,
               .initialName = initialName,
               .onAccept = [accepted = accepted](std::filesystem::path path) {
                 if (accepted) {
                   accepted->push_back(std::move(path));
                 }
                 return true;
               },
           }
        .size(640.f, 420.f);
  }
};

void writeFile(std::filesystem::path const& path) {
  std::ofstream out(path);
  out << "data";
}

void makeOpenDialogFixture(ScopedTempDir& temp) {
  std::filesystem::create_directories(temp.path / "child");
  writeFile(temp.path / "root.txt");
  writeFile(temp.path / "child" / "nested.txt");
}

bool hasTapInteraction(lambda::scenegraph::Interaction const& interaction) {
  auto const& data = lambda::interactionData(interaction);
  return static_cast<bool>(data.onTap) || static_cast<bool>(data.onTapWithModifiers);
}

void dispatchTap(lambda::scenegraph::Interaction const& interaction) {
  // Copy the handler before invoking: tapping a row can navigate and unmount
  // the node that owns the handler, destroying the closure mid-call.
  auto const& data = lambda::interactionData(interaction);
  if (data.onTapWithModifiers) {
    auto handler = data.onTapWithModifiers;
    handler(lambda::MouseButton::Left, lambda::Modifiers::None);
  } else if (data.onTap) {
    auto handler = data.onTap;
    handler(lambda::MouseButton::Left);
  }
}

void tap(lambda::scenegraph::SceneGraph& sceneGraph, lambda::Point point) {
  auto hit = lambda::scenegraph::hitTestInteraction(
      sceneGraph, point, [](lambda::scenegraph::Interaction const& interaction) {
        return hasTapInteraction(interaction);
      });
  INFO("tap point: " << point.x << ", " << point.y);
  REQUIRE(hit.has_value());
  dispatchTap(*hit->interaction);
}

constexpr lambda::Point kFirstEntry{220.f, 84.f};
constexpr lambda::Point kSecondEntry{220.f, 122.f};
constexpr lambda::Point kBackButton{27.f, 26.f};
constexpr lambda::Point kForwardButton{58.f, 26.f};
constexpr lambda::Point kPrimaryButton{610.f, 392.f};

} // namespace

TEST_CASE("FileDialog open navigation supports Back Forward and primary activation") {
  ScopedTempDir temp{"lambda-file-dialog-open"};
  makeOpenDialogFixture(temp);
  std::vector<std::filesystem::path> accepted;
  FakeTextSystem textSystem;

  auto runScenario = [&](std::vector<lambda::Point> taps,
                         std::filesystem::path const& expected) {
    accepted.clear();
    lambda::scenegraph::SceneGraph sceneGraph;
    lambda::MountRoot root{
        std::make_unique<lambda::TypedRootHolder<FileDialogRoot>>(
            std::in_place,
            FileDialogRoot{
                .mode = lambda::FileDialogMode::Open,
                .initialDirectory = temp.path,
                .accepted = &accepted,
            }),
        textSystem,
        testEnvironment(),
        {640.f, 420.f}};
    root.mount(sceneGraph);
    root.resize({640.f, 420.f}, sceneGraph);
    for (lambda::Point point : taps) {
      tap(sceneGraph, point);
    }
    REQUIRE(accepted.size() == 1);
    CHECK(accepted.back() == expected);
  };

  runScenario({kFirstEntry, kBackButton, kSecondEntry, kPrimaryButton},
              temp.path / "root.txt");
  runScenario({kFirstEntry, kBackButton, kForwardButton, kFirstEntry, kPrimaryButton},
              temp.path / "child" / "nested.txt");
}

TEST_CASE("FileDialog save accepts initial name without a filename field") {
  ScopedTempDir temp{"lambda-file-dialog-save"};
  std::vector<std::filesystem::path> accepted;
  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<FileDialogRoot>>(
          std::in_place,
          FileDialogRoot{
              .mode = lambda::FileDialogMode::Save,
              .initialDirectory = temp.path,
              .initialName = "draft.txt",
              .accepted = &accepted,
          }),
      textSystem,
      testEnvironment(),
      {640.f, 420.f}};
  root.mount(sceneGraph);
  root.resize({640.f, 420.f}, sceneGraph);

  tap(sceneGraph, kPrimaryButton);

  REQUIRE(accepted.size() == 1);
  CHECK(accepted.back() == temp.path / "draft.txt");
}
