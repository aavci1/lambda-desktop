#include <Lambda/UI/Environment.hpp>

#include <cassert>
#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace lambda {

namespace detail {

namespace {

struct SlotRegistry {
  std::mutex mutex;
  std::uint16_t nextIndex = 0;
  std::unordered_map<std::type_index, std::uint16_t> assigned;
};

SlotRegistry& slotRegistry() {
  static SlotRegistry registry;
  return registry;
}

} // namespace

std::uint16_t allocateEnvironmentSlot(std::type_info const& tag) {
  SlotRegistry& registry = slotRegistry();
  std::lock_guard lock{registry.mutex};
  auto [it, inserted] = registry.assigned.try_emplace(std::type_index{tag}, registry.nextIndex);
  if (inserted) {
    assert(registry.nextIndex < kMaxEnvironmentSlots && "too many environment keys");
    ++registry.nextIndex;
  }
  return it->second;
}

} // namespace detail

} // namespace lambda
