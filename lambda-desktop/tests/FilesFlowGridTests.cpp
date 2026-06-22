#include <doctest/doctest.h>

#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include "FilesFlowGrid.hpp"
#include "FilesApp.hpp"
#include "FilesFlowGridLayout.hpp"
#include "FilesListView.hpp"
#include "FilesStore.hpp"
#include "FilesTheme.hpp"
#include "UI/ViewLayout/ScrollLayout.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace lambda;
using namespace lambda_files;

class FakeTextSystem final : public TextSystem {
public:
  std::shared_ptr<TextLayout const> layout(AttributedString const&, float,
                                         TextLayoutOptions const&) override {
    return std::make_shared<TextLayout>();
  }

  std::shared_ptr<TextLayout const> layout(std::string_view, Font const&, Color const&, float,
                                         TextLayoutOptions const&) override {
    return std::make_shared<TextLayout>();
  }

  Size measure(AttributedString const&, float, TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  Size measure(std::string_view text, Font const&, Color const&, float width,
               TextLayoutOptions const&) override {
  (void)text;
  (void)width;
    return {48.f, 14.f};
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override { return 0; }

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t, std::uint32_t, float,
                                           std::uint32_t& outWidth, std::uint32_t& outHeight,
                                           Point& outBearing) override {
    outWidth = 0;
    outHeight = 0;
    outBearing = {};
    return {};
  }
};

EnvironmentBinding testEnvironment() {
  return EnvironmentBinding{}.withValue<ThemeKey>(Theme::light());
}

std::vector<FileEntry> makeEntries(int count, std::string const& directory = "/test/dir") {
  std::vector<FileEntry> entries;
  entries.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) {
    FileEntry entry;
    entry.name = "item-" + std::to_string(index);
    entry.path = std::filesystem::path(directory) / entry.name;
    entry.isDirectory = (index % 7) == 0;
    entries.push_back(std::move(entry));
  }
  return entries;
}

float constexpr kGridWidth = 780.f;

FilesFlowGridLayout const kLayout{};

scenegraph::SceneNode const* findClippingViewport(scenegraph::SceneNode const& node) {
  if (node.kind() == scenegraph::SceneNodeKind::Rect) {
    auto const& rect = static_cast<scenegraph::RectNode const&>(node);
    if (rect.clipsContents() && rect.bounds().height > 1.f) {
      return &node;
    }
  }
  for (auto const& child : node.children()) {
    if (child) {
      if (scenegraph::SceneNode const* found = findClippingViewport(*child)) {
        return found;
      }
    }
  }
  return nullptr;
}

scenegraph::SceneNode* findClippingViewport(scenegraph::SceneNode& node) {
  return const_cast<scenegraph::SceneNode*>(
      findClippingViewport(static_cast<scenegraph::SceneNode const&>(node)));
}

scenegraph::SceneNode const* scrollContentGroup(scenegraph::SceneNode const& viewport) {
  REQUIRE(viewport.children().size() >= 1);
  return viewport.children()[0].get();
}

scenegraph::SceneNode* scrollContentGroup(scenegraph::SceneNode& viewport) {
  REQUIRE(viewport.children().size() >= 1);
  return viewport.children()[0].get();
}

scenegraph::SceneNode const& gridForGroup(scenegraph::SceneNode const& gridRoot) {
  if (gridRoot.children().size() == 1 && gridRoot.children()[0]) {
    return *gridRoot.children()[0];
  }
  return gridRoot;
}

std::size_t gridRowCount(scenegraph::SceneNode const& gridRoot) {
  return gridForGroup(gridRoot).children().size();
}

bool hasTapInteraction(scenegraph::Interaction const& interaction) {
  auto const& data = interactionData(interaction);
  return static_cast<bool>(data.onTap) || static_cast<bool>(data.onTapWithModifiers);
}

void dispatchTap(scenegraph::Interaction const& interaction, MouseButton button,
                 Modifiers modifiers = Modifiers::None) {
  auto const& data = interactionData(interaction);
  if (data.onTapWithModifiers) {
    data.onTapWithModifiers(button, modifiers);
  } else if (data.onTap) {
    data.onTap(button);
  }
}

} // namespace

TEST_CASE("FilesFlowGrid layout column and row math") {
  CHECK(kLayout.columnCountForWidth(kGridWidth) == 6);
  CHECK(kLayout.rowCountForEntries(0, kGridWidth) == 0);
  CHECK(kLayout.rowCountForEntries(1, kGridWidth) == 1);
  CHECK(kLayout.rowCountForEntries(6, kGridWidth) == 1);
  CHECK(kLayout.rowCountForEntries(7, kGridWidth) == 2);
  CHECK(kLayout.rowCountForEntries(52, kGridWidth) == 9);

  Size const size52 = kLayout.contentSizeFor(kGridWidth, 52);
  CHECK(size52.width == doctest::Approx(kGridWidth));
  CHECK(size52.height ==
        doctest::Approx(9.f * FilesTheme::kGridTileH + 8.f * FilesTheme::kGridGapV));

  Size const size5 = kLayout.contentSizeFor(kGridWidth, 5);
  CHECK(size5.height == doctest::Approx(FilesTheme::kGridTileH));
  CHECK(size5.height < size52.height);

  std::vector<std::size_t> const rows = kLayout.rowIndicesFor(52, kGridWidth);
  REQUIRE(rows.size() == 9);
  CHECK(rows.front() == 0);
  CHECK(rows.back() == 8);
}

TEST_CASE("FilesStore display names truncate on UTF-8 scalar boundaries") {
  std::string name = "1234567890123456";
  name += "\xC3\xA9";
  name += "-tail";

  std::string expected = "1234567890123456";
  expected += "\xC3\xA9";
  expected += "...";
  CHECK(gridDisplayName(name) == expected);

  std::string invalid = "bad";
  invalid.push_back(static_cast<char>(0xC3));
  invalid += "name";
  CHECK(gridDisplayName(invalid) == std::string("bad\xEF\xBF\xBDname"));
}

TEST_CASE("FilesStore hides dotfiles unless requested") {
  std::filesystem::path const sandbox =
      std::filesystem::temp_directory_path() / "lambda-files-hidden-filter-test";
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox / "visible");
  std::filesystem::create_directories(sandbox / ".hidden");

  auto hasName = [](ListDirectoryResult const& result, std::string const& name) {
    return std::any_of(result.entries.begin(), result.entries.end(), [&](FileEntry const& entry) {
      return entry.name == name;
    });
  };

  ListDirectoryResult const defaultListing = listDirectory(sandbox);
  CHECK(hasName(defaultListing, "visible"));
  CHECK_FALSE(hasName(defaultListing, ".hidden"));

  ListDirectoryResult const hiddenListing = listDirectory(sandbox, true);
  CHECK(hasName(hiddenListing, "visible"));
  CHECK(hasName(hiddenListing, ".hidden"));

  std::filesystem::remove_all(sandbox);
}

TEST_CASE("FilesFlowGrid measure matches layout formula") {
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(52)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  Reactive::Signal<std::string> selectedPath{};

  FilesFlowGrid grid{
      .entries = entries,
      .listingKey = listingKey,
      .selectedPath = selectedPath,
  };

  LayoutConstraints constraints{
      .maxWidth = kGridWidth,
      .maxHeight = std::numeric_limits<float>::infinity(),
      .minWidth = 0.f,
      .minHeight = 0.f,
  };

  Size const measured = measureFilesFlowGrid(grid, constraints);
  Size const expected = kLayout.contentSizeFor(kGridWidth, 52);
  CHECK(measured.width == doctest::Approx(expected.width));
  CHECK(measured.height == doctest::Approx(expected.height));
}

TEST_CASE("FilesFlowGrid has no extent before it receives a real width") {
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(8)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  Reactive::Signal<std::string> selectedPath{};

  FilesFlowGrid grid{
      .entries = entries,
      .listingKey = listingKey,
      .selectedPath = selectedPath,
  };

  LayoutConstraints constraints{
      .maxWidth = std::numeric_limits<float>::infinity(),
      .maxHeight = std::numeric_limits<float>::infinity(),
      .minWidth = 0.f,
      .minHeight = 0.f,
  };

  Size const measured = measureFilesFlowGrid(grid, constraints);

  CHECK(measured.width == doctest::Approx(0.f));
  CHECK(measured.height == doctest::Approx(0.f));
}

TEST_CASE("FilesListView measures rows with header and viewport width") {
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(3)};
  Reactive::Signal<std::string> selectedPath{};

  FilesListView list{
      .entries = entries,
      .selectedPath = selectedPath,
  };

  LayoutConstraints constraints{
      .maxWidth = 640.f,
      .maxHeight = std::numeric_limits<float>::infinity(),
      .minWidth = 0.f,
      .minHeight = 0.f,
  };

  Size const measured = measureFilesListView(list, constraints);
  CHECK(measured.width == doctest::Approx(640.f));
  CHECK(measured.height == doctest::Approx(FilesTheme::kListHeaderHeight +
                                           3.f * FilesTheme::kListRowHeight +
                                           3.f * FilesTheme::kListRowGap));
}

TEST_CASE("FilesListView expands in a flex scroll viewport and preserves pointer modifiers") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    std::shared_ptr<int> activations;
    std::shared_ptr<std::vector<Modifiers>> activationModifiers;

    Element body() const {
      return HStack{
          .spacing = 0.f,
          .alignment = Alignment::Stretch,
          .children = children(
              Rectangle{}.width(220.f),
              Element{ScrollView{
                  .axis = ScrollAxis::Vertical,
                  .children = children(Element{FilesListView{
                      .entries = entries,
                      .selectedPath = Reactive::Signal<std::string>{},
                      .tapEntry = [activations = activations,
                                   activationModifiers = activationModifiers](
                                      FileEntry const&, Modifiers modifiers) {
                        ++*activations;
                        activationModifiers->push_back(modifiers);
                      },
                  }}),
              }}.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(10)};
  auto activations = std::make_shared<int>(0);
  auto activationModifiers = std::make_shared<std::vector<Modifiers>>();
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, activations, activationModifiers}),
                 textSystem,
                 testEnvironment(),
                 Size{820.f, 320.f}};

  root.mount(sceneGraph);
  root.resize(Size{820.f, 320.f}, sceneGraph);

  scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode const* content = scrollContentGroup(*viewport);
  REQUIRE(content->children().size() == 1);
  scenegraph::SceneNode const& listRoot = *content->children()[0];
  CHECK(listRoot.size().width == doctest::Approx(600.f).epsilon(0.01f));
  CHECK(listRoot.size().height == doctest::Approx(FilesTheme::kListHeaderHeight +
                                                  10.f * FilesTheme::kListRowHeight +
                                                  10.f * FilesTheme::kListRowGap).epsilon(0.01f));

  std::optional<scenegraph::InteractionHitResult> hit =
      scenegraph::hitTestInteraction(sceneGraph, Point{230.f, 52.f}, [](scenegraph::Interaction const& interaction) {
        return hasTapInteraction(interaction);
      });
  REQUIRE(hit.has_value());
  dispatchTap(*hit->interaction, MouseButton::Left, Modifiers::Ctrl | Modifiers::Shift);
  CHECK(*activations == 1);
  REQUIRE(activationModifiers->size() == 1);
  CHECK(activationModifiers->front() == (Modifiers::Ctrl | Modifiers::Shift));
}

TEST_CASE("ScrollView viewport taps carry pointer modifiers") {
  struct Root {
    std::shared_ptr<std::vector<Modifiers>> seenModifiers;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .onTap = [seen = seenModifiers](MouseButton button, Modifiers modifiers) {
            if (button == MouseButton::Right) {
              seen->push_back(modifiers);
            }
          },
          .children = children(Rectangle{}.size(100.f, 40.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  auto seen = std::make_shared<std::vector<Modifiers>>();
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{.seenModifiers = seen}),
                 textSystem,
                 testEnvironment(),
                 Size{200.f, 120.f}};

  root.mount(sceneGraph);

  auto hit = scenegraph::hitTestInteraction(
      sceneGraph, Point{20.f, 20.f}, [](scenegraph::Interaction const& interaction) {
        return static_cast<bool>(interactionData(interaction).onTapWithModifiers);
      });
  REQUIRE(hit.has_value());
  interactionData(*hit->interaction).onTapWithModifiers(MouseButton::Right, Modifiers::Alt);

  REQUIRE(seen->size() == 1);
  CHECK(seen->front() == Modifiers::Alt);
}

TEST_CASE("ScrollView indicators fit very small tracks") {
  lambda::layout::ScrollIndicatorMetrics const horizontal =
      lambda::layout::makeHorizontalIndicator(Point{}, Size{18.5f, 40.f}, Size{69.f, 40.f}, false);

  CHECK(horizontal.visible());
  CHECK(horizontal.width == doctest::Approx(12.5f));

  lambda::layout::ScrollIndicatorMetrics const vertical =
      lambda::layout::makeVerticalIndicator(Point{}, Size{40.f, 18.5f}, Size{40.f, 69.f}, false);

  CHECK(vertical.visible());
  CHECK(vertical.height == doctest::Approx(12.5f));
}

TEST_CASE("FilesFlowGrid mounted row count follows entry count") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    FilesFlowGrid grid;

    Element body() const {
      return Element{grid};
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(52)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place,
                     Root{
                         .entries = entries,
                         .listingKey = listingKey,
                         .grid =
                             FilesFlowGrid{
                                 .entries = entries,
                                 .listingKey = listingKey,
                                 .selectedPath = Reactive::Signal<std::string>{},
                             },
                     }),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 600.f}};

  root.mount(sceneGraph);
  root.resize(Size{kGridWidth, 600.f}, sceneGraph);
  CHECK(gridRowCount(sceneGraph.root()) == 9);

  entries.set(makeEntries(5, "/test/dir"));
  listingKey.set("/test/dir");

  root.resize(Size{kGridWidth, 600.f}, sceneGraph);
  CHECK(gridRowCount(sceneGraph.root()) == 1);

  entries.set(makeEntries(13, "/other"));
  listingKey.set("/other");

  root.resize(Size{kGridWidth, 600.f}, sceneGraph);
  CHECK(gridRowCount(sceneGraph.root()) == 3);
}

TEST_CASE("FilesFlowGrid expands in a flex scroll viewport and remains clickable") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    std::shared_ptr<int> activations;
    std::shared_ptr<std::vector<Modifiers>> activationModifiers;

    Element body() const {
      return HStack{
          .spacing = 0.f,
          .alignment = Alignment::Stretch,
          .children = children(
              Rectangle{}.width(220.f),
              Element{ScrollView{
                  .axis = ScrollAxis::Vertical,
                  .children = children(Element{FilesFlowGrid{
                      .entries = entries,
                      .listingKey = listingKey,
                      .selectedPath = Reactive::Signal<std::string>{},
                      .tapEntry = [activations = activations,
                                   activationModifiers = activationModifiers](
                                      FileEntry const&, Modifiers modifiers) {
                        ++*activations;
                        activationModifiers->push_back(modifiers);
                      },
                  }}),
              }}.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(10)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  auto activations = std::make_shared<int>(0);
  auto activationModifiers = std::make_shared<std::vector<Modifiers>>();
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, listingKey, activations, activationModifiers}),
                 textSystem,
                 testEnvironment(),
                 Size{820.f, 320.f}};

  root.mount(sceneGraph);
  root.resize(Size{820.f, 320.f}, sceneGraph);

  scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode const* content = scrollContentGroup(*viewport);
  REQUIRE(content->children().size() == 1);
  scenegraph::SceneNode const& gridRoot = *content->children()[0];
  CHECK(gridRoot.size().width == doctest::Approx(600.f).epsilon(0.01f));
  CHECK(gridRowCount(gridRoot) == kLayout.rowCountForEntries(10, 600.f));
  CHECK(gridRowCount(gridRoot) > 1);

  std::optional<scenegraph::InteractionHitResult> hit =
      scenegraph::hitTestInteraction(sceneGraph, Point{230.f, 20.f}, [](scenegraph::Interaction const& interaction) {
        return hasTapInteraction(interaction);
      });
  REQUIRE(hit.has_value());
  dispatchTap(*hit->interaction, MouseButton::Left, Modifiers::Ctrl | Modifiers::Shift);
  CHECK(*activations == 1);
  REQUIRE(activationModifiers->size() == 1);
  CHECK(activationModifiers->front() == (Modifiers::Ctrl | Modifiers::Shift));
}

TEST_CASE("FilesFlowGrid expands inside the Files app shell layout") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    FilesFlowGrid filesGrid;

    Element body() const {
      return VStack{
          .spacing = 0.f,
          .alignment = Alignment::Stretch,
          .children = children(
              Rectangle{}.height(FilesTheme::kTitlebarHeight),
              HStack{
                  .spacing = 0.f,
                  .alignment = Alignment::Stretch,
                  .children = children(
                      Rectangle{}.width(FilesTheme::kSidebarWidth),
                      Rectangle{}.width(1.f),
                      Element{ScrollView{
                          .axis = ScrollAxis::Vertical,
                          .children = children(Show(
                              [] { return false; },
                              [] {
                                return Text{.text = "error"};
                              },
                              [&filesGrid = filesGrid] {
                                return Element{filesGrid}
                                    .padding(FilesTheme::kContentPadV, FilesTheme::kContentPadH,
                                             FilesTheme::kContentPadV, FilesTheme::kContentPadH);
                              })),
                      }}.flex(1.f, 1.f, 0.f)),
              }.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(80)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  FilesFlowGrid filesGrid{
      .entries = entries,
      .listingKey = listingKey,
      .selectedPath = Reactive::Signal<std::string>{},
  };
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place,
                     Root{
                         .entries = entries,
                         .listingKey = listingKey,
                         .filesGrid = filesGrid,
                     }),
                 textSystem,
                 testEnvironment(),
                 Size{1040.f, 680.f}};

  root.mount(sceneGraph);

  scenegraph::SceneNode* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode* content = scrollContentGroup(*viewport);
  REQUIRE(content->children().size() == 1);
  scenegraph::SceneNode const& gridRoot = *content->children()[0];
  float const gridWidth = 1040.f - FilesTheme::kSidebarWidth - 1.f -
                          2.f * FilesTheme::kContentPadH;

  REQUIRE(gridRoot.children().size() == 1);
  scenegraph::SceneNode const& paddedGrid = *gridRoot.children()[0];

  CHECK(paddedGrid.size().width == doctest::Approx(1040.f - FilesTheme::kSidebarWidth - 1.f).epsilon(0.01f));
  REQUIRE(paddedGrid.children().size() == 1);
  scenegraph::SceneNode const& grid = *paddedGrid.children()[0];
  CHECK(grid.size().width == doctest::Approx(gridWidth).epsilon(0.01f));
  CHECK(kLayout.columnCountForWidth(grid.size().width) >= 6);
  CHECK(gridRowCount(grid) == kLayout.rowCountForEntries(80, gridWidth));
  REQUIRE(content->size().height > viewport->size().height);

  auto const* scrollInteraction = interactionData(*viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(Vec2{0.f, -120.f});

  CHECK(content->position().y == doctest::Approx(-120.f));

  root.resize(Size{820.f, 680.f}, sceneGraph);
  float const resizedGridWidthForMeasure = 820.f - FilesTheme::kSidebarWidth - 1.f -
                                           2.f * FilesTheme::kContentPadH;
  measureFilesFlowGrid(
      filesGrid,
      LayoutConstraints{
          .maxWidth = resizedGridWidthForMeasure,
          .maxHeight = std::numeric_limits<float>::infinity(),
          .minWidth = 0.f,
          .minHeight = 0.f,
      });

  scenegraph::SceneNode* resizedViewport = findClippingViewport(sceneGraph.root());
  REQUIRE(resizedViewport != nullptr);
  scenegraph::SceneNode* resizedContent = scrollContentGroup(*resizedViewport);
  REQUIRE(resizedContent->children().size() == 1);
  scenegraph::SceneNode const& resizedGridRoot = *resizedContent->children()[0];
  float const resizedGridWidth = 820.f - FilesTheme::kSidebarWidth - 1.f -
                                 2.f * FilesTheme::kContentPadH;

  REQUIRE(resizedGridRoot.children().size() == 1);
  scenegraph::SceneNode const& resizedPaddedGrid = *resizedGridRoot.children()[0];
  CHECK(resizedPaddedGrid.size().width ==
        doctest::Approx(820.f - FilesTheme::kSidebarWidth - 1.f).epsilon(0.01f));
  REQUIRE(resizedPaddedGrid.children().size() == 1);
  scenegraph::SceneNode const& resizedGrid = *resizedPaddedGrid.children()[0];
  CHECK(resizedGrid.size().width == doctest::Approx(resizedGridWidth).epsilon(0.01f));
  CHECK(kLayout.columnCountForWidth(resizedGrid.size().width) == 4);
  CHECK(gridRowCount(resizedGrid) == kLayout.rowCountForEntries(80, resizedGridWidth));
}

TEST_CASE("FilesFlowGrid ScrollView content size tracks listing changes") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Size> contentSize;
    Reactive::Signal<Size> viewportSize;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .viewportSize = viewportSize,
          .contentSize = contentSize,
          .children = children(Element{FilesFlowGrid{
              .entries = entries,
              .listingKey = listingKey,
              .selectedPath = Reactive::Signal<std::string>{},
          }}),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(52)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  Reactive::Signal<Size> contentSize{};
  Reactive::Signal<Size> viewportSize{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place,
                     Root{.entries = entries,
                          .listingKey = listingKey,
                          .contentSize = contentSize,
                          .viewportSize = viewportSize}),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 320.f}};

  root.mount(sceneGraph);

  float const largeHeight = kLayout.contentSizeFor(kGridWidth, 52).height;
  float const smallHeight = kLayout.contentSizeFor(kGridWidth, 5).height;
  float const mediumHeight = kLayout.contentSizeFor(kGridWidth, 13).height;

  REQUIRE(largeHeight > mediumHeight);
  REQUIRE(mediumHeight > smallHeight);

  CHECK(contentSize.peek().height == doctest::Approx(largeHeight).epsilon(1.f));
  CHECK(viewportSize.peek().height == doctest::Approx(320.f).epsilon(1.f));
  CHECK(contentSize.peek().height > viewportSize.peek().height);

  scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode const* content = scrollContentGroup(*viewport);
  CHECK(content->size().height == doctest::Approx(largeHeight).epsilon(1.f));

  entries.set(makeEntries(5, "/test/dir"));
  listingKey.set("/test/dir");
  root.resize(Size{kGridWidth, 320.f}, sceneGraph);

  CHECK(contentSize.peek().height == doctest::Approx(smallHeight).epsilon(1.f));
  CHECK(content->size().height == doctest::Approx(smallHeight).epsilon(1.f));
  CHECK(contentSize.peek().height <= viewportSize.peek().height + 1.f);

  entries.set(makeEntries(13, "/other"));
  listingKey.set("/other");
  root.resize(Size{kGridWidth, 320.f}, sceneGraph);

  CHECK(contentSize.peek().height == doctest::Approx(mediumHeight).epsilon(1.f));
  CHECK(content->size().height == doctest::Approx(mediumHeight).epsilon(1.f));
  CHECK(mediumHeight == doctest::Approx(contentSize.peek().height).epsilon(1.f));
}

TEST_CASE("FilesFlowGrid ScrollView keeps child origins stable during scrolled content relayout") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Point> offset;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .scrollOffset = offset,
          .children = children(Element{FilesFlowGrid{
              .entries = entries,
              .listingKey = listingKey,
              .selectedPath = Reactive::Signal<std::string>{},
          }}),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(52)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  Reactive::Signal<Point> offset{Point{0.f, 40.f}};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, listingKey, offset}),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 320.f}};

  root.mount(sceneGraph);

  scenegraph::SceneNode* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode* content = scrollContentGroup(*viewport);
  REQUIRE(content->children().size() == 1);
  CHECK(content->position().y == doctest::Approx(-40.f));
  CHECK(content->children()[0]->position().y == doctest::Approx(0.f));

  entries.set(makeEntries(13, "/other"));
  listingKey.set("/other");
  REQUIRE(content->relayoutStoredConstraints());

  CHECK(content->position().y == doctest::Approx(-40.f));
  REQUIRE(content->children().size() == 1);
  CHECK(content->children()[0]->position().y == doctest::Approx(0.f));
  CHECK(offset.get().y == doctest::Approx(40.f));
}

TEST_CASE("FilesFlowGrid ScrollView clamps offset when listing shrinks") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Point> offset;
    Reactive::Signal<Size> contentSize;
    Reactive::Signal<Size> viewportSize;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .scrollOffset = offset,
          .viewportSize = viewportSize,
          .contentSize = contentSize,
          .children = children(Element{FilesFlowGrid{
              .entries = entries,
              .listingKey = listingKey,
              .selectedPath = Reactive::Signal<std::string>{},
          }}),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(52)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  Reactive::Signal<Point> offset{Point{0.f, 500.f}};
  Reactive::Signal<Size> contentSize{};
  Reactive::Signal<Size> viewportSize{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, listingKey, offset, contentSize, viewportSize}),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 320.f}};

  root.mount(sceneGraph);
  scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode const* content = scrollContentGroup(*viewport);
  if (viewport->hasLayoutConstraints()) {
    LayoutConstraints const constraints = viewport->layoutConstraints();
    CHECK(constraints.maxHeight < 1000.f);
    CHECK(constraints.minHeight < 1000.f);
  }
  REQUIRE(content->size().height > viewport->size().height);
  REQUIRE(offset.peek().y > 0.f);

  entries.set(makeEntries(5, "/test/dir"));
  listingKey.set("/test/dir");
  root.resize(Size{kGridWidth, 320.f}, sceneGraph);

  CHECK(content->size().height == doctest::Approx(FilesTheme::kGridTileH).epsilon(1.f));
  CHECK(content->size().height <= viewport->size().height + 1.f);
  CHECK(offset.peek().y == doctest::Approx(0.f));

  entries.set(makeEntries(52, "/test/dir"));
  listingKey.set("/test/dir");
  root.resize(Size{kGridWidth, 320.f}, sceneGraph);

  CHECK(content->size().height == doctest::Approx(kLayout.contentSizeFor(kGridWidth, 52).height).epsilon(1.f));
  CHECK(offset.peek().y == doctest::Approx(0.f));
}

TEST_CASE("FilesFlowGrid ScrollView can scroll after folder navigation without resize") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Point> offset;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .scrollOffset = offset,
          .dragScrollEnabled = true,
          .children = children(Element{FilesFlowGrid{
              .entries = entries,
              .listingKey = listingKey,
              .selectedPath = Reactive::Signal<std::string>{},
          }}),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(80, "/first")};
  Reactive::Signal<std::string> listingKey{"/first"};
  Reactive::Signal<Point> offset{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, listingKey, offset}),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 320.f}};

  root.mount(sceneGraph);

  scenegraph::SceneNode* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode* content = scrollContentGroup(*viewport);
  REQUIRE(content->size().height > viewport->size().height);

  auto const* scrollInteraction = interactionData(*viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(Vec2{0.f, -120.f});
  CHECK(content->position().y == doctest::Approx(-120.f));

  entries.set(makeEntries(80, "/second"));
  listingKey.set("/second");
  offset.set(Point{});

  CHECK(offset.peek().y == doctest::Approx(0.f));
  CHECK(content->position().y == doctest::Approx(0.f));
  REQUIRE(content->size().height > viewport->size().height);

  scrollInteraction->onScroll(Vec2{0.f, -120.f});
  CHECK(content->position().y == doctest::Approx(-120.f));
  CHECK(offset.peek().y == doctest::Approx(120.f));
}

TEST_CASE("FilesFlowGrid app shell hit-test scrolling survives folder navigation") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Point> offset;
    FilesFlowGrid filesGrid;

    Element body() const {
      return VStack{
          .spacing = 0.f,
          .alignment = Alignment::Stretch,
          .children = children(
              Rectangle{}.height(FilesTheme::kTitlebarHeight),
              HStack{
                  .spacing = 0.f,
                  .alignment = Alignment::Stretch,
                  .children = children(
                      Rectangle{}.width(FilesTheme::kSidebarWidth),
                      Rectangle{}.width(1.f),
                      Element{ScrollView{
                          .axis = ScrollAxis::Vertical,
                          .scrollOffset = offset,
                          .dragScrollEnabled = true,
                          .children = children(Show(
                              [] { return false; },
                              [] {
                                return Text{.text = "error"};
                              },
                              [&filesGrid = filesGrid] {
                                return Element{filesGrid}
                                    .padding(FilesTheme::kContentPadV, FilesTheme::kContentPadH,
                                             FilesTheme::kContentPadV, FilesTheme::kContentPadH);
                              })),
                      }}.flex(1.f, 1.f, 0.f)),
              }.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(80, "/first")};
  Reactive::Signal<std::string> listingKey{"/first"};
  Reactive::Signal<Point> offset{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place,
                     Root{
                         .entries = entries,
                         .listingKey = listingKey,
                         .offset = offset,
                         .filesGrid =
                             FilesFlowGrid{
                                 .entries = entries,
                                 .listingKey = listingKey,
                                 .selectedPath = Reactive::Signal<std::string>{},
                             },
                     }),
                 textSystem,
                 testEnvironment(),
                 Size{1040.f, 680.f}};

  root.mount(sceneGraph);

  scenegraph::SceneNode* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode* content = scrollContentGroup(*viewport);
  REQUIRE(content->size().height > viewport->size().height);

  Point const contentPoint{FilesTheme::kSidebarWidth + 40.f, FilesTheme::kTitlebarHeight + 40.f};
  auto firstHit = scenegraph::hitTestInteraction(sceneGraph, contentPoint, [](scenegraph::Interaction const& interaction) {
    return static_cast<bool>(interactionData(interaction).onScroll);
  });
  REQUIRE(firstHit.has_value());
  interactionData(*firstHit->interaction).onScroll(Vec2{0.f, -120.f});
  CHECK(content->position().y == doctest::Approx(-120.f));
  CHECK(offset.peek().y == doctest::Approx(120.f));

  entries.set(makeEntries(80, "/second"));
  listingKey.set("/second");
  offset.set(Point{});

  CHECK(offset.peek().y == doctest::Approx(0.f));
  CHECK(content->position().y == doctest::Approx(0.f));
  REQUIRE(content->size().height > viewport->size().height);

  auto secondHit = scenegraph::hitTestInteraction(sceneGraph, contentPoint, [](scenegraph::Interaction const& interaction) {
    return static_cast<bool>(interactionData(interaction).onScroll);
  });
  REQUIRE(secondHit.has_value());
  interactionData(*secondHit->interaction).onScroll(Vec2{0.f, -120.f});
  CHECK(content->position().y == doctest::Approx(-120.f));
  CHECK(offset.peek().y == doctest::Approx(120.f));
}

TEST_CASE("FilesAppRoot main scroll view survives sidebar folder navigation") {
  std::filesystem::path const sandbox =
      std::filesystem::temp_directory_path() / "lambda-files-scroll-navigation-test";
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox / "Desktop");
  std::filesystem::create_directories(sandbox / "Documents");
  std::filesystem::create_directories(sandbox / "Downloads");
  for (int index = 0; index < 120; ++index) {
    std::filesystem::create_directory(sandbox / "Downloads" /
                                      ("download-folder-" + std::to_string(index)));
  }

  ::setenv("HOME", sandbox.string().c_str(), 1);
  ::setenv("XDG_DOWNLOAD_DIR", (sandbox / "Downloads").string().c_str(), 1);

  FakeTextSystem textSystem;
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<FilesAppRoot>>(
                     std::in_place, FilesAppRoot{nullptr}),
                 textSystem,
                 testEnvironment(),
                 Size{1040.f, 680.f}};

  root.mount(sceneGraph);
  root.resize(Size{1040.f, 680.f}, sceneGraph);

  Point const contentPoint{FilesTheme::kSidebarWidth + 40.f,
                           FilesTheme::kTitlebarHeight + 80.f};
  auto initialScrollHit = scenegraph::hitTestInteraction(
      sceneGraph, contentPoint, [](scenegraph::Interaction const& interaction) {
        return static_cast<bool>(interactionData(interaction).onScroll);
      });
  REQUIRE(initialScrollHit.has_value());
  REQUIRE(initialScrollHit->node != nullptr);
  CHECK(initialScrollHit->node->size().height == doctest::Approx(632.f).epsilon(1.f));

  auto downloadsHit = scenegraph::hitTestInteraction(
      sceneGraph, Point{32.f, FilesTheme::kTitlebarHeight + FilesTheme::kSidePad + 98.f},
      [](scenegraph::Interaction const& interaction) {
        return hasTapInteraction(interaction);
      });
  REQUIRE(downloadsHit.has_value());
  dispatchTap(*downloadsHit->interaction, MouseButton::Left);

  auto scrollHit = scenegraph::hitTestInteraction(
      sceneGraph, contentPoint, [](scenegraph::Interaction const& interaction) {
        return static_cast<bool>(interactionData(interaction).onScroll);
      });
  REQUIRE(scrollHit.has_value());
  auto const* viewport = scrollHit->node;
  REQUIRE(viewport != nullptr);
  auto const& scrollInteraction = interactionData(*scrollHit->interaction);
  REQUIRE(scrollInteraction.onScroll);

  scenegraph::SceneNode const* content = scrollContentGroup(*viewport);
  REQUIRE(content->size().height > viewport->size().height);

  scrollInteraction.onScroll(Vec2{0.f, -120.f});
  CHECK(content->position().y == doctest::Approx(-120.f));

  std::filesystem::remove_all(sandbox);
}

TEST_CASE("FilesAppRoot survives resizing below its fixed sidebar width") {
  std::filesystem::path const sandbox =
      std::filesystem::temp_directory_path() / "lambda-files-tiny-resize-test";
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox / "Desktop");
  std::filesystem::create_directories(sandbox / "Documents");
  std::filesystem::create_directories(sandbox / "Downloads");

  ::setenv("HOME", sandbox.string().c_str(), 1);

  FakeTextSystem textSystem;
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<FilesAppRoot>>(
                     std::in_place, FilesAppRoot{nullptr}),
                 textSystem,
                 testEnvironment(),
                 Size{420.f, 360.f}};

  root.mount(sceneGraph);

  for (Size const tinySize : {Size{260.f, 220.f},
                             Size{180.f, 180.f},
                             Size{120.f, 96.f}}) {
    root.resize(tinySize, sceneGraph);
    CHECK(sceneGraph.root().size().width >= 0.f);
    CHECK(sceneGraph.root().size().height >= 0.f);
  }

  std::filesystem::remove_all(sandbox);
}

TEST_CASE("FilesFlowGrid large listing scrolls after folder navigation") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Point> offset;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .scrollOffset = offset,
          .dragScrollEnabled = true,
          .children = children(Element{FilesFlowGrid{
              .entries = entries,
              .listingKey = listingKey,
              .selectedPath = Reactive::Signal<std::string>{},
          }}),
      };
    }
  };

  int constexpr kLargeCount = 120;
  int constexpr kSmallCount = 8;
  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(kLargeCount, "/big")};
  Reactive::Signal<std::string> listingKey{"/big"};
  Reactive::Signal<Point> offset{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, listingKey, offset}),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 320.f}};

  root.mount(sceneGraph);

  scenegraph::SceneNode* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode* content = scrollContentGroup(*viewport);
  REQUIRE(content->children().size() == 1);
  scenegraph::SceneNode const& forGroup = gridForGroup(*content->children()[0]);
  int const largeRows = kLayout.rowCountForEntries(kLargeCount, kLayout.columnCountForWidth(kGridWidth));
  REQUIRE(forGroup.children().size() == static_cast<std::size_t>(largeRows));
  REQUIRE(content->size().height > viewport->size().height);

  float const scrollY = std::min(400.f, content->size().height - viewport->size().height);
  offset.set(Point{0.f, scrollY});
  CHECK(content->position().y == doctest::Approx(-scrollY));

  entries.set(makeEntries(kSmallCount, "/small"));
  listingKey.set("/small");

  int const smallRows = kLayout.rowCountForEntries(kSmallCount, kLayout.columnCountForWidth(kGridWidth));
  REQUIRE(forGroup.children().size() == static_cast<std::size_t>(smallRows));
  CHECK(content->position().y == doctest::Approx(0.f));

  entries.set(makeEntries(kLargeCount, "/big-again"));
  listingKey.set("/big-again");

  REQUIRE(content->size().height > viewport->size().height);
  CHECK(offset.peek().y == doctest::Approx(0.f));
  auto const* scrollInteraction = interactionData(*viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(Vec2{0.f, -80.f});
  CHECK(offset.peek().y == doctest::Approx(80.f));
  CHECK(content->position().y == doctest::Approx(-80.f));
}

TEST_CASE("FilesFlowGrid app shell content size tracks listing without window resize") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Size> contentSize;
    Reactive::Signal<Size> viewportSize;

    Element body() const {
      return VStack{
          .spacing = 0.f,
          .alignment = Alignment::Stretch,
          .children = children(
              Rectangle{}.height(FilesTheme::kTitlebarHeight),
              HStack{
                  .spacing = 0.f,
                  .alignment = Alignment::Stretch,
                  .children = children(
                      Rectangle{}.width(FilesTheme::kSidebarWidth),
                      Rectangle{}.width(1.f),
                      Element{ScrollView{
                          .axis = ScrollAxis::Vertical,
                          .viewportSize = viewportSize,
                          .contentSize = contentSize,
                          .children = children(Show(
                              [] { return false; },
                              [] { return Text{.text = "error"}; },
                              [entries = entries, listingKey = listingKey] {
                                return Element{FilesFlowGrid{
                                    .entries = entries,
                                    .listingKey = listingKey,
                                    .selectedPath = Reactive::Signal<std::string>{},
                                }}
                                    .padding(FilesTheme::kContentPadV, FilesTheme::kContentPadH,
                                             FilesTheme::kContentPadV, FilesTheme::kContentPadH);
                              })),
                      }}.flex(1.f, 1.f, 0.f)),
              }.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  float const shellWidth = 1040.f;
  float const gridWidth = shellWidth - FilesTheme::kSidebarWidth - 1.f -
                          FilesTheme::kContentPadH * 2.f;
  float const largeHeight = kLayout.contentSizeFor(gridWidth, 52).height +
                            FilesTheme::kContentPadV * 2.f;
  float const smallHeight = kLayout.contentSizeFor(gridWidth, 5).height +
                            FilesTheme::kContentPadV * 2.f;

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(52)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  Reactive::Signal<Size> contentSize{};
  Reactive::Signal<Size> viewportSize{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place,
                     Root{.entries = entries,
                          .listingKey = listingKey,
                          .contentSize = contentSize,
                          .viewportSize = viewportSize}),
                 textSystem,
                 testEnvironment(),
                 Size{shellWidth, 680.f}};

  root.mount(sceneGraph);

  scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode const* content = scrollContentGroup(*viewport);

  CHECK(contentSize.peek().height == doctest::Approx(largeHeight).epsilon(1.f));
  CHECK(content->size().height == doctest::Approx(largeHeight).epsilon(1.f));

  entries.set(makeEntries(5, "/other"));
  listingKey.set("/other");

  CHECK(contentSize.peek().height == doctest::Approx(smallHeight).epsilon(1.f));
  CHECK(content->size().height == doctest::Approx(smallHeight).epsilon(1.f));
}

TEST_CASE("FilesFlowGrid keeps rows visible when scrolled through many entries") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Point> offset;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .scrollOffset = offset,
          .children = children(Element{FilesFlowGrid{
              .entries = entries,
              .listingKey = listingKey,
              .selectedPath = Reactive::Signal<std::string>{},
          }}),
      };
    }
  };

  int constexpr kEntryCount = 300;
  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(kEntryCount)};
  Reactive::Signal<std::string> listingKey{"/test/downloads"};
  Reactive::Signal<Point> offset{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, listingKey, offset}),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 320.f}};

  root.mount(sceneGraph);
  root.resize(Size{kGridWidth, 320.f}, sceneGraph);

  scenegraph::SceneNode* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  scenegraph::SceneNode* content = scrollContentGroup(*viewport);
  float const contentHeight = kLayout.contentSizeFor(kGridWidth, kEntryCount).height;
  REQUIRE(content->size().height == doctest::Approx(contentHeight).epsilon(1.f));
  REQUIRE(content->size().height > viewport->size().height);

  REQUIRE(content->children().size() == 1);
  scenegraph::SceneNode const& forGroup = gridForGroup(*content->children()[0]);
  FilesFlowGridLayout const kLayout{};
  int const expectedColumns = kLayout.columnCountForWidth(kGridWidth);
  int const expectedRows = kLayout.rowCountForEntries(kEntryCount, expectedColumns);
  REQUIRE(expectedRows > 14);
  REQUIRE(forGroup.children().size() == static_cast<std::size_t>(expectedRows));

  float const rowStride = FilesTheme::kGridTileH + FilesTheme::kGridGapV;
  float const midScroll =
      std::max(0.f, content->size().height - viewport->size().height) * 0.45f;
  offset.set(Point{0.f, midScroll});

  float const bandTop = midScroll;
  float const bandBottom = midScroll + viewport->size().height;
  bool foundVisibleRow = false;
  for (std::unique_ptr<scenegraph::SceneNode> const& row : forGroup.children()) {
    if (!row) {
      continue;
    }
    float const rowTop = row->position().y;
    float const rowBottom = rowTop + row->size().height;
    if (rowBottom >= bandTop && rowTop <= bandBottom) {
      foundVisibleRow = true;
      break;
    }
  }
  REQUIRE(foundVisibleRow);

  int const targetRow = 20;
  float const targetScroll = static_cast<float>(targetRow) * rowStride;
  offset.set(Point{0.f, targetScroll});
  REQUIRE(static_cast<std::size_t>(targetRow) < forGroup.children().size());
  scenegraph::SceneNode const& rowNode = *forGroup.children()[static_cast<std::size_t>(targetRow)];
  float const rowTop = rowNode.position().y;
  float const rowBottom = rowTop + rowNode.size().height;
  CHECK(rowBottom >= targetScroll);
  CHECK(rowTop <= targetScroll + viewport->size().height);
}

TEST_CASE("FilesFlowGrid ScrollView drops stale content height after shrink") {
  struct Root {
    Reactive::Signal<std::vector<FileEntry>> entries;
    Reactive::Signal<std::string> listingKey;
    Reactive::Signal<Size> contentSize;

    Element body() const {
      return ScrollView{
          .axis = ScrollAxis::Vertical,
          .contentSize = contentSize,
          .children = children(Element{FilesFlowGrid{
              .entries = entries,
              .listingKey = listingKey,
              .selectedPath = Reactive::Signal<std::string>{},
          }}),
      };
    }
  };

  FakeTextSystem textSystem;
  Reactive::Signal<std::vector<FileEntry>> entries{makeEntries(52)};
  Reactive::Signal<std::string> listingKey{"/test/dir"};
  Reactive::Signal<Size> contentSize{};
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{std::make_unique<TypedRootHolder<Root>>(
                     std::in_place, Root{entries, listingKey, contentSize}),
                 textSystem,
                 testEnvironment(),
                 Size{kGridWidth, 400.f}};

  root.mount(sceneGraph);
  float const largeHeight = contentSize.peek().height;

  entries.set(makeEntries(5, "/test/dir"));
  listingKey.set("/test/dir");
  root.resize(Size{kGridWidth, 400.f}, sceneGraph);

  float const smallHeight = contentSize.peek().height;
  CHECK(smallHeight < largeHeight * 0.5f);

  scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  float const maxScroll = std::max(0.f, scrollContentGroup(*viewport)->size().height -
                                            viewport->size().height);
  CHECK(maxScroll < largeHeight * 0.5f);
}
