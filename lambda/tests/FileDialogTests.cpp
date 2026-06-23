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

class FakeTextSystem final : public lambdaui::TextSystem {
public:
  std::shared_ptr<lambdaui::TextLayout const>
  layout(lambdaui::AttributedString const& text, float maxWidth,
         lambdaui::TextLayoutOptions const& options) override {
    return makeLayout(text.utf8, maxWidth, options);
  }

  std::shared_ptr<lambdaui::TextLayout const>
  layout(std::string_view text, lambdaui::Font const&, lambdaui::Color const&, float maxWidth,
         lambdaui::TextLayoutOptions const& options) override {
    return makeLayout(text, maxWidth, options);
  }

  lambdaui::Size measure(lambdaui::AttributedString const& text, float maxWidth,
                       lambdaui::TextLayoutOptions const& options) override {
    return measuredSize(text.utf8, maxWidth, options);
  }

  lambdaui::Size measure(std::string_view text, lambdaui::Font const&, lambdaui::Color const&,
                       float maxWidth, lambdaui::TextLayoutOptions const& options) override {
    return measuredSize(text, maxWidth, options);
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override {
    return 0;
  }

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t, std::uint32_t, float,
                                           std::uint32_t& outWidth,
                                           std::uint32_t& outHeight,
                                           lambdaui::Point& outBearing) override {
    outWidth = 0;
    outHeight = 0;
    outBearing = {};
    return {};
  }

private:
  static lambdaui::Size measuredSize(std::string_view text, float maxWidth,
                                   lambdaui::TextLayoutOptions const&) {
    float width = std::max(4.f, static_cast<float>(text.size()) * 7.f);
    if (maxWidth > 0.f) {
      width = std::min(width, maxWidth);
    }
    return {width, 16.f};
  }

  static std::shared_ptr<lambdaui::TextLayout const>
  makeLayout(std::string_view text, float maxWidth, lambdaui::TextLayoutOptions const& options) {
    auto layout = std::make_shared<lambdaui::TextLayout>();
    layout->measuredSize = measuredSize(text, maxWidth, options);
    return layout;
  }
};

lambdaui::EnvironmentBinding testEnvironment() {
  return lambdaui::EnvironmentBinding{}.withValue<lambdaui::ThemeKey>(lambdaui::Theme::light());
}

struct FileDialogRoot {
  lambdaui::FileDialogMode mode = lambdaui::FileDialogMode::Open;
  std::filesystem::path initialDirectory;
  std::string initialName;
  std::vector<std::filesystem::path>* accepted = nullptr;

  lambdaui::Element body() const {
    return lambdaui::FileDialog{
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

bool hasTapInteraction(lambdaui::scenegraph::Interaction const& interaction) {
  auto const& data = lambdaui::interactionData(interaction);
  return static_cast<bool>(data.onTap) || static_cast<bool>(data.onTapWithModifiers);
}

void dispatchTap(lambdaui::scenegraph::Interaction const& interaction) {
  // Copy the handler before invoking: tapping a row can navigate and unmount
  // the node that owns the handler, destroying the closure mid-call.
  auto const& data = lambdaui::interactionData(interaction);
  if (data.onTapWithModifiers) {
    auto handler = data.onTapWithModifiers;
    handler(lambdaui::MouseButton::Left, lambdaui::Modifiers::None);
  } else if (data.onTap) {
    auto handler = data.onTap;
    handler(lambdaui::MouseButton::Left);
  }
}

void tap(lambdaui::scenegraph::SceneGraph& sceneGraph, lambdaui::Point point) {
  auto hit = lambdaui::scenegraph::hitTestInteraction(
      sceneGraph, point, [](lambdaui::scenegraph::Interaction const& interaction) {
        return hasTapInteraction(interaction);
      });
  INFO("tap point: " << point.x << ", " << point.y);
  REQUIRE(hit.has_value());
  dispatchTap(*hit->interaction);
}

constexpr lambdaui::Point kFirstEntry{220.f, 84.f};
constexpr lambdaui::Point kSecondEntry{220.f, 122.f};
constexpr lambdaui::Point kBackButton{27.f, 26.f};
constexpr lambdaui::Point kForwardButton{58.f, 26.f};
constexpr lambdaui::Point kPrimaryButton{610.f, 392.f};

} // namespace

TEST_CASE("FileDialog open navigation supports Back Forward and primary activation") {
  ScopedTempDir temp{"lambda-file-dialog-open"};
  makeOpenDialogFixture(temp);
  std::vector<std::filesystem::path> accepted;
  FakeTextSystem textSystem;

  auto runScenario = [&](std::vector<lambdaui::Point> taps,
                         std::filesystem::path const& expected) {
    accepted.clear();
    lambdaui::scenegraph::SceneGraph sceneGraph;
    lambdaui::MountRoot root{
        std::make_unique<lambdaui::TypedRootHolder<FileDialogRoot>>(
            std::in_place,
            FileDialogRoot{
                .mode = lambdaui::FileDialogMode::Open,
                .initialDirectory = temp.path,
                .accepted = &accepted,
            }),
        textSystem,
        testEnvironment(),
        {640.f, 420.f}};
    root.mount(sceneGraph);
    root.resize({640.f, 420.f}, sceneGraph);
    for (lambdaui::Point point : taps) {
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
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<FileDialogRoot>>(
          std::in_place,
          FileDialogRoot{
              .mode = lambdaui::FileDialogMode::Save,
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
