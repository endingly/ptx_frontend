#pragma once
#include <concepts>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

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
  T value{};
  SourceRange loc{};

  WithLoc() = default;

  WithLoc(const T& v, SourceRange l) : value(v), loc(l) {}

  WithLoc(T&& v,
          SourceRange l) noexcept(std::is_nothrow_move_constructible_v<T>)
      : value(std::move(v)), loc(l) {}

  WithLoc(const T& v) : value(v), loc{} {}

  WithLoc(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>)
      : value(std::move(v)), loc{} {}

  WithLoc(const WithLoc&) = default;
  WithLoc& operator=(const WithLoc&) = default;
  WithLoc(WithLoc&&) = default;
  WithLoc& operator=(WithLoc&&) = default;

  T& operator*() & noexcept { return value; }

  const T& operator*() const& noexcept { return value; }

  T* operator->() noexcept { return std::addressof(value); }

  const T* operator->() const noexcept { return std::addressof(value); }

  operator T&() & noexcept { return value; }

  operator const T&() const& noexcept { return value; }

  operator T() && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return std::move(value);
  }

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

template <typename T>
  requires std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>
struct WithLocs {
  T value{};
  std::vector<SourceRange> locs;

  WithLocs() = default;

  WithLocs(const T& v, SourceRange l) : value(v) { locs.push_back(l); }

  WithLocs(T&& v, SourceRange l) : value(std::move(v)) { locs.push_back(l); }

  WithLocs(const T& v) : value(v), locs{} {}

  WithLocs(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>)
      : value(std::move(v)), locs{} {}

  WithLocs(const WithLocs&) = default;
  WithLocs& operator=(const WithLocs&) = default;
  WithLocs(WithLocs&&) = default;
  WithLocs& operator=(WithLocs&&) = default;

  T& operator*() & noexcept { return value; }

  const T& operator*() const& noexcept { return value; }

  T* operator->() noexcept { return std::addressof(value); }

  const T* operator->() const noexcept { return std::addressof(value); }

  operator T&() & noexcept { return value; }

  operator const T&() const& noexcept { return value; }

  operator T() && noexcept(std::is_nothrow_move_constructible_v<T>) {
    return std::move(value);
  }

  WithLocs& operator=(const T& v)
    requires std::is_copy_assignable_v<T>
  {
    value = v;
    return *this;
  }

  WithLocs& operator=(T&& v)
    requires std::is_move_assignable_v<T>
  {
    value = std::move(v);
    return *this;
  }

  bool operator==(const WithLocs& other) const
    requires std::equality_comparable<T>
  {
    return value == other.value && locs == other.locs;
  }

  bool operator==(const T& other) const
    requires std::equality_comparable<T>
  {
    return value == other;
  }
};

};  // namespace ptx_frontend
