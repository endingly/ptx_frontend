#pragma once
#include <cstdint>
#include <type_traits>
#include <utility>

namespace ptx_frontend {

struct SourcePos {
  int32_t line;
  int32_t column;

  SourcePos() : line(0), column(0) {}
  SourcePos(int32_t l, int32_t c) : line(l), column(c) {}

  bool operator==(const SourcePos& other) const = default;
};

struct SourceRange {
  SourcePos start;
  SourcePos end;

  SourceRange() : start(), end() {}
  SourceRange(SourcePos s, SourcePos e) : start(s), end(e) {}

  bool operator==(const SourceRange& other) const = default;
};

template <typename T>
  requires std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>
struct WithLoc {
  T value;
  SourceRange loc;

  WithLoc() = default;
  WithLoc(const T& v, SourceRange l) : value(v), loc(l) {}
  WithLoc(T&& v,
          SourceRange l) noexcept(std::is_nothrow_move_constructible_v<T>)
      : value(std::move(v)), loc(l) {}
  WithLoc(const T& v) : value(v), loc({{0, 0}, {0, 0}}) {}
  WithLoc(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>)
      : value(std::move(v)), loc({{0, 0}, {0, 0}}) {}
  // copy
  WithLoc(const WithLoc&) = default;
  WithLoc& operator=(const WithLoc&) = default;

  // move (only if T supports move)
  WithLoc(WithLoc&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
    requires std::is_move_constructible_v<T>
      : value(std::move(other.value)), loc(other.loc) {}

  // access / pointer semantics
  T& operator*() & noexcept { return value; }
  const T& operator*() const& noexcept { return value; }
  T* operator->() noexcept { return std::addressof(value); }
  const T* operator->() const noexcept { return std::addressof(value); }

  // implicit/explicit conversions: lvalue reference, copy/move as needed
  operator T&() & noexcept { return value; }
  operator const T&() const& noexcept { return value; }
  operator T() && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return std::move(value);
  }

  // assignment from T
  WithLoc& operator=(const T& v)
    requires std::is_copy_assignable_v<T>
  {
    value = v;
    return *this;
  }
  WithLoc& operator=(T&& v)
    requires std::is_move_assignable_v<T>
  {
    value = std::move(v);
    return *this;
  }

  WithLoc& operator=(WithLoc&& other) noexcept(
      std::is_nothrow_move_assignable_v<T>)
    requires std::is_move_assignable_v<T>
  {
    value = std::move(other.value);
    loc = other.loc;
    return *this;
  }

  WithLoc& operator=(WithLoc& other)
    requires std::is_copy_assignable_v<T>
  {
    value = other.value;
    loc = other.loc;
    return *this;
  }

  bool operator==(const WithLoc& other) const
    requires std::equality_comparable<T>
  {
    return value == other.value && loc == other.loc;
  }

  bool operator==(const T& other) const
    requires std::equality_comparable<T>
  {
    return value == other;
  }
};

};  // namespace ptx_frontend