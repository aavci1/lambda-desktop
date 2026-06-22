#pragma once

/// \file Lambda/Detail/SmallVector.hpp
///
/// Small inline buffer (stack) with heap spillover when capacity exceeds \p N.

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace lambda::detail {

template <typename T, std::size_t N>
class SmallVector {
  static_assert(N > 0, "N must be positive");

public:
  using value_type = T;
  using size_type = std::size_t;
  using iterator = T*;
  using const_iterator = T const*;

  SmallVector() = default;

  SmallVector(SmallVector const& other) requires std::is_copy_constructible_v<T> {
    reserve(other.size_);
    for (std::size_t i = 0; i < other.size_; ++i) {
      push_back(other.data()[i]);
    }
  }

  SmallVector(SmallVector const& other) requires (!std::is_copy_constructible_v<T>) = delete;

  SmallVector& operator=(SmallVector const& other) requires std::is_copy_constructible_v<T> {
    if (this == &other) {
      return *this;
    }
    clear();
    reserve(other.size_);
    for (std::size_t i = 0; i < other.size_; ++i) {
      push_back(other.data()[i]);
    }
    return *this;
  }

  SmallVector& operator=(SmallVector const& other) requires (!std::is_copy_constructible_v<T>) = delete;

  SmallVector(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (other.size_ <= N) {
      for (std::size_t i = 0; i < other.size_; ++i) {
        new (inlinePtr(i)) T(std::move(*other.inlinePtr(i)));
        other.inlinePtr(i)->~T();
      }
      size_ = other.size_;
      other.size_ = 0;
    } else {
      heap_ = std::move(other.heap_);
      size_ = other.size_;
      other.size_ = 0;
    }
  }

  SmallVector& operator=(SmallVector&& other) noexcept(std::is_nothrow_move_assignable_v<T>) {
    if (this == &other) {
      return *this;
    }
    clear();
    if (other.size_ <= N) {
      for (std::size_t i = 0; i < other.size_; ++i) {
        new (inlinePtr(i)) T(std::move(*other.inlinePtr(i)));
        other.inlinePtr(i)->~T();
      }
      size_ = other.size_;
      other.size_ = 0;
    } else {
      heap_ = std::move(other.heap_);
      size_ = other.size_;
      other.size_ = 0;
    }
    return *this;
  }

  ~SmallVector() { clear(); }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  /// Inline storage holds at most \p N elements; the heap is used only after \c emplace_back spills.
  /// Invariant: \c heap_.empty() iff elements live in inline storage (equivalently \c size_ <= \p N).
  [[nodiscard]] T* data() noexcept {
    return heap_.empty() ? inlinePtr(0) : heap_.data();
  }

  [[nodiscard]] T const* data() const noexcept {
    return heap_.empty() ? const_cast<SmallVector*>(this)->inlinePtr(0) : heap_.data();
  }

  [[nodiscard]] T& operator[](std::size_t i) noexcept { return data()[i]; }

  [[nodiscard]] T const& operator[](std::size_t i) const noexcept { return data()[i]; }

  [[nodiscard]] T& back() noexcept { return data()[size_ - 1]; }

  [[nodiscard]] T const& back() const noexcept { return data()[size_ - 1]; }

  [[nodiscard]] iterator begin() noexcept { return data(); }

  [[nodiscard]] const_iterator begin() const noexcept { return data(); }

  [[nodiscard]] iterator end() noexcept { return data() + size_; }

  [[nodiscard]] const_iterator end() const noexcept { return data() + size_; }

  void clear() {
    if (size_ <= N) {
      for (std::size_t i = 0; i < size_; ++i) {
        inlinePtr(i)->~T();
      }
    } else {
      heap_.clear();
    }
    size_ = 0;
  }

  /// Does not move inline elements to the heap; capacity beyond \p N is acquired only via \c push_back /
  /// \c emplace_back spill. When already on the heap, forwards to \c std::vector::reserve.
  void reserve(std::size_t cap) {
    (void)cap;
    if (size_ > N) {
      heap_.reserve(cap);
    }
  }

  void push_back(T const& v) { emplace_back(v); }

  void push_back(T&& v) { emplace_back(std::move(v)); }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    if (size_ < N) {
      new (inlinePtr(size_)) T(std::forward<Args>(args)...);
      ++size_;
      return *inlinePtr(size_ - 1);
    }
    if (size_ == N) {
      std::vector<T> tmp;
      tmp.reserve(std::max<std::size_t>(N + 1, N * 2));
      for (std::size_t i = 0; i < N; ++i) {
        tmp.push_back(std::move(*inlinePtr(i)));
        inlinePtr(i)->~T();
      }
      tmp.emplace_back(std::forward<Args>(args)...);
      heap_ = std::move(tmp);
      ++size_;
      return heap_.back();
    }
    heap_.emplace_back(std::forward<Args>(args)...);
    ++size_;
    return heap_.back();
  }

  void pop_back() {
    if (size_ == 0) {
      return;
    }
    if (size_ <= N) {
      inlinePtr(size_ - 1)->~T();
      --size_;
      return;
    }
    heap_.pop_back();
    --size_;
    if (size_ == N) {
      for (std::size_t i = 0; i < N; ++i) {
        new (inlinePtr(i)) T(std::move(heap_[i]));
      }
      heap_.clear();
      heap_.shrink_to_fit();
    }
  }

private:
  T* inlinePtr(std::size_t i) noexcept {
    return reinterpret_cast<T*>(inlineStorage_.data() + i * sizeof(T));
  }

  alignas(T) std::array<std::byte, sizeof(T) * N> inlineStorage_{};
  std::vector<T> heap_{};
  std::size_t size_ = 0;
};

} // namespace lambda::detail
