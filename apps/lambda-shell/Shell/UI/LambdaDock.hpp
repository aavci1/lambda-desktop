#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <Lambda/UI/Element.hpp>

namespace lambda_shell {

struct LambdaDock {
  DockProps props;

  lambda::Element body() const;
};

struct LambdaDockMenu {
  DockMenuProps props;

  lambda::Element body() const;
};

struct LambdaSessionMenu {
  SessionMenuProps props;

  lambda::Element body() const;
};

} // namespace lambda_shell
