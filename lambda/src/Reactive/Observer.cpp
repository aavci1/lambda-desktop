#include <Lambda/Reactive/Observer.hpp>

namespace lambdaui {

bool ObserverHandle::isValid() const {
  return id != 0;
}

} // namespace lambdaui
