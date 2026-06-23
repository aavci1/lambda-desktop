#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <Lambda/UI/Element.hpp>

namespace lambda_shell {

struct LambdaDock {
  DockProps props;

  lambdaui::Element body() const;
};

struct LambdaDockMenu {
  DockMenuProps props;

  lambdaui::Element body() const;
};

struct LambdaSessionMenu {
  SessionMenuProps props;

  lambdaui::Element body() const;
};

} // namespace lambda_shell
