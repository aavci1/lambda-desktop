#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <Lambda/UI/Element.hpp>

namespace lambda_shell {

struct LambdaCommandLauncher {
  CommandLauncherProps props;

  lambda::Element body() const;
};

} // namespace lambda_shell
