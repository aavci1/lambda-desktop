#pragma once

#include <Lambda/UI/Clipboard.hpp>

namespace lambdaui {

class MacClipboard final : public Clipboard {
public:
  std::optional<std::string> readText() const override;
  void writeText(std::string text) override;
  bool hasText() const override;
};

} // namespace lambdaui
