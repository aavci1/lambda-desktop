#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace lambdaui::Reactive {

namespace detail {

template <typename>
struct IsStdFunction : std::false_type {};

template <typename R, typename... Args>
struct IsStdFunction<std::function<R(Args...)>> : std::true_type {};

} // namespace detail

template <typename Signature, std::size_t InlineSize = 32>
class SmallFn;

/// Wider inline budget for framework-generated UI binding closures that capture retained-node plumbing.
using BindingFn = SmallFn<void(), 192>;

template <typename R, typename... Args, std::size_t InlineSize>
class SmallFn<R(Args...), InlineSize> {
public:
  static constexpr std::size_t inlineCapacity = InlineSize;

  SmallFn() = default;
  SmallFn(std::nullptr_t) {}

  SmallFn(SmallFn const& other) {
    copyFrom(other);
  }

  SmallFn(SmallFn&& other) noexcept {
    moveFrom(std::move(other));
  }

  template <typename Fn>
    requires(!std::is_same_v<std::decay_t<Fn>, SmallFn> &&
             std::is_invocable_r_v<R, std::decay_t<Fn>&, Args...>)
  SmallFn(Fn&& fn) {
    using FnType = std::decay_t<Fn>;
    if constexpr (detail::IsStdFunction<FnType>::value) {
      if (!fn) {
        return;
      }
    }
    emplace<FnType>(std::forward<Fn>(fn));
  }

  ~SmallFn() {
    reset();
  }

  SmallFn& operator=(SmallFn const& other) {
    if (this != &other) {
      reset();
      copyFrom(other);
    }
    return *this;
  }

  SmallFn& operator=(SmallFn&& other) noexcept {
    if (this != &other) {
      reset();
      moveFrom(std::move(other));
    }
    return *this;
  }

  explicit operator bool() const {
    return table_ != nullptr;
  }

  R operator()(Args... args) const {
    assert(table_ && "SmallFn called without a target");
    return table_->call(ptr(), std::forward<Args>(args)...);
  }

  void reset() {
    if (!table_) {
      return;
    }
    table_->destroy(ptr());
    if (heap_) {
      ::operator delete(storage_.heap, std::align_val_t{table_->align});
    }
    table_ = nullptr;
    heap_ = false;
    storage_.heap = nullptr;
  }

  bool usesHeapStorage() const {
    return heap_;
  }

private:
  struct VTable {
    R (*call)(void*, Args&&...);
    void (*destroy)(void*);
    void (*copy)(void const*, void*);
    void (*move)(void*, void*) noexcept;
    std::size_t size;
    std::size_t align;
  };

  template <typename Fn>
  static constexpr bool fitsInline =
      sizeof(Fn) <= InlineSize &&
      alignof(Fn) <= alignof(std::max_align_t) &&
      std::is_nothrow_move_constructible_v<Fn>;

  template <typename Fn>
  static VTable const* tableFor() {
    static_assert(std::is_copy_constructible_v<Fn>,
      "SmallFn targets must be copy constructible");

    static VTable const table = {
      [](void* object, Args&&... args) -> R {
        return (*static_cast<Fn*>(object))(std::forward<Args>(args)...);
      },
      [](void* object) {
        static_cast<Fn*>(object)->~Fn();
      },
      [](void const* source, void* target) {
        new (target) Fn(*static_cast<Fn const*>(source));
      },
      [](void* source, void* target) noexcept {
        new (target) Fn(std::move(*static_cast<Fn*>(source)));
        static_cast<Fn*>(source)->~Fn();
      },
      sizeof(Fn),
      alignof(Fn),
    };
    return &table;
  }

  template <typename Fn, typename Arg>
  void emplace(Arg&& arg) {
    table_ = tableFor<Fn>();
    if constexpr (fitsInline<Fn>) {
      heap_ = false;
      new (storage_.inlineBytes) Fn(std::forward<Arg>(arg));
    } else {
      heap_ = true;
      storage_.heap = ::operator new(sizeof(Fn), std::align_val_t{alignof(Fn)});
      try {
        new (storage_.heap) Fn(std::forward<Arg>(arg));
      } catch (...) {
        ::operator delete(storage_.heap, std::align_val_t{alignof(Fn)});
        heap_ = false;
        table_ = nullptr;
        storage_.heap = nullptr;
        throw;
      }
    }
  }

  void copyFrom(SmallFn const& other) {
    if (!other.table_) {
      return;
    }
    table_ = other.table_;
    heap_ = other.heap_;
    if (heap_) {
      storage_.heap = ::operator new(table_->size, std::align_val_t{table_->align});
      other.table_->copy(other.ptr(), storage_.heap);
    } else {
      other.table_->copy(other.ptr(), storage_.inlineBytes);
    }
  }

  void moveFrom(SmallFn&& other) noexcept {
    if (!other.table_) {
      return;
    }
    table_ = other.table_;
    heap_ = other.heap_;
    if (heap_) {
      storage_.heap = other.storage_.heap;
      other.storage_.heap = nullptr;
      other.heap_ = false;
      other.table_ = nullptr;
    } else {
      other.table_->move(other.storage_.inlineBytes, storage_.inlineBytes);
      other.table_ = nullptr;
    }
  }

  void* ptr() const {
    return heap_ ? storage_.heap : const_cast<unsigned char*>(storage_.inlineBytes);
  }

  union Storage {
    alignas(std::max_align_t) unsigned char inlineBytes[InlineSize];
    void* heap;

    Storage() : heap(nullptr) {}
  } storage_;

  VTable const* table_ = nullptr;
  bool heap_ = false;
};

} // namespace lambdaui::Reactive
