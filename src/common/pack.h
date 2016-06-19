/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016 Yosuke Demura
 * except where otherwise indicated.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMMON_PACK_H_
#define COMMON_PACK_H_

#include <cstddef>
#include <type_traits>
#include <utility>
#include "bitop.h"

// コンパイラにClangまたはGCCを使っている場合は、コンパイラのベクトル拡張を利用する
#if defined(__clang__) || defined(__GNUC__)
# define USE_VECTOR_EXTENSIONS
#endif

/**
 * いくつかの整数型または浮動小数点型の変数をまとめて扱うためのクラスです.
 *
 * コンパイラがベクトル拡張に対応している場合は、Streaming SIMD Extensions (SSE) 等を利用して、
 * 複数の計算を同時に行うため、計算の高速化が期待できます。
 *
 * （ベクトル拡張についての参考文献）
 *   - Free Software Foundation: Using Vector Instructions through Built-in Functions,
 *     https://gcc.gnu.org/onlinedocs/gcc/Vector-Extensions.html.
 *   - The Clang Team: Clang Language Extensions,
 *     http://clang.llvm.org/docs/LanguageExtensions.html.
 */
template<typename T, int kSize>
class Pack {
  static_assert(   std::is_same<T, int8_t  >::value
                || std::is_same<T, int16_t >::value
                || std::is_same<T, int32_t >::value
                || std::is_same<T, int64_t >::value
                || std::is_same<T, uint8_t >::value
                || std::is_same<T, uint16_t>::value
                || std::is_same<T, uint32_t>::value
                || std::is_same<T, uint64_t>::value
                || std::is_same<T, float   >::value
                || std::is_same<T, double  >::value,
                "T must be an integer type or a floating point type.");

  static_assert(bitop::reset_first_bit(kSize) == 0 && kSize > 0,
                "kSize must be power of two.");

  static_assert(sizeof(T) * kSize >= 16,
                "Pack class must be equal to or more than 16 bytes.");

 public:
  typedef T value_type;

  Pack() {}

  explicit Pack(T value) {
    for (int i = 0; i < kSize; ++i) {
#ifdef USE_VECTOR_EXTENSIONS
      vector_[i] = value;
#else
      array_[i] = value;
#endif
    }
  }

  template<typename...Args>
#ifdef USE_VECTOR_EXTENSIONS
  explicit Pack(T arg1, T arg2, Args... args) : vector_{arg1, arg2, args...} {
#else
  explicit Pack(T arg1, T arg2, Args... args) : array_{arg1, arg2, args...} {
#endif
    static_assert(sizeof...(Args) == (kSize - 2),
                  "The number of enumerating constructor arguments must be the "
                  "same as the kSize.");
  }

  Pack& operator=(T value) {
    for (int i = 0; i < kSize; ++i) {
#ifdef USE_VECTOR_EXTENSIONS
      vector_[i] = value;
#else
      array_[i] = value;
#endif
    }
    return *this;
  }

  T operator[](size_t pos) const {
    assert(pos < size());
#ifdef USE_VECTOR_EXTENSIONS
    return vector_[pos];
#else
    return array_[pos];
#endif
  }

  T& operator[](size_t pos) {
    assert(pos < size());
    return array_[pos];
  }

#ifdef USE_VECTOR_EXTENSIONS
# define DEFINE_UNARY_OPERATOR(op)     \
  Pack operator op() const {           \
    Pack result;                       \
    result.vector_ = op vector_;       \
    return result;                     \
  }
#else
# define DEFINE_UNARY_OPERATOR(op)     \
  Pack operator op() const {           \
    Pack result;                       \
    for (int i = 0; i < kSize; ++i) {  \
      result.array_[i] = op array_[i]; \
    }                                  \
    return result;                     \
  }
#endif

  DEFINE_UNARY_OPERATOR(+)
  DEFINE_UNARY_OPERATOR(-)
  DEFINE_UNARY_OPERATOR(~)

#ifdef USE_VECTOR_EXTENSIONS
# define DEFINE_BINARY_OPERATORS(op)                \
  Pack& operator op##=(const Pack& rhs) {           \
    vector_ op##= rhs.vector_;                      \
    return *this;                                   \
  }                                                 \
  Pack& operator op##=(T value) {                   \
	  vector_ op##= value;                            \
    return *this;                                   \
  }                                                 \
  Pack operator op(const Pack& rhs) const {         \
    return Pack(*this) op##= rhs;                   \
  }                                                 \
  Pack operator op(T value) const {                 \
    return Pack(*this) op##= value;                 \
  }                                                 \
  friend Pack operator op(T lhs, const Pack& rhs) { \
    Pack result;                                    \
    result.vector_ = lhs op rhs.vector_;            \
    return result;                                  \
  }
#else
# define DEFINE_BINARY_OPERATORS(op)                \
  Pack& operator op##=(const Pack& rhs) {           \
    for (int i = 0; i < kSize; ++i) {               \
      array_[i] op##= rhs.array_[i];                \
    }                                               \
    return *this;                                   \
  }                                                 \
  Pack& operator op##=(T value) {                   \
    for (int i = 0; i < kSize; ++i) {               \
      array_[i] op##= value;                        \
    }                                               \
    return *this;                                   \
  }                                                 \
  Pack operator op(const Pack& rhs) const {         \
    return Pack(*this) op##= rhs;                   \
  }                                                 \
  Pack operator op(T value) const {                 \
    return Pack(*this) op##= value;                 \
  }                                                 \
  friend Pack operator op(T lhs, const Pack& rhs) { \
    return Pack(lhs) op##= rhs;                     \
  }
#endif

  DEFINE_BINARY_OPERATORS(+)
  DEFINE_BINARY_OPERATORS(-)
  DEFINE_BINARY_OPERATORS(*)
  DEFINE_BINARY_OPERATORS(/)
  DEFINE_BINARY_OPERATORS(%)
  DEFINE_BINARY_OPERATORS(&)
  DEFINE_BINARY_OPERATORS(|)
  DEFINE_BINARY_OPERATORS(^)
  DEFINE_BINARY_OPERATORS(<<)
  DEFINE_BINARY_OPERATORS(>>)

#undef DEFINE_UNARY_OPERATOR
#undef DEFINE_BINARY_OPERATORS

  bool operator==(const Pack& rhs) const {
    for (int i = 0; i < kSize; ++i) {
      if (array_[i] != rhs.array_[i]) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(const Pack& rhs) const {
    return !(*this == rhs);
  }

  size_t size() const {
    return kSize;
  }

  Pack apply(T func(T)) const {
    Pack result;
    for (int i = 0; i < kSize; ++i) {
      result.array_[i] = func(array_[i]);
    }
    return result;
  }

 private:

#ifdef USE_VECTOR_EXTENSIONS
# if defined(__clang__)
    typedef T VectorType __attribute__((ext_vector_type(kSize)));
# elif defined(__GNUC__)
    typedef T VectorType __attribute__((vector_size(sizeof(T) * kSize)));
# else
# error "We can use vector extensions only with Clang or GCC."
# endif
#endif

  union {
#ifdef USE_VECTOR_EXTENSIONS
    VectorType vector_;
#endif
    T array_[kSize];
  };
};

#ifdef USE_VECTOR_EXTENSIONS
# undef USE_VECTOR_EXTENSIONS
#endif

#endif /* COMMON_PACK_H_ */
