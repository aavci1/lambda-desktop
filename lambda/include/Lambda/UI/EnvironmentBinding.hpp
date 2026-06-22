#pragma once

#include <Lambda/UI/Environment.hpp>
#include <Lambda/UI/Detail/EnvironmentSlot.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace lambda {

class EnvironmentBinding {
public:
  EnvironmentBinding() = default;
  EnvironmentBinding(EnvironmentBinding const&) = default;
  EnvironmentBinding(EnvironmentBinding&&) noexcept = default;
  EnvironmentBinding& operator=(EnvironmentBinding const&) = default;
  EnvironmentBinding& operator=(EnvironmentBinding&&) noexcept = default;

  template<typename Key>
  EnvironmentBinding withValue(typename EnvironmentKey<Key>::Value value) const {
    using Value = typename EnvironmentKey<Key>::Value;
    std::uint16_t const index = EnvironmentKey<Key>::slot().index();
    if (entries_ && index < entries_->size()) {
      detail::EnvironmentEntry const& existing = (*entries_)[index];
      if (Value const* current = existing.asValue<Value>(); current && *current == value) {
        return *this;
      }
    }
    detail::EnvironmentEntry nextEntry;
    nextEntry.setValue<Value>(std::move(value));
    auto entries = copyEntries(index);
    (*entries)[index] = std::move(nextEntry);
    return EnvironmentBinding{std::move(entries)};
  }

  template<typename Key>
  EnvironmentBinding withSignal(Reactive::Signal<typename EnvironmentKey<Key>::Value> signal) const {
    using Value = typename EnvironmentKey<Key>::Value;
    std::uint16_t const index = EnvironmentKey<Key>::slot().index();
    if (entries_ && index < entries_->size()) {
      detail::EnvironmentEntry const& existing = (*entries_)[index];
      if (auto const* current = existing.asSignal<Value>(); current && *current == signal) {
        return *this;
      }
    }
    detail::EnvironmentEntry nextEntry;
    nextEntry.setSignal<Value>(std::move(signal));
    auto entries = copyEntries(index);
    (*entries)[index] = std::move(nextEntry);
    return EnvironmentBinding{std::move(entries)};
  }

  template<typename Key>
  typename EnvironmentKey<Key>::Value value() const {
    using Value = typename EnvironmentKey<Key>::Value;
    if (detail::EnvironmentEntry const* entry = entryFor<Key>()) {
      if (Value const* value = entry->asValue<Value>()) {
        return *value;
      }
      if (auto const* signal = entry->asSignal<Value>()) {
        return signal->get();
      }
    }
    return EnvironmentKey<Key>::defaultValue();
  }

  template<typename Key>
  std::optional<Reactive::Signal<typename EnvironmentKey<Key>::Value>> signal() const {
    using Value = typename EnvironmentKey<Key>::Value;
    if (detail::EnvironmentEntry const* entry = entryFor<Key>()) {
      if (auto const* signal = entry->asSignal<Value>()) {
        return *signal;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] bool empty() const noexcept {
    return !entries_ || entries_->empty();
  }

  [[nodiscard]] void const* internalEntriesPointer() const noexcept {
    return entries_.get();
  }

private:
  explicit EnvironmentBinding(std::shared_ptr<std::vector<detail::EnvironmentEntry> const> entries)
      : entries_(std::move(entries)) {}

  template<typename Key>
  detail::EnvironmentEntry const* entryFor() const {
    if (!entries_) {
      return nullptr;
    }
    std::uint16_t const index = EnvironmentKey<Key>::slot().index();
    if (index >= entries_->size()) {
      return nullptr;
    }
    detail::EnvironmentEntry const& entry = (*entries_)[index];
    return entry.empty() ? nullptr : &entry;
  }

  std::shared_ptr<std::vector<detail::EnvironmentEntry>> copyEntries(std::uint16_t requiredIndex) const {
    auto entries = std::make_shared<std::vector<detail::EnvironmentEntry>>();
    if (entries_) {
      *entries = *entries_;
    }
    if (entries->size() <= requiredIndex) {
      entries->resize(static_cast<std::size_t>(requiredIndex) + 1);
    }
    return entries;
  }

  std::shared_ptr<std::vector<detail::EnvironmentEntry> const> entries_;
};

} // namespace lambda
