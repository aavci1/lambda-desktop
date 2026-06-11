#include <Lambda/SceneGraph/Renderer.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace lambda::scenegraph {
namespace {

void hashCombine(std::uint64_t& hash, void const* data, std::size_t size) {
    auto const* bytes = static_cast<std::uint8_t const*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

template <typename T>
void hashValue(std::uint64_t& hash, T const& value) {
    hashCombine(hash, &value, sizeof(value));
}

} // namespace

TextNode::TextNode(Rect bounds, std::shared_ptr<TextLayout const> layout)
    : SceneNode(SceneNodeKind::Text, bounds), layout_(std::move(layout)) {}

TextNode::~TextNode() = default;

std::shared_ptr<TextLayout const> const &TextNode::layout() const noexcept {
    return layout_;
}

void TextNode::setLayout(std::shared_ptr<TextLayout const> layout) {
    if (layout_ == layout) {
        return;
    }
    layout_ = std::move(layout);
    markDirty();
}

void TextNode::render(Renderer &renderer) const {
    if (!layout_) {
        return;
    }
    renderer.drawTextLayout(*layout_);
}

bool TextNode::canPrepareRenderOps() const noexcept {
    return layout_ != nullptr;
}

std::uint64_t TextNode::preparedRenderOpsKey(float dpiScale) const noexcept {
    if (!layout_) {
        return 0;
    }
    std::uint64_t hash = 14695981039346656037ull;
    std::uintptr_t const layoutAddress = reinterpret_cast<std::uintptr_t>(layout_.get());
    hashValue(hash, layoutAddress);
    hashValue(hash, dpiScale);
    return hash == 0 ? 1 : hash;
}

} // namespace lambda::scenegraph
