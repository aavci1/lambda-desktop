#include <Lambda/Reactive/Observer.hpp>

namespace lambda {

bool ObserverHandle::isValid() const {
  return id != 0;
}

} // namespace lambda
