#pragma once

#include <Lambda/Graphics/TextSystem.hpp>

#include <functional>
#include <memory>
#include <string>

namespace lambda {

class FreeTypeTextSystem final : public TextSystem {
public:
  explicit FreeTypeTextSystem(std::function<std::string()> appNameProvider = {});
  ~FreeTypeTextSystem() override;

  std::shared_ptr<TextLayout const> layout(AttributedString const& text, float maxWidth = 0.f,
                                           TextLayoutOptions const& options = {}) override;
  std::shared_ptr<TextLayout const> layout(std::string_view utf8, Font const& font, Color const& color,
                                           float maxWidth = 0.f,
                                           TextLayoutOptions const& options = {}) override;
  Size measure(AttributedString const& text, float maxWidth = 0.f,
               TextLayoutOptions const& options = {}) override;
  Size measure(std::string_view utf8, Font const& font, Color const& color, float maxWidth = 0.f,
               TextLayoutOptions const& options = {}) override;
  std::uint32_t resolveFontId(std::string_view fontFamily, float weight, bool italic) override;
  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t fontId, std::uint32_t glyphId, float size,
                                           std::uint32_t& outWidth, std::uint32_t& outHeight,
                                           Point& outBearing) override;

protected:
  std::shared_ptr<TextLayout const> layoutBoxedImpl(AttributedString const& text, Rect const& box,
                                                    TextLayoutOptions const& options = {}) override;

private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

} // namespace lambda
