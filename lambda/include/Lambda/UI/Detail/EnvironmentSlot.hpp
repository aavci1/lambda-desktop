#pragma once

#include <Lambda/Reactive/Signal.hpp>

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace lambda::detail {

inline constexpr std::uint16_t kMaxEnvironmentSlots = 256;

class EnvironmentSlot {
public:
  explicit constexpr EnvironmentSlot(std::uint16_t index) noexcept
      : index_(index) {}

  constexpr std::uint16_t index() const noexcept { return index_; }

private:
  std::uint16_t index_ = 0;
};

std::uint16_t allocateEnvironmentSlot(std::type_info const& tag);

enum class EnvironmentEntryKind : std::uint8_t {
  None,
  Value,
  Signal,
};

struct EnvironmentEntryVTable {
  void (*destroy)(void* storage) = nullptr;
  void (*copyTo)(void const* src, void* dst) = nullptr;
  bool (*equals)(void const* a, void const* b) = nullptr;
  std::uint16_t size = 0;
  std::uint16_t align = 0;
};

class EnvironmentEntry {
public:
  EnvironmentEntry() = default;

  EnvironmentEntry(EnvironmentEntry const& other) {
    copyFrom(other);
  }

  EnvironmentEntry(EnvironmentEntry&& other) noexcept {
    moveFrom(std::move(other));
  }

  EnvironmentEntry& operator=(EnvironmentEntry const& other) {
    if (this != &other) {
      reset();
      copyFrom(other);
    }
    return *this;
  }

  EnvironmentEntry& operator=(EnvironmentEntry&& other) noexcept {
    if (this != &other) {
      reset();
      moveFrom(std::move(other));
    }
    return *this;
  }

  ~EnvironmentEntry() {
    reset();
  }

  EnvironmentEntryKind kind() const noexcept { return kind_; }
  bool empty() const noexcept { return kind_ == EnvironmentEntryKind::None; }

  template<typename T>
  T const* asValue() const noexcept {
    if (kind_ != EnvironmentEntryKind::Value || vtable_ != valueVTable<T>()) {
      return nullptr;
    }
    return static_cast<T const*>(objectPtr());
  }

  template<typename T>
  Reactive::Signal<T> const* asSignal() const noexcept {
    if (kind_ != EnvironmentEntryKind::Signal || vtable_ != signalVTable<T>()) {
      return nullptr;
    }
    return static_cast<Reactive::Signal<T> const*>(objectPtr());
  }

  template<typename T>
  void setValue(T value) {
    static_assert(std::copy_constructible<T>,
                  "Environment values must be copy-constructible.");
    static_assert(std::equality_comparable<T>,
                  "Environment values must define operator==.");

    reset();
    EnvironmentEntryVTable const* table = valueVTable<T>();
    void* newHeap = nullptr;
    void* dst = storage_;
    if constexpr (!fitsInlineMoveSafely<T>()) {
      newHeap = allocate(*table);
      dst = newHeap;
    }
    try {
      new (dst) T(std::move(value));
    } catch (...) {
      if (newHeap) {
        deallocate(newHeap, *table);
      }
      throw;
    }
    kind_ = EnvironmentEntryKind::Value;
    vtable_ = table;
    heap_ = newHeap;
  }

  template<typename T>
  void setSignal(Reactive::Signal<T> signal) {
    static_assert(std::copy_constructible<T>,
                  "Environment signal values must be copy-constructible.");
    static_assert(std::equality_comparable<T>,
                  "Environment signal values must define operator==.");

    reset();
    EnvironmentEntryVTable const* table = signalVTable<T>();
    void* newHeap = nullptr;
    void* dst = storage_;
    using SignalT = Reactive::Signal<T>;
    if constexpr (!fitsInlineMoveSafely<SignalT>()) {
      newHeap = allocate(*table);
      dst = newHeap;
    }
    try {
      new (dst) SignalT(std::move(signal));
    } catch (...) {
      if (newHeap) {
        deallocate(newHeap, *table);
      }
      throw;
    }
    kind_ = EnvironmentEntryKind::Signal;
    vtable_ = table;
    heap_ = newHeap;
  }

  bool equals(EnvironmentEntry const& other) const noexcept {
    if (kind_ != other.kind_ || vtable_ != other.vtable_) {
      return false;
    }
    if (kind_ == EnvironmentEntryKind::None) {
      return true;
    }
    return vtable_ && vtable_->equals && vtable_->equals(objectPtr(), other.objectPtr());
  }

private:
  static constexpr std::size_t kInlineBytes = 24;

  template<typename T>
  static EnvironmentEntryVTable const* valueVTable() {
    static EnvironmentEntryVTable const table{
        .destroy = [](void* storage) {
          static_cast<T*>(storage)->~T();
        },
        .copyTo = [](void const* src, void* dst) {
          new (dst) T(*static_cast<T const*>(src));
        },
        .equals = [](void const* a, void const* b) {
          return *static_cast<T const*>(a) == *static_cast<T const*>(b);
        },
        .size = static_cast<std::uint16_t>(sizeof(T)),
        .align = static_cast<std::uint16_t>(alignof(T)),
    };
    return &table;
  }

  template<typename T>
  static EnvironmentEntryVTable const* signalVTable() {
    using SignalT = Reactive::Signal<T>;
    static EnvironmentEntryVTable const table{
        .destroy = [](void* storage) {
          static_cast<SignalT*>(storage)->~SignalT();
        },
        .copyTo = [](void const* src, void* dst) {
          new (dst) SignalT(*static_cast<SignalT const*>(src));
        },
        .equals = [](void const* a, void const* b) {
          SignalT const& lhs = *static_cast<SignalT const*>(a);
          SignalT const& rhs = *static_cast<SignalT const*>(b);
          return lhs == rhs;
        },
        .size = static_cast<std::uint16_t>(sizeof(SignalT)),
        .align = static_cast<std::uint16_t>(alignof(SignalT)),
    };
    return &table;
  }

  template<typename T>
  static constexpr bool fitsInlineMoveSafely() noexcept {
    return sizeof(T) <= kInlineBytes &&
           alignof(T) <= alignof(std::max_align_t) &&
           std::is_nothrow_copy_constructible_v<T>;
  }

  static void* allocate(EnvironmentEntryVTable const& table) {
    return ::operator new(table.size, std::align_val_t{std::max<std::size_t>(table.align, 1)});
  }

  static void deallocate(void* ptr, EnvironmentEntryVTable const& table) noexcept {
    ::operator delete(ptr, std::align_val_t{std::max<std::size_t>(table.align, 1)});
  }

  void const* objectPtr() const noexcept {
    return heap_ ? heap_ : storage_;
  }

  void* objectPtr() noexcept {
    return heap_ ? heap_ : storage_;
  }

  void copyFrom(EnvironmentEntry const& other) {
    if (other.kind_ == EnvironmentEntryKind::None) {
      return;
    }
    assert(other.vtable_);
    EnvironmentEntryVTable const* table = other.vtable_;
    void* newHeap = nullptr;
    void* dst = storage_;
    if (other.heap_) {
      newHeap = allocate(*table);
      dst = newHeap;
    }
    try {
      table->copyTo(other.objectPtr(), dst);
    } catch (...) {
      if (newHeap) {
        deallocate(newHeap, *table);
      }
      throw;
    }
    kind_ = other.kind_;
    vtable_ = table;
    heap_ = newHeap;
  }

  void moveFrom(EnvironmentEntry&& other) noexcept {
    if (other.kind_ == EnvironmentEntryKind::None) {
      return;
    }
    EnvironmentEntryKind const nextKind = other.kind_;
    EnvironmentEntryVTable const* nextVTable = other.vtable_;
    if (other.heap_) {
      heap_ = other.heap_;
      other.heap_ = nullptr;
      kind_ = nextKind;
      vtable_ = nextVTable;
      other.kind_ = EnvironmentEntryKind::None;
      other.vtable_ = nullptr;
      return;
    }
    assert(nextVTable);
    // Invariant: inline-stored entries are nothrow-copy-constructible, filtered by
    // setValue/setSignal. The vtable signature cannot express that, but this copyTo
    // call is effectively noexcept for every inline entry.
    nextVTable->copyTo(other.objectPtr(), storage_);
    kind_ = nextKind;
    vtable_ = nextVTable;
    heap_ = nullptr;
    other.reset();
  }

  void reset() noexcept {
    if (kind_ != EnvironmentEntryKind::None && vtable_) {
      vtable_->destroy(objectPtr());
      if (heap_) {
        deallocate(heap_, *vtable_);
      }
    }
    kind_ = EnvironmentEntryKind::None;
    vtable_ = nullptr;
    heap_ = nullptr;
  }

  EnvironmentEntryKind kind_ = EnvironmentEntryKind::None;
  EnvironmentEntryVTable const* vtable_ = nullptr;
  alignas(std::max_align_t) unsigned char storage_[kInlineBytes]{};
  void* heap_ = nullptr;
};

} // namespace lambda::detail
