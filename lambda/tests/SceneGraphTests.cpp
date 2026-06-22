#include <Lambda/Core/Identity.hpp>
#include <Lambda/UI/Cursor.hpp>
#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <doctest/doctest.h>

#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/ImageNode.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/PathNode.hpp>
#include <Lambda/SceneGraph/RasterCacheNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/Renderer.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>

#include "SceneGraph/SceneBounds.hpp"
#include "SceneGraph/SceneNodeInternal.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace {

using namespace lambda;
using namespace lambda::scenegraph;

class DummyImage final : public Image {
  public:
    explicit DummyImage(Size size) : size_(size) {}

    Size size() const override { return size_; }

  private:
    Size size_{};
};

class CountingInteraction final : public Interaction {
  public:
    CountingInteraction(ComponentKey key, int* probes)
        : key_(std::move(key))
        , probes_(probes) {}

    ComponentKey const& stableTargetKey() const noexcept override {
        ++*probes_;
        return key_;
    }

    bool focusable() const override { return false; }
    bool isEmpty() const noexcept override { return false; }

  private:
    ComponentKey key_;
    int* probes_ = nullptr;
};

class CountingBoundsNode final : public SceneNode {
  public:
    CountingBoundsNode(Rect bounds, Rect localBounds, int* probes)
        : SceneNode(bounds)
        , localBounds_(localBounds)
        , probes_(probes) {}

    Rect localBounds() const noexcept override {
        if (probes_) {
            ++*probes_;
        }
        return localBounds_;
    }

  private:
    Rect localBounds_{};
    int* probes_ = nullptr;
};

class DpiOnlyCanvas final : public Canvas {
  public:
    Backend backend() const noexcept override { return Backend::Vulkan; }
    unsigned int windowHandle() const override { return 0; }
    void resize(int, int) override {}
    void updateDpiScale(float scaleX, float scaleY) override { dpiScale_ = std::max(scaleX, scaleY); }
    float dpiScale() const noexcept override { return dpiScale_; }
    void beginFrame() override {}
    void present() override {}
    void save() override {}
    void restore() override {}
    void setTransform(Mat3 const&) override {}
    void transform(Mat3 const&) override {}
    void translate(Point) override {}
    void translate(float, float) override {}
    void scale(float, float) override {}
    void scale(float) override {}
    void rotate(float) override {}
    void rotate(float, Point) override {}
    Mat3 currentTransform() const override { return Mat3::identity(); }
    void clipRect(Rect, CornerRadius const&, bool) override {}
    Rect clipBounds() const override { return Rect::sharp(0.f, 0.f, 1000.f, 1000.f); }
    bool quickReject(Rect) const override { return false; }
    void setOpacity(float opacity) override { opacity_ = opacity; }
    float opacity() const override { return opacity_; }
    void setBlendMode(BlendMode mode) override { blendMode_ = mode; }
    BlendMode blendMode() const override { return blendMode_; }
    void drawRect(Rect const&, CornerRadius const&, FillStyle const&, StrokeStyle const&, ShadowStyle const&) override {}
    void drawLine(Point, Point, StrokeStyle const&) override {}
    void drawPath(Path const&, FillStyle const&, StrokeStyle const&, ShadowStyle const&) override {}
    void drawCircle(Point, float, FillStyle const&, StrokeStyle const&) override {}
    void drawTextLayout(TextLayout const&, Point) override {}
    void drawImage(Image const&, Rect const&, Rect const&, CornerRadius const&, float) override {}
    void drawImageTiled(Image const&, Rect const&, CornerRadius const&, float) override {}
    void drawBackdropBlur(Rect const&, float, Color, CornerRadius const&) override {}
    void* gpuDevice() const override { return nullptr; }
    void clear(Color) override {}

  private:
    float dpiScale_ = 1.f;
    float opacity_ = 1.f;
    BlendMode blendMode_ = BlendMode::Normal;
};

class RecordingRenderer final : public Renderer {
  public:
    struct RectDraw {
        Rect rect {};
        Point translation {};
        float opacity = 1.f;
    };

    struct ClipDraw {
        Rect rect {};
        CornerRadius cornerRadius {};
        Point translation {};
    };

    struct PathDraw {
        Point translation {};
        float opacity = 1.f;
    };

    struct ImageDraw {
        Rect rect {};
        ImageFillMode fillMode = ImageFillMode::Cover;
        Point translation {};
    };

    std::vector<RectDraw> rectDraws;
    std::vector<Point> textTranslations;
    std::vector<ClipDraw> clipDraws;
    std::vector<PathDraw> pathDraws;
    std::vector<ImageDraw> imageDraws;
    mutable std::vector<Rect> quickRejects;
    std::function<bool(Rect)> quickRejectHandler;
    int saveCalls = 0;
    int restoreCalls = 0;

    void save() override {
        ++saveCalls;
        transforms_.push_back(transforms_.back());
        opacities_.push_back(opacities_.back());
    }
    void restore() override {
        ++restoreCalls;
        if (transforms_.size() > 1) {
            transforms_.pop_back();
            opacities_.pop_back();
        }
    }

    void translate(Point offset) override { transforms_.back() = transforms_.back() * Mat3::translate(offset); }
    void transform(Mat3 const &matrix) override { transforms_.back() = transforms_.back() * matrix; }
    void clipRect(Rect rect, CornerRadius const &cornerRadius, bool) override {
        clipDraws.push_back({rect, cornerRadius, transforms_.back().apply(Point {})});
    }
    bool quickReject(Rect rect) const override {
        quickRejects.push_back(rect);
        return quickRejectHandler ? quickRejectHandler(rect) : false;
    }
    void setOpacity(float opacity) override { opacities_.back() = opacity; }
    void setBlendMode(BlendMode) override {}

    void drawRect(Rect const &rect, CornerRadius const &, FillStyle const &, StrokeStyle const &, ShadowStyle const &) override {
        rectDraws.push_back({rect, transforms_.back().apply(Point {}), opacities_.back()});
    }

    void drawPath(Path const &, FillStyle const &, StrokeStyle const &, ShadowStyle const &) override {
        pathDraws.push_back({transforms_.back().apply(Point {}), opacities_.back()});
    }

    void drawTextLayout(TextLayout const &) override { textTranslations.push_back(transforms_.back().apply(Point {})); }

    void drawImage(Image const &, Rect const &rect, ImageFillMode fillMode) override {
        imageDraws.push_back({rect, fillMode, transforms_.back().apply(Point {})});
    }

  private:
    std::vector<Mat3> transforms_ {Mat3::identity()};
    std::vector<float> opacities_ {1.f};
};

class PreparedCountingRenderer final : public Renderer {
  public:
    explicit PreparedCountingRenderer(DpiOnlyCanvas* canvas = nullptr) : canvas_(canvas) {}

    struct PreparedMarker final : public PreparedRenderOps {
        explicit PreparedMarker(PreparedCountingRenderer &owner)
            : owner_(owner) {}

        bool replay(Renderer &) const override {
            ++owner_.replayCalls;
            owner_.replayTranslations.push_back(owner_.transforms_.back().apply(Point {}));
            return true;
        }

      private:
        PreparedCountingRenderer &owner_;
    };

    int prepareCalls = 0;
    int replayCalls = 0;
    int fallbackRectDraws = 0;
    std::vector<Point> replayTranslations;

    void save() override { transforms_.push_back(transforms_.back()); }
    void restore() override {
        if (transforms_.size() > 1) {
            transforms_.pop_back();
        }
    }
    void translate(Point offset) override { transforms_.back() = transforms_.back() * Mat3::translate(offset); }
    void transform(Mat3 const &matrix) override { transforms_.back() = transforms_.back() * matrix; }
    void clipRect(Rect, CornerRadius const &, bool) override {}
    bool quickReject(Rect) const override { return false; }
    void setOpacity(float) override {}
    void setBlendMode(BlendMode) override {}

    void drawRect(Rect const &, CornerRadius const &, FillStyle const &, StrokeStyle const &, ShadowStyle const &) override {
        ++fallbackRectDraws;
    }

    void drawPath(Path const &, FillStyle const &, StrokeStyle const &, ShadowStyle const &) override {}
    void drawTextLayout(TextLayout const &) override {}
    void drawImage(Image const &, Rect const &, ImageFillMode) override {}

    std::unique_ptr<PreparedRenderOps> prepare(SceneNode const &) override {
        ++prepareCalls;
        return std::make_unique<PreparedMarker>(*this);
    }

    Canvas* canvas() noexcept override { return canvas_; }

  private:
    DpiOnlyCanvas* canvas_ = nullptr;
    std::vector<Mat3> transforms_ {Mat3::identity()};
};

TEST_CASE("RectNode local bounds keep strokes inside and expand for shadows") {
    RectNode stroked(Rect {0.f, 0.f, 20.f, 10.f},
                     FillStyle::none(),
                     StrokeStyle::solid(Colors::blue, 4.f));
    CHECK(stroked.localBounds() == Rect::sharp(0.f, 0.f, 20.f, 10.f));

    RectNode shadowed(Rect {0.f, 0.f, 20.f, 10.f},
                      FillStyle::solid(Colors::white),
                      StrokeStyle::none(),
                      CornerRadius {},
                      ShadowStyle {.radius = 3.f, .offset = {1.f, 2.f}, .color = Colors::black});
    CHECK(shadowed.localBounds() == Rect::sharp(-2.f, -1.f, 26.f, 16.f));
}

TEST_CASE("SceneRenderer accumulates parent-space bounds as local translations") {
    auto root = std::make_unique<SceneNode>(Rect {10.f, 20.f, 300.f, 200.f});
    auto panel = std::make_unique<SceneNode>(Rect {15.f, 25.f, 120.f, 80.f});
    panel->appendChild(std::make_unique<RectNode>(Rect {5.f, 6.f, 100.f, 50.f}, FillStyle::solid(Colors::red)));
    root->appendChild(std::move(panel));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    REQUIRE(renderer.rectDraws.size() == 1);
    CHECK(renderer.rectDraws[0].translation == Point {30.f, 51.f});
    CHECK(renderer.rectDraws[0].rect == Rect::sharp(0.f, 0.f, 100.f, 50.f));
}

TEST_CASE("SceneRenderer skips stack pushes for identity group nodes") {
    auto root = std::make_unique<SceneNode>(Rect {10.f, 20.f, 300.f, 200.f});
    auto panel = std::make_unique<SceneNode>(Rect {15.f, 25.f, 120.f, 80.f});
    panel->appendChild(std::make_unique<RectNode>(Rect {5.f, 6.f, 100.f, 50.f}, FillStyle::solid(Colors::red)));
    root->appendChild(std::move(panel));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    CHECK(renderer.saveCalls == 1);
    CHECK(renderer.restoreCalls == 1);
    REQUIRE(renderer.rectDraws.size() == 1);
    CHECK(renderer.rectDraws[0].translation == Point {30.f, 51.f});
}

TEST_CASE("TextNode renders its stored layout in node-local space") {
    auto layout = std::make_shared<TextLayout>();
    layout->measuredSize = Size {80.f, 20.f};

    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 100.f});
    root->appendChild(std::make_unique<TextNode>(Rect {12.f, 18.f, 80.f, 20.f}, layout));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    REQUIRE(renderer.textTranslations.size() == 1);
    CHECK(renderer.textTranslations[0] == Point {12.f, 18.f});
}

TEST_CASE("TextNode prepared render ops are keyed by layout and dpi scale") {
    auto layout = std::make_shared<TextLayout>();
    layout->measuredSize = Size {80.f, 20.f};

    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 100.f});
    auto text = std::make_unique<TextNode>(Rect {12.f, 18.f, 80.f, 20.f}, layout);
    TextNode* textNode = text.get();
    root->appendChild(std::move(text));

    SceneGraph graph {std::move(root)};
    DpiOnlyCanvas canvas;
    PreparedCountingRenderer renderer {&canvas};
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);
    int const firstPrepareCount = renderer.prepareCalls;
    REQUIRE(firstPrepareCount > 0);

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == firstPrepareCount);

    canvas.updateDpiScale(2.f, 2.f);
    sceneRenderer.render(graph);
    int const dpiPrepareCount = renderer.prepareCalls;
    CHECK(dpiPrepareCount > firstPrepareCount);

    auto nextLayout = std::make_shared<TextLayout>();
    nextLayout->measuredSize = Size {72.f, 20.f};
    textNode->setLayout(nextLayout);
    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls > dpiPrepareCount);
}

TEST_CASE("RectNode clipping scopes child traversal to the node bounds") {
    auto layout = std::make_shared<TextLayout>();
    layout->measuredSize = Size {40.f, 16.f};

    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 120.f});
    auto container = std::make_unique<RectNode>(
        Rect {10.f, 20.f, 90.f, 60.f},
        FillStyle::none(),
        StrokeStyle::none(),
        CornerRadius {6.f}
    );
    container->setClipsContents(true);
    container->appendChild(std::make_unique<TextNode>(Rect {5.f, 7.f, 40.f, 16.f}, layout));
    root->appendChild(std::move(container));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    REQUIRE(renderer.clipDraws.size() == 1);
    CHECK(renderer.clipDraws[0].translation == Point {10.f, 20.f});
    CHECK(renderer.clipDraws[0].rect == Rect::sharp(0.f, 0.f, 90.f, 60.f));
    CHECK(renderer.clipDraws[0].cornerRadius == CornerRadius {6.f});
    REQUIRE(renderer.textTranslations.size() == 1);
    CHECK(renderer.textTranslations[0] == Point {15.f, 27.f});
}

TEST_CASE("PathNode renders through the scenegraph renderer") {
    Path path;
    path.rect(Rect {0.f, 0.f, 24.f, 12.f}, CornerRadius {4.f});

    auto root = std::make_unique<SceneNode>(Rect {8.f, 6.f, 100.f, 80.f});
    root->appendChild(std::make_unique<PathNode>(
        Rect {10.f, 14.f, 24.f, 12.f},
        path,
        FillStyle::solid(Colors::green),
        StrokeStyle::solid(Colors::black, 1.f),
        ShadowStyle::none()));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    REQUIRE(renderer.pathDraws.size() == 1);
    CHECK(renderer.pathDraws[0].translation == Point {18.f, 20.f});
}

TEST_CASE("ImageNode renders through the scenegraph renderer with its fill mode") {
    auto source = std::make_shared<DummyImage>(Size {320.f, 180.f});
    auto root = std::make_unique<SceneNode>(Rect {8.f, 6.f, 120.f, 90.f});
    root->appendChild(std::make_unique<ImageNode>(Rect {10.f, 14.f, 64.f, 40.f}, source, ImageFillMode::Fit));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    REQUIRE(renderer.imageDraws.size() == 1);
    CHECK(renderer.imageDraws[0].translation == Point {18.f, 20.f});
    CHECK(renderer.imageDraws[0].rect == Rect::sharp(0.f, 0.f, 64.f, 40.f));
    CHECK(renderer.imageDraws[0].fillMode == ImageFillMode::Fit);
}

TEST_CASE("RectNode subtree opacity multiplies through descendants") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 100.f});
    auto parent = std::make_unique<RectNode>(Rect {10.f, 12.f, 80.f, 40.f}, FillStyle::solid(Colors::red));
    parent->setOpacity(0.5f);
    auto child = std::make_unique<RectNode>(Rect {4.f, 6.f, 20.f, 10.f}, FillStyle::solid(Colors::blue));
    child->setOpacity(0.25f);
    parent->appendChild(std::move(child));
    root->appendChild(std::move(parent));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    REQUIRE(renderer.rectDraws.size() == 2);
    CHECK(renderer.rectDraws[0].opacity == doctest::Approx(0.5f));
    CHECK(renderer.rectDraws[1].opacity == doctest::Approx(0.125f));
}

TEST_CASE("Scenegraph hit testing honors node transforms") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 120.f});
    auto rect = std::make_unique<RectNode>(Rect {20.f, 30.f, 40.f, 20.f}, FillStyle::solid(Colors::red));
    auto interaction = std::make_unique<InteractionData>();
    interaction->stableTargetKey_ = ComponentKey {LocalId::fromString("scaled")};
    interaction->cursor = Cursor::Hand;
    rect->setInteraction(std::move(interaction));
    rect->setTransform(Mat3::scale(2.f, 2.f));
    root->appendChild(std::move(rect));

    SceneGraph graph {std::move(root)};

    auto hit = hitTestInteraction(graph, Point {80.f, 40.f});
    REQUIRE(hit.has_value());
    CHECK(hit->interaction->stableTargetKey() == ComponentKey {LocalId::fromString("scaled")});
    CHECK(hit->localPoint == Point {30.f, 5.f});

    CHECK_FALSE(hitTestInteraction(graph, Point {110.f, 40.f}).has_value());
}

TEST_CASE("Scenegraph hit testing reaches overflowing children outside non-clipping parents") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 160.f, 100.f});
    auto parent = std::make_unique<SceneNode>(Rect {20.f, 20.f, 40.f, 40.f});
    auto parentInteraction = std::make_unique<InteractionData>();
    parentInteraction->stableTargetKey_ = ComponentKey {LocalId::fromString("parent")};
    parentInteraction->onTap = [](MouseButton) {};
    parent->setInteraction(std::move(parentInteraction));

    auto child = std::make_unique<RectNode>(Rect {50.f, 0.f, 20.f, 20.f}, FillStyle::solid(Colors::blue));
    auto childInteraction = std::make_unique<InteractionData>();
    childInteraction->stableTargetKey_ = ComponentKey {LocalId::fromString("overflow-child")};
    childInteraction->onTap = [](MouseButton) {};
    child->setInteraction(std::move(childInteraction));
    parent->appendChild(std::move(child));
    root->appendChild(std::move(parent));

    SceneGraph graph {std::move(root)};

    auto parentHit = hitTestInteraction(graph, Point {30.f, 30.f});
    REQUIRE(parentHit.has_value());
    CHECK(parentHit->interaction->stableTargetKey() == ComponentKey {LocalId::fromString("parent")});

    auto childHit = hitTestInteraction(graph, Point {75.f, 25.f});
    REQUIRE(childHit.has_value());
    CHECK(childHit->interaction->stableTargetKey() == ComponentKey {LocalId::fromString("overflow-child")});
}

TEST_CASE("Scenegraph hit testing does not reach overflowing children through clipping parents") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 160.f, 100.f});
    auto parent = std::make_unique<RectNode>(
        Rect {20.f, 20.f, 40.f, 40.f},
        FillStyle::none(),
        StrokeStyle::none(),
        CornerRadius {8.f});
    parent->setClipsContents(true);

    auto child = std::make_unique<RectNode>(Rect {50.f, 0.f, 20.f, 20.f}, FillStyle::solid(Colors::blue));
    auto interaction = std::make_unique<InteractionData>();
    interaction->stableTargetKey_ = ComponentKey {LocalId::fromString("clipped-overflow-child")};
    interaction->onTap = [](MouseButton) {};
    child->setInteraction(std::move(interaction));
    parent->appendChild(std::move(child));
    root->appendChild(std::move(parent));

    SceneGraph graph {std::move(root)};

    CHECK_FALSE(hitTestInteraction(graph, Point {75.f, 25.f}).has_value());
}

TEST_CASE("Scenegraph hit testing respects clipped rounded rect subtrees") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 160.f, 160.f});
    auto container = std::make_unique<RectNode>(Rect {20.f, 20.f, 100.f, 100.f}, FillStyle::none(),
                                                StrokeStyle::none(), CornerRadius {40.f});
    container->setClipsContents(true);
    auto child = std::make_unique<RectNode>(Rect {0.f, 0.f, 100.f, 100.f}, FillStyle::solid(Colors::blue));
    auto interaction = std::make_unique<InteractionData>();
    interaction->stableTargetKey_ = ComponentKey {LocalId::fromString("clipped")};
    child->setInteraction(std::move(interaction));
    container->appendChild(std::move(child));
    root->appendChild(std::move(container));

    SceneGraph graph {std::move(root)};

    CHECK_FALSE(hitTestInteraction(graph, Point {22.f, 22.f}).has_value());
    auto hit = hitTestInteraction(graph, Point {70.f, 70.f});
    REQUIRE(hit.has_value());
    CHECK(hit->interaction->stableTargetKey() == ComponentKey {LocalId::fromString("clipped")});
}

TEST_CASE("SceneRenderer quick reject uses overflowing group visual bounds") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 120.f, 60.f});
    auto group = std::make_unique<SceneNode>(Rect {0.f, 0.f, 20.f, 20.f});
    group->appendChild(std::make_unique<RectNode>(
        Rect {80.f, 0.f, 20.f, 20.f},
        FillStyle::solid(Colors::blue)));
    root->appendChild(std::move(group));

    SceneGraph graph {std::move(root)};
    RecordingRenderer renderer;
    auto sawSubtreeBounds = std::make_shared<bool>(false);
    renderer.quickRejectHandler = [sawSubtreeBounds](Rect rect) {
        if (rect == Rect::sharp(0.f, 0.f, 100.f, 20.f)) {
            *sawSubtreeBounds = true;
        }
        return !*sawSubtreeBounds &&
               rect.x == 0.f && rect.y == 0.f && rect.width == 20.f && rect.height == 20.f;
    };
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);

    REQUIRE(renderer.rectDraws.size() == 1);
    CHECK(renderer.rectDraws[0].translation == Point {80.f, 0.f});
    CHECK(std::any_of(renderer.quickRejects.begin(), renderer.quickRejects.end(), [](Rect rect) {
        return rect == Rect::sharp(0.f, 0.f, 100.f, 20.f);
    }));
}

TEST_CASE("subtree visual bounds are cached and invalidated by subtree mutations") {
    int rootBoundsCalls = 0;
    int childBoundsCalls = 0;
    auto root = std::make_unique<CountingBoundsNode>(
        Rect {0.f, 0.f, 10.f, 10.f},
        Rect::sharp(0.f, 0.f, 10.f, 10.f),
        &rootBoundsCalls);
    auto child = std::make_unique<CountingBoundsNode>(
        Rect {20.f, 0.f, 5.f, 5.f},
        Rect::sharp(0.f, 0.f, 5.f, 5.f),
        &childBoundsCalls);
    CountingBoundsNode* childNode = child.get();
    root->appendChild(std::move(child));

    CHECK(lambda::scenegraph::detail::subtreeLocalVisualBounds(*root) ==
          Rect::sharp(0.f, 0.f, 25.f, 10.f));
    CHECK(rootBoundsCalls == 1);
    CHECK(childBoundsCalls == 1);

    CHECK(lambda::scenegraph::detail::subtreeLocalVisualBounds(*root) ==
          Rect::sharp(0.f, 0.f, 25.f, 10.f));
    CHECK(rootBoundsCalls == 1);
    CHECK(childBoundsCalls == 1);

    childNode->setPosition(Point {30.f, 0.f});
    CHECK(lambda::scenegraph::detail::subtreeLocalVisualBounds(*root) ==
          Rect::sharp(0.f, 0.f, 35.f, 10.f));
    CHECK(rootBoundsCalls == 2);
    CHECK(childBoundsCalls == 2);
}

TEST_CASE("SceneNode setBounds position changes dirty the owning subtree") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 100.f, 100.f});
    auto child = std::make_unique<SceneNode>(Rect {5.f, 6.f, 20.f, 20.f});
    SceneNode* childNode = child.get();
    root->appendChild(std::move(child));
    lambda::scenegraph::detail::SceneNodeAccess::clearSubtreeDirty(*childNode);
    lambda::scenegraph::detail::SceneNodeAccess::clearSubtreeDirty(*root);

    childNode->setBounds(Rect {12.f, 18.f, 20.f, 20.f});

    CHECK(childNode->isSubtreeDirty());
    CHECK(root->isSubtreeDirty());
}

TEST_CASE("Scenegraph hit testing rejects cached visual bounds before walking descendants") {
    int rootBoundsCalls = 0;
    int groupBoundsCalls = 0;
    int leafBoundsCalls = 0;
    auto root = std::make_unique<CountingBoundsNode>(
        Rect {0.f, 0.f, 240.f, 160.f},
        Rect::sharp(0.f, 0.f, 240.f, 160.f),
        &rootBoundsCalls);
    auto group = std::make_unique<CountingBoundsNode>(
        Rect {120.f, 80.f, 40.f, 40.f},
        Rect::sharp(0.f, 0.f, 40.f, 40.f),
        &groupBoundsCalls);
    for (int i = 0; i < 12; ++i) {
        group->appendChild(std::make_unique<CountingBoundsNode>(
            Rect {static_cast<float>(i), static_cast<float>(i), 4.f, 4.f},
            Rect::sharp(0.f, 0.f, 4.f, 4.f),
            &leafBoundsCalls));
    }
    root->appendChild(std::move(group));
    SceneGraph graph {std::move(root)};

    (void)lambda::scenegraph::detail::subtreeLocalVisualBounds(graph.root());
    rootBoundsCalls = 0;
    groupBoundsCalls = 0;
    leafBoundsCalls = 0;

    CHECK_FALSE(hitTestInteraction(graph, Point {12.f, 12.f}).has_value());
    CHECK(rootBoundsCalls == 1);
    CHECK(groupBoundsCalls == 0);
    CHECK(leafBoundsCalls == 0);
}

TEST_CASE("Scenegraph interaction lookup collects focusable keys") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 120.f});

    auto panel = std::make_unique<RectNode>(Rect {10.f, 10.f, 80.f, 40.f}, FillStyle::none());
    auto panelInteraction = std::make_unique<InteractionData>();
    panelInteraction->stableTargetKey_ =
        ComponentKey {LocalId::fromString("panel"), LocalId::fromString("button")};
    panelInteraction->focusable_ = true;
    panel->setInteraction(std::move(panelInteraction));
    root->appendChild(std::move(panel));

    auto label = std::make_unique<TextNode>(Rect {20.f, 60.f, 40.f, 16.f});
    auto labelInteraction = std::make_unique<InteractionData>();
    labelInteraction->stableTargetKey_ = ComponentKey {LocalId::fromString("label")};
    label->setInteraction(std::move(labelInteraction));
    root->appendChild(std::move(label));

    SceneGraph graph {std::move(root)};

    std::vector<ComponentKey> const focusable = collectFocusableKeys(graph);
    REQUIRE(focusable.size() == 1);
    CHECK(focusable[0] == ComponentKey {LocalId::fromString("panel"), LocalId::fromString("button")});
}

TEST_CASE("Scenegraph interaction lookup stops after the first matching key") {
    ComponentKey const targetKey {LocalId::fromString("target")};
    ComponentKey const decoyKey {LocalId::fromString("decoy")};
    int targetProbes = 0;
    int decoyProbes = 0;

    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 120.f});
    auto decoy = std::make_unique<RectNode>(Rect {60.f, 10.f, 40.f, 40.f}, FillStyle::none());
    decoy->setInteraction(std::make_unique<CountingInteraction>(decoyKey, &decoyProbes));
    root->appendChild(std::move(decoy));

    auto target = std::make_unique<RectNode>(Rect {10.f, 10.f, 40.f, 40.f}, FillStyle::none());
    target->setInteraction(std::make_unique<CountingInteraction>(targetKey, &targetProbes));
    root->appendChild(std::move(target));

    SceneGraph graph {std::move(root)};

    auto const [node, interaction] = findInteractionByKey(graph, targetKey);

    REQUIRE(node != nullptr);
    REQUIRE(interaction != nullptr);
    CHECK(interaction->stableTargetKey() == targetKey);
    CHECK(targetProbes == 2);
    CHECK(decoyProbes == 0);
}

TEST_CASE("SceneGraph stores geometry keyed by component key") {
    SceneGraph graph;
    ComponentKey const keyA {LocalId::fromString("a")};
    ComponentKey const keyB {LocalId::fromString("a"), LocalId::fromString("b")};

    graph.beginGeometryBuild();
    graph.recordGeometry(keyA, Rect {10.f, 20.f, 30.f, 40.f});
    graph.recordGeometry(keyB, Rect {12.f, 24.f, 16.f, 8.f});
    graph.finishGeometryBuild();

    REQUIRE(graph.rectForKey(keyA).has_value());
    CHECK(*graph.rectForKey(keyA) == Rect {10.f, 20.f, 30.f, 40.f});
    REQUIRE(graph.rectForLeafKeyPrefix(keyB).has_value());
    CHECK(*graph.rectForLeafKeyPrefix(keyB) == Rect {12.f, 24.f, 16.f, 8.f});
    CHECK(*graph.rectForTapAnchor(keyB) == Rect {12.f, 24.f, 16.f, 8.f});

    graph.beginGeometryBuild();
    graph.recordGeometry(keyA, Rect {14.f, 18.f, 20.f, 10.f});
    graph.finishGeometryBuild();

    REQUIRE(graph.rectForKey(keyB).has_value());
    CHECK(*graph.rectForKey(keyB) == Rect {12.f, 24.f, 16.f, 8.f});
}

TEST_CASE("SceneGraph retains subtree geometry with translated origin") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 120.f, 80.f});
    auto child = std::make_unique<RectNode>(Rect {0.f, 0.f, 20.f, 10.f}, FillStyle::solid(Colors::red));
    SceneNode* rootNode = root.get();
    SceneNode* childNode = child.get();
    root->appendChild(std::move(child));

    SceneGraph graph {std::move(root)};
    ComponentKey const keyA {LocalId::fromString("a")};
    ComponentKey const keyB {LocalId::fromString("a"), LocalId::fromString("b")};
    ComponentKey const missing {LocalId::fromString("missing")};

    graph.beginGeometryBuild();
    graph.recordGeometry(keyA, Rect {10.f, 20.f, 30.f, 40.f});
    graph.recordGeometry(keyB, Rect {12.f, 24.f, 16.f, 8.f});
    graph.recordNode(keyA, rootNode);
    graph.recordNode(keyB, childNode);
    graph.finishGeometryBuild();

    graph.beginGeometryBuild();
    CHECK_FALSE(graph.retainSubtreeGeometry(missing, Point {0.f, 0.f}));
    CHECK(graph.retainSubtreeGeometry(keyA, Point {30.f, 45.f}));
    graph.finishGeometryBuild();

    CHECK(*graph.rectForKey(keyA) == Rect {30.f, 45.f, 30.f, 40.f});
    CHECK(*graph.rectForKey(keyB) == Rect {32.f, 49.f, 16.f, 8.f});
    CHECK(graph.nodeForKey(keyA) == rootNode);
    CHECK(graph.nodeForKey(keyB) == childNode);
}

TEST_CASE("SceneGraph replaceNodeForKey updates node mappings for repeated replacements") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 120.f, 80.f});
    auto container = std::make_unique<SceneNode>(Rect {0.f, 0.f, 120.f, 80.f});
    auto leaf = std::make_unique<RectNode>(Rect {10.f, 12.f, 40.f, 20.f}, FillStyle::solid(Colors::red));
    SceneNode* originalLeaf = leaf.get();
    SceneNode* containerNode = container.get();
    root->appendChild(std::move(container));
    containerNode->appendChild(std::move(leaf));

    SceneGraph graph {std::move(root)};
    ComponentKey const key {LocalId::fromString("ambient")};
    ComponentKey const alias {LocalId::fromString("ambient"), LocalId::fromString("body")};

    graph.beginGeometryBuild();
    graph.recordNode(key, originalLeaf);
    graph.recordNode(alias, originalLeaf);
    graph.finishGeometryBuild();

    auto placeholder = std::make_unique<SceneNode>(Rect {0.f, 0.f, 40.f, 20.f});
    SceneNode* placeholderNode = placeholder.get();
    std::unique_ptr<SceneNode> removed = graph.replaceNodeForKey(key, std::move(placeholder));
    REQUIRE(removed.get() == originalLeaf);
    CHECK(graph.nodeForKey(key) == placeholderNode);
    CHECK(graph.nodeForKey(alias) == placeholderNode);
    REQUIRE(containerNode->children().size() == 1);
    CHECK(containerNode->children().front().get() == placeholderNode);

    auto replacement = std::make_unique<RectNode>(Rect {10.f, 12.f, 40.f, 20.f}, FillStyle::solid(Colors::blue));
    SceneNode* replacementNode = replacement.get();
    std::unique_ptr<SceneNode> removedPlaceholder =
        graph.replaceNodeForKey(key, std::move(replacement));
    REQUIRE(removedPlaceholder.get() == placeholderNode);
    CHECK(graph.nodeForKey(key) == replacementNode);
    CHECK(graph.nodeForKey(alias) == replacementNode);
    REQUIRE(containerNode->children().size() == 1);
    CHECK(containerNode->children().front().get() == replacementNode);
    CHECK(&graph.root() != replacementNode);
}

TEST_CASE("SceneRenderer reuses prepared ops for position-only changes and rebuilds on content changes") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 100.f});
    auto rect = std::make_unique<RectNode>(Rect {10.f, 12.f, 50.f, 30.f}, FillStyle::solid(Colors::red));
    RectNode *rectNode = rect.get();
    root->appendChild(std::move(rect));

    SceneGraph graph {std::move(root)};
    PreparedCountingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 1);
    CHECK(renderer.replayCalls == 1);
    CHECK(renderer.fallbackRectDraws == 0);
    REQUIRE(renderer.replayTranslations.size() == 1);
    CHECK(renderer.replayTranslations.back() == Point {10.f, 12.f});

    rectNode->setPosition(Point {40.f, 24.f});
    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 1);
    CHECK(renderer.replayCalls == 2);
    CHECK(renderer.fallbackRectDraws == 0);
    REQUIRE(renderer.replayTranslations.size() == 2);
    CHECK(renderer.replayTranslations.back() == Point {40.f, 24.f});

    rectNode->setClipsContents(true);
    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 1);
    CHECK(renderer.replayCalls == 3);
    CHECK(renderer.fallbackRectDraws == 0);

    rectNode->setFill(FillStyle::solid(Colors::blue));
    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 2);
    CHECK(renderer.replayCalls == 4);
    CHECK(renderer.fallbackRectDraws == 0);

    rectNode->setSize(Size {64.f, 30.f});
    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 3);
    CHECK(renderer.replayCalls == 5);
    CHECK(renderer.fallbackRectDraws == 0);
}

TEST_CASE("SceneRenderer does not reprepare parent paint for child-only content changes") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 240.f, 160.f});
    auto panel = std::make_unique<RectNode>(Rect {10.f, 12.f, 120.f, 80.f}, FillStyle::solid(Colors::red));
    auto child = std::make_unique<RectNode>(Rect {8.f, 10.f, 32.f, 20.f}, FillStyle::solid(Colors::green));
    RectNode *childNode = child.get();
    panel->appendChild(std::move(child));
    root->appendChild(std::move(panel));

    SceneGraph graph {std::move(root)};
    PreparedCountingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 2);
    CHECK(renderer.replayCalls == 2);
    CHECK(renderer.fallbackRectDraws == 0);

    childNode->setFill(FillStyle::solid(Colors::blue));
    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 3);
    CHECK(renderer.replayCalls == 4);
    CHECK(renderer.fallbackRectDraws == 0);
}

TEST_CASE("SceneGraph invalidates prepared render caches") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 200.f, 100.f});
    auto rect = std::make_unique<RectNode>(
        Rect {10.f, 12.f, 50.f, 30.f}, FillStyle::solid(Colors::red));
    RectNode* rectNode = rect.get();
    root->appendChild(std::move(rect));

    SceneGraph graph {std::move(root)};
    PreparedCountingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 1);
    CHECK(renderer.replayCalls == 1);
    CHECK_FALSE(graph.root().isSubtreeDirty());
    CHECK_FALSE(rectNode->isDirty());

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 1);
    CHECK(renderer.replayCalls == 2);

    graph.invalidateRenderCaches();
    CHECK(graph.root().isSubtreeDirty());
    CHECK(rectNode->isDirty());

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 2);
    CHECK(renderer.replayCalls == 3);
    CHECK_FALSE(graph.root().isSubtreeDirty());
    CHECK_FALSE(rectNode->isDirty());
}

TEST_CASE("RasterCacheNode bypasses prepared ops for descendants") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 240.f, 160.f});
    auto raster = std::make_unique<RasterCacheNode>(Rect {20.f, 24.f, 120.f, 80.f});
    raster->setSubtree(std::make_unique<RectNode>(
        Rect {0.f, 0.f, 80.f, 40.f}, FillStyle::solid(Colors::red)));
    root->appendChild(std::move(raster));

    SceneGraph graph {std::move(root)};
    PreparedCountingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 0);
    CHECK(renderer.replayCalls == 0);
    CHECK(renderer.fallbackRectDraws == 1);
}

TEST_CASE("SceneGraph invalidates raster cache boundaries") {
    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, 240.f, 160.f});
    auto raster = std::make_unique<RasterCacheNode>(Rect {20.f, 24.f, 120.f, 80.f});
    RasterCacheNode* rasterNode = raster.get();
    raster->setSubtree(std::make_unique<RectNode>(
        Rect {0.f, 0.f, 80.f, 40.f}, FillStyle::solid(Colors::red)));
    root->appendChild(std::move(raster));

    SceneGraph graph {std::move(root)};
    PreparedCountingRenderer renderer;
    SceneRenderer sceneRenderer {renderer};

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 0);
    CHECK(renderer.fallbackRectDraws == 1);
    CHECK_FALSE(rasterNode->isDirty());

    rasterNode->setCachedImage(std::make_shared<DummyImage>(Size{240.f, 160.f}),
                               Size{120.f, 80.f}, 2.f);
    REQUIRE(rasterNode->cachedImage());
    CHECK(rasterNode->hasValidCache(Size{120.f, 80.f}, 2.f));

    graph.invalidateRenderCaches();
    CHECK(rasterNode->isDirty());
    CHECK(rasterNode->isSubtreeDirty());
    CHECK_FALSE(rasterNode->cachedImage());
    CHECK_FALSE(rasterNode->hasValidCache(Size{120.f, 80.f}, 2.f));

    sceneRenderer.render(graph);
    CHECK(renderer.prepareCalls == 0);
    CHECK(renderer.fallbackRectDraws == 2);
    CHECK_FALSE(rasterNode->isDirty());
}

} // namespace
