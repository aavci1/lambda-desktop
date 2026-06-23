#include <Lambda/Core/Identity.hpp>
#include <Lambda/Detail/SmallVector.hpp>

#include "Debug/PerfCounters.hpp"

#include <cassert>
#include <cstdint>
#include <thread>

namespace lambdaui {

namespace {

using ComponentKeyHandle = std::uint32_t;

constexpr ComponentKeyHandle kRootHandle = 0;
constexpr std::size_t kInitialInternReserve = 16'384;
constexpr std::uint64_t kHandleHashMultiplier = 0x9e3779b97f4a7c15ull;

struct InternedKeyNode {
  ComponentKeyHandle parent = kRootHandle;
  LocalId tail{};
  std::uint32_t depth = 0;
};

struct InternedKeyChild {
  LocalId tail{};
  ComponentKeyHandle handle = kRootHandle;
};

struct InternedChildBucket {
  detail::SmallVector<ComponentKeyHandle, 4> positionalChildren{};
  detail::SmallVector<InternedKeyChild, 4> keyedChildren{};
};

std::size_t mixHandle(ComponentKeyHandle handle) noexcept {
  return static_cast<std::size_t>(static_cast<std::uint64_t>(handle) * kHandleHashMultiplier);
}

bool isIndexedPositional(LocalId tail) noexcept {
  return tail.kind == LocalId::Kind::Positional && tail.value != 0;
}

class ComponentKeyTable {
public:
  ComponentKeyTable() {
    nodes_.reserve(kInitialInternReserve);
    children_.reserve(kInitialInternReserve);
    nodes_.push_back(InternedKeyNode{});
    children_.emplace_back();
  }

  ComponentKeyHandle intern(ComponentKeyHandle parent, LocalId tail) {
    assertOwnerThread();
    assert(parent < nodes_.size());

    InternedChildBucket const& children = children_[parent];
    if (isIndexedPositional(tail)) {
      std::size_t const childIndex = static_cast<std::size_t>(tail.value - 1ull);
      if (childIndex < children.positionalChildren.size()) {
        ComponentKeyHandle const existing = children.positionalChildren[childIndex];
        if (existing != kRootHandle) {
          return existing;
        }
      }
    } else {
      for (InternedKeyChild const& child : children.keyedChildren) {
        if (child.tail == tail) {
          return child.handle;
        }
      }
    }

    ComponentKeyHandle const handle = static_cast<ComponentKeyHandle>(nodes_.size());
    nodes_.push_back(InternedKeyNode{
        .parent = parent,
        .tail = tail,
        .depth = static_cast<std::uint32_t>(nodes_[parent].depth + 1U),
    });
    children_.emplace_back();

    InternedChildBucket& parentChildren = children_[parent];
    if (isIndexedPositional(tail)) {
      std::size_t const childIndex = static_cast<std::size_t>(tail.value - 1ull);
      while (parentChildren.positionalChildren.size() <= childIndex) {
        parentChildren.positionalChildren.emplace_back(kRootHandle);
      }
      parentChildren.positionalChildren[childIndex] = handle;
    } else {
      parentChildren.keyedChildren.emplace_back(InternedKeyChild{
          .tail = tail,
          .handle = handle,
      });
    }
    return handle;
  }

  ComponentKeyHandle intern(LocalId const* values, std::size_t count) {
    assertOwnerThread();
    ComponentKeyHandle handle = kRootHandle;
    for (std::size_t index = 0; index < count; ++index) {
      handle = intern(handle, values[index]);
    }
    return handle;
  }

  ComponentKeyHandle parent(ComponentKeyHandle handle) const noexcept {
    assertOwnerThread();
    return nodes_[handle].parent;
  }

  ComponentKeyHandle ancestorAtDepth(ComponentKeyHandle handle, std::uint32_t depth) const noexcept {
    assertOwnerThread();
    while (nodes_[handle].depth > depth) {
      handle = nodes_[handle].parent;
    }
    return handle;
  }

  bool hasPrefix(ComponentKeyHandle key, std::uint32_t keyDepth, ComponentKeyHandle prefix,
                 std::uint32_t prefixDepth) const noexcept {
    assertOwnerThread();
    if (prefixDepth > keyDepth) {
      return false;
    }
    while (nodes_[key].depth > prefixDepth) {
      key = nodes_[key].parent;
    }
    return key == prefix;
  }

  bool sharesPrefix(ComponentKeyHandle lhs, ComponentKeyHandle rhs) const noexcept {
    assertOwnerThread();
    if (lhs == kRootHandle || rhs == kRootHandle) {
      return false;
    }
    while (nodes_[lhs].depth > nodes_[rhs].depth) {
      lhs = nodes_[lhs].parent;
    }
    while (nodes_[rhs].depth > nodes_[lhs].depth) {
      rhs = nodes_[rhs].parent;
    }
    while (lhs != rhs) {
      if (lhs == kRootHandle || rhs == kRootHandle) {
        return false;
      }
      lhs = nodes_[lhs].parent;
      rhs = nodes_[rhs].parent;
    }
    return lhs != kRootHandle;
  }

  LocalId tail(ComponentKeyHandle handle) const noexcept {
    assertOwnerThread();
    return nodes_[handle].tail;
  }

  void appendPrefix(ComponentKeyHandle handle, std::uint32_t depth, std::vector<LocalId>& out) const {
    assertOwnerThread();
    std::size_t const offset = out.size();
    out.resize(offset + depth);
    for (std::uint32_t index = depth; index > 0; --index) {
      InternedKeyNode const& node = nodes_[handle];
      out[offset + index - 1U] = node.tail;
      handle = node.parent;
    }
  }

private:
  void assertOwnerThread() const noexcept {
#ifndef NDEBUG
    std::thread::id const current = std::this_thread::get_id();
    if (!ownerThreadBound_) {
      ownerThread_ = current;
      ownerThreadBound_ = true;
      return;
    }
    assert(ownerThread_ == current && "ComponentKeyTable is expected to stay on one thread");
#endif
  }

  std::vector<InternedKeyNode> nodes_{};
  std::vector<InternedChildBucket> children_{};
#ifndef NDEBUG
  mutable std::thread::id ownerThread_{};
  mutable bool ownerThreadBound_ = false;
#endif
};

ComponentKeyTable& componentKeyTable() {
  static ComponentKeyTable table{};
  return table;
}

} // namespace

ComponentKey::ComponentKey(std::initializer_list<value_type> init) {
  debug::perf::recordComponentKeyCopy(init.size());
  assignFromValues(init.begin(), init.size());
}

ComponentKey::ComponentKey(std::vector<value_type> const& values) {
  debug::perf::recordComponentKeyCopy(values.size());
  assignFromValues(values.data(), values.size());
}

ComponentKey::ComponentKey(std::vector<value_type> const& prefix, value_type tail) {
  debug::perf::recordComponentKeyAppend(prefix.size() + 1U);
  handle_ = componentKeyTable().intern(prefix.data(), prefix.size());
  handle_ = componentKeyTable().intern(handle_, tail);
  size_ = static_cast<std::uint32_t>(prefix.size() + 1U);
}

ComponentKey::ComponentKey(ComponentKey const& other) {
  debug::perf::recordComponentKeyCopy(other.size());
  handle_ = other.handle_;
  size_ = other.size_;
}

ComponentKey::ComponentKey(ComponentKey const& prefix, value_type tail) {
  debug::perf::recordComponentKeyAppend(prefix.size() + 1U);
  handle_ = componentKeyTable().intern(prefix.handle_, tail);
  size_ = prefix.size_ + 1U;
}

ComponentKey::ComponentKey(ComponentKey&& other) noexcept
    : handle_(other.handle_)
    , size_(other.size_) {
  other.clear();
}

ComponentKey& ComponentKey::operator=(ComponentKey const& other) {
  if (this != &other) {
    debug::perf::recordComponentKeyCopy(other.size());
    handle_ = other.handle_;
    size_ = other.size_;
  }
  return *this;
}

ComponentKey& ComponentKey::operator=(ComponentKey&& other) noexcept {
  if (this != &other) {
    handle_ = other.handle_;
    size_ = other.size_;
    other.clear();
  }
  return *this;
}

ComponentKey::~ComponentKey() = default;

ComponentKey ComponentKey::fromScope(void const* scope) {
  if (!scope) {
    return {};
  }

  std::uint64_t bits = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(scope));
  bits ^= bits >> 33U;
  bits *= 0xff51afd7ed558ccdull;
  bits ^= bits >> 33U;
  bits *= 0xc4ceb9fe1a85ec53ull;
  bits ^= bits >> 33U;
  if (bits == 0) {
    bits = 1;
  }
  LocalId scopeId;
  scopeId.kind = LocalId::Kind::Keyed;
  scopeId.value = bits;

  return ComponentKey{
      LocalId::fromString("__lambda_scope__"),
      scopeId,
  };
}

void ComponentKey::clear() noexcept {
  handle_ = kRootHandle;
  size_ = 0;
}

void ComponentKey::push_back(value_type value) {
  debug::perf::recordComponentKeyAppend(size_ + 1U);
  handle_ = componentKeyTable().intern(handle_, value);
  ++size_;
}

void ComponentKey::pop_back() noexcept {
  if (size_ == 0) {
    return;
  }
  handle_ = componentKeyTable().parent(handle_);
  --size_;
}

ComponentKey ComponentKey::prefix(std::size_t length) const {
  if (length >= size_) {
    return *this;
  }
  if (length == 0) {
    return {};
  }
  return fromHandle(componentKeyTable().ancestorAtDepth(handle_, static_cast<std::uint32_t>(length)),
                    static_cast<std::uint32_t>(length));
}

bool ComponentKey::hasPrefix(ComponentKey const& prefix) const noexcept {
  if (prefix.empty()) {
    return true;
  }
  if (size_ < prefix.size_) {
    return false;
  }
  return componentKeyTable().hasPrefix(handle_, size_, prefix.handle_, prefix.size_);
}

bool ComponentKey::sharesPrefix(ComponentKey const& other) const noexcept {
  if (empty() || other.empty()) {
    return false;
  }
  return componentKeyTable().sharesPrefix(handle_, other.handle_);
}

ComponentKey::value_type ComponentKey::tail() const noexcept {
  if (empty()) {
    return {};
  }
  return componentKeyTable().tail(handle_);
}

void ComponentKey::appendPrefixTo(std::vector<value_type>& out, std::size_t length) const {
  assert(length <= size_);
  componentKeyTable().appendPrefix(handle_, static_cast<std::uint32_t>(length), out);
}

std::vector<ComponentKey::value_type> ComponentKey::materialize() const {
  std::vector<value_type> values{};
  values.reserve(size_);
  appendPrefixTo(values, size_);
  return values;
}

bool operator==(ComponentKey const& lhs, ComponentKey const& rhs) noexcept {
  std::size_t const comparedIds =
      lhs.size_ == rhs.size_ ? lhs.size_ : std::min(lhs.size_, rhs.size_);
  debug::perf::recordComponentKeyEquality(comparedIds);
  return lhs.size_ == rhs.size_ && lhs.handle_ == rhs.handle_;
}

void ComponentKey::assignFromValues(value_type const* values, std::size_t count) {
  size_ = static_cast<std::uint32_t>(count);
  handle_ = componentKeyTable().intern(values, count);
}

ComponentKey ComponentKey::fromHandle(std::uint32_t handle, std::uint32_t size) noexcept {
  ComponentKey key{};
  key.handle_ = handle;
  key.size_ = size;
  return key;
}

std::size_t ComponentKeyHash::operator()(ComponentKey const& k) const noexcept {
  debug::perf::recordComponentKeyHash(k.size());
  return mixHandle(k.handle_);
}

bool keySharesPrefix(ComponentKey const& a, ComponentKey const& b) noexcept {
  if (a.empty() || b.empty()) {
    return false;
  }
  return a.sharesPrefix(b);
}

} // namespace lambdaui
