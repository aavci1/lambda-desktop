#include <doctest/doctest.h>

#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>
#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/TextInput.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

class LineHeightTextSystem final : public lambdaui::TextSystem {
public:
  std::shared_ptr<lambdaui::TextLayout const>
  layout(lambdaui::AttributedString const& text, float maxWidth,
         lambdaui::TextLayoutOptions const& options) override {
    return makeLayout(text.utf8, maxWidth, options);
  }

  std::shared_ptr<lambdaui::TextLayout const>
  layout(std::string_view text, lambdaui::Font const&, lambdaui::Color const&, float maxWidth,
         lambdaui::TextLayoutOptions const& options) override {
    return makeLayout(std::string{text}, maxWidth, options);
  }

  lambdaui::Size measure(lambdaui::AttributedString const& text, float maxWidth,
                       lambdaui::TextLayoutOptions const& options) override {
    return layout(text, maxWidth, options)->measuredSize;
  }

  lambdaui::Size measure(std::string_view text, lambdaui::Font const& font, lambdaui::Color const& color,
                       float maxWidth, lambdaui::TextLayoutOptions const& options) override {
    return layout(text, font, color, maxWidth, options)->measuredSize;
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override { return 0; }

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
  static std::shared_ptr<lambdaui::TextLayout> makeLayout(std::string const& text, float maxWidth,
                                                        lambdaui::TextLayoutOptions const& options) {
    auto layout = std::make_shared<lambdaui::TextLayout>();
    float const lineHeight = options.lineHeight > 0.f ? options.lineHeight : 20.f;
    std::size_t lineStart = 0;
    std::uint32_t lineIndex = 0;
    while (lineStart <= text.size()) {
      std::size_t const newline = text.find('\n', lineStart);
      std::size_t const lineEnd = newline == std::string::npos ? text.size() : newline;
      float const top = static_cast<float>(lineIndex) * lineHeight;
      layout->lines.push_back(lambdaui::TextLayout::LineRange{
          .ctLineIndex = lineIndex,
          .byteStart = static_cast<int>(lineStart),
          .byteEnd = static_cast<int>(lineEnd),
          .lineMinX = 0.f,
          .top = top,
          .bottom = top + lineHeight,
          .baseline = top + lineHeight * 0.75f,
      });
      ++lineIndex;
      if (newline == std::string::npos) {
        break;
      }
      lineStart = newline + 1;
    }
    layout->measuredSize = {
        std::max(120.f, maxWidth > 0.f ? maxWidth : 120.f),
        static_cast<float>(layout->lines.size()) * lineHeight,
    };
    layout->firstBaseline = layout->lines.empty() ? 0.f : layout->lines.front().baseline;
    layout->lastBaseline = layout->lines.empty() ? 0.f : layout->lines.back().baseline;
    return layout;
  }
};

lambdaui::EnvironmentBinding testEnvironment() {
  return lambdaui::EnvironmentBinding{}.withValue<lambdaui::ThemeKey>(lambdaui::Theme::light());
}

lambdaui::scenegraph::SceneNode const* findClippingViewport(lambdaui::scenegraph::SceneNode const& node) {
  if (node.kind() == lambdaui::scenegraph::SceneNodeKind::Rect) {
    auto const& rect = static_cast<lambdaui::scenegraph::RectNode const&>(node);
    if (rect.clipsContents() && rect.bounds().height > 1.f) {
      return &node;
    }
  }
  for (auto const& child : node.children()) {
    if (child) {
      if (lambdaui::scenegraph::SceneNode const* found = findClippingViewport(*child)) {
        return found;
      }
    }
  }
  return nullptr;
}

lambdaui::scenegraph::TextNode const* findTextNode(lambdaui::scenegraph::SceneNode const& node) {
  if (node.kind() == lambdaui::scenegraph::SceneNodeKind::Text) {
    return &static_cast<lambdaui::scenegraph::TextNode const&>(node);
  }
  for (auto const& child : node.children()) {
    if (child) {
      if (lambdaui::scenegraph::TextNode const* found = findTextNode(*child)) {
        return found;
      }
    }
  }
  return nullptr;
}

std::string lines(int count) {
  std::string out;
  for (int i = 0; i < count; ++i) {
    if (!out.empty()) {
      out += '\n';
    }
    out += "line " + std::to_string(i + 1);
  }
  return out;
}

} // namespace

TEST_CASE("multiline TextInput inside ScrollView grows rendered text bounds after value change") {
  struct Root {
    lambdaui::Reactive::Signal<std::string> text;
    lambdaui::Reactive::Signal<lambdaui::Point> offset;
    lambdaui::Reactive::Signal<lambdaui::Size> contentSize;

    lambdaui::Element body() const {
      lambdaui::TextInput::Style style = lambdaui::TextInput::Style::plain();
      style.lineHeight = 20.f;
      return lambdaui::ScrollView{
          .axis = lambdaui::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .contentSize = contentSize,
          .children = lambdaui::children(
              lambdaui::TextInput{
                  .value = text,
                  .style = style,
                  .multiline = true,
                  .wrapping = lambdaui::TextWrapping::Wrap,
                  .multilineHeight = {.fixed = 0.f, .minIntrinsic = 40.f},
              }.fill(lambdaui::FillStyle::solid(lambdaui::Colors::white))),
      };
    }
  };

  lambdaui::Reactive::Signal<std::string> text{"line 1"};
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{}};
  lambdaui::Reactive::Signal<lambdaui::Size> contentSize{};
  LineHeightTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(
          std::in_place, Root{text, offset, contentSize}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{240.f, 100.f},
  };

  root.mount(sceneGraph);

  lambdaui::scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  REQUIRE(contentSize.peek().height == doctest::Approx(40.f));

  text.set(lines(12));

  CHECK(contentSize.peek().height > viewport->size().height);
  CHECK(contentSize.peek().height == doctest::Approx(240.f));
  lambdaui::scenegraph::TextNode const* textNode = findTextNode(sceneGraph.root());
  REQUIRE(textNode != nullptr);
  CHECK(textNode->bounds().height == doctest::Approx(240.f));
}

TEST_CASE("multiline TextInput inside ScrollView renders full initial text bounds") {
  struct Root {
    lambdaui::Reactive::Signal<std::string> text;
    lambdaui::Reactive::Signal<lambdaui::Point> offset;
    lambdaui::Reactive::Signal<lambdaui::Size> contentSize;

    lambdaui::Element body() const {
      lambdaui::TextInput::Style style = lambdaui::TextInput::Style::plain();
      style.lineHeight = 20.f;
      return lambdaui::ScrollView{
          .axis = lambdaui::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .contentSize = contentSize,
          .children = lambdaui::children(
              lambdaui::TextInput{
                  .value = text,
                  .style = style,
                  .multiline = true,
                  .wrapping = lambdaui::TextWrapping::Wrap,
                  .multilineHeight = {.fixed = 0.f, .minIntrinsic = 40.f},
              }.fill(lambdaui::FillStyle::solid(lambdaui::Colors::white))),
      };
    }
  };

  lambdaui::Reactive::Signal<std::string> text{lines(12)};
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{}};
  lambdaui::Reactive::Signal<lambdaui::Size> contentSize{};
  LineHeightTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(
          std::in_place, Root{text, offset, contentSize}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{240.f, 100.f},
  };

  root.mount(sceneGraph);

  lambdaui::scenegraph::SceneNode const* viewport = findClippingViewport(sceneGraph.root());
  REQUIRE(viewport != nullptr);
  CHECK(contentSize.peek().height > viewport->size().height);
  CHECK(contentSize.peek().height == doctest::Approx(240.f));
  lambdaui::scenegraph::TextNode const* textNode = findTextNode(sceneGraph.root());
  REQUIRE(textNode != nullptr);
  CHECK(textNode->bounds().height == doctest::Approx(240.f));
}
