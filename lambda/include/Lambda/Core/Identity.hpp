#pragma once

/// \file Lambda/Core/Identity.hpp
///
/// Stable local and component identity primitives.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace lambda {

struct LocalId {
  enum class Kind : std::uint8_t {
    Positional,
    Keyed,
  };

  constexpr LocalId() = default;

  constexpr LocalId(std::size_t index)
      : kind(Kind::Positional)
      , value(static_cast<std::uint64_t>(index) + 1ull) {}

  static constexpr LocalId fromIndex(std::size_t index) { return LocalId{index}; }

  static LocalId fromString(std::string_view key) {
    LocalId id;
    id.kind = Kind::Keyed;
    id.value = hashKeyString(key);
    return id;
  }

  constexpr bool operator==(LocalId const&) const = default;

  Kind kind = Kind::Positional;
  std::uint64_t value = 0;

private:
  static std::uint64_t hashKeyString(std::string_view key) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char ch : key) {
      h ^= static_cast<std::uint64_t>(ch);
      h *= 1099511628211ull;
    }
    return h == 0 ? 1ull : h;
  }
};

struct LocalIdHash {
  std::size_t operator()(LocalId const& id) const noexcept {
    std::size_t seed = static_cast<std::size_t>(id.value);
    seed ^= static_cast<std::size_t>(id.kind) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
  }
};

class ComponentKey {
public:
  using value_type = LocalId;

  ComponentKey() = default;
  ComponentKey(std::initializer_list<value_type> init);
  ComponentKey(std::vector<value_type> const& values);
  ComponentKey(std::vector<value_type> const& prefix, value_type tail);

  template<typename It>
  ComponentKey(It first, It last) {
    assign(first, last);
  }

  ComponentKey(ComponentKey const& other);
  ComponentKey(ComponentKey const& prefix, value_type tail);
  ComponentKey(ComponentKey&& other) noexcept;
  ComponentKey& operator=(ComponentKey const& other);
  ComponentKey& operator=(ComponentKey&& other) noexcept;
  ~ComponentKey();

  [[nodiscard]] static ComponentKey fromScope(void const* scope);

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  void clear() noexcept;
  void push_back(value_type value);
  void pop_back() noexcept;
  void reserve(std::size_t) noexcept {}

  [[nodiscard]] ComponentKey prefix(std::size_t length) const;
  [[nodiscard]] bool hasPrefix(ComponentKey const& prefix) const noexcept;
  [[nodiscard]] bool sharesPrefix(ComponentKey const& other) const noexcept;
  [[nodiscard]] value_type tail() const noexcept;
  void appendPrefixTo(std::vector<value_type>& out, std::size_t length) const;
  [[nodiscard]] std::vector<value_type> materialize() const;

  friend bool operator==(ComponentKey const& lhs, ComponentKey const& rhs) noexcept;
  friend struct ComponentKeyHash;

  friend bool operator!=(ComponentKey const& lhs, ComponentKey const& rhs) noexcept {
    return !(lhs == rhs);
  }

private:
  template<typename It>
  void assign(It first, It last) {
    std::vector<value_type> values{};
    for (It it = first; it != last; ++it) {
      values.push_back(*it);
    }
    assignFromValues(values.data(), values.size());
  }

  void assignFromValues(value_type const* values, std::size_t count);
  [[nodiscard]] static ComponentKey fromHandle(std::uint32_t handle, std::uint32_t size) noexcept;

  std::uint32_t handle_ = 0;
  std::uint32_t size_ = 0;
};

struct ComponentKeyHash {
  std::size_t operator()(ComponentKey const& k) const noexcept;
};

bool keySharesPrefix(ComponentKey const& a, ComponentKey const& b) noexcept;

} // namespace lambda
