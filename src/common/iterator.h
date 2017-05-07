/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
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

#ifndef COMMON_ITERATOR_H_
#define COMMON_ITERATOR_H_

#include <iterator>
#include <type_traits>
#include "bitop.h"

namespace iterator {

/**
 * 符号なし整数をビットセットとみなし、各ビットを参照するためのイテレータです.
 */
template<typename T, typename Base = unsigned>
class BitIterator : public std::iterator<std::input_iterator_tag, T,
    std::ptrdiff_t, void, T> {
 public:
  explicit BitIterator(Base b = 0) : bits_(b) {
  }
  T operator*() const {
    static_assert(std::is_unsigned<decltype(bits_)>::value, "");
    return T(bitop::bsf64(bits_));
  }
  bool operator==(BitIterator rhs) const {
    return bits_ == rhs.bits_;
  }
  bool operator!=(BitIterator rhs) const {
    return !(*this == rhs);
  }
  BitIterator& operator++() {
    bits_ = bitop::reset_first_bit(bits_);
    return *this;
  }
  BitIterator operator++(int) {
    BitIterator temp = *this;
    bits_ = bitop::reset_first_bit(bits_);
    return temp;
  }
 private:
  Base bits_;
};

/**
 * 値を１ずつ増加させて列挙するための擬似イテレータです.
 * @see Sequence
 */
template<typename Incrementable>
class CountingIterator : public std::iterator<std::input_iterator_tag,
    Incrementable, std::ptrdiff_t, void, const Incrementable&> {
 public:
  CountingIterator() : value_() {
  }
  explicit CountingIterator(Incrementable v) : value_(v) {
  }
  const Incrementable& operator*() const {
    return value_;
  }
  bool operator==(const CountingIterator& rhs) const {
    return value_ == rhs.value_;
  }
  bool operator!=(const CountingIterator& rhs) const {
    return !(*this == rhs);
  }
  CountingIterator& operator++() {
    ++value_;
    return *this;
  }
  CountingIterator operator++(int) {
    CountingIterator temp = *this;
    ++value_;
    return temp;
  }
 private:
  Incrementable value_;
};

/**
 * 要素を飛び飛びに参照するために作られている、イテレータのアダプタ・クラスです.
 */
template<typename Iterator>
class StepIterator : public std::iterator<
    typename std::iterator_traits<Iterator>::iterator_category,
    typename std::iterator_traits<Iterator>::value_type,
    typename std::iterator_traits<Iterator>::difference_type,
    typename std::iterator_traits<Iterator>::pointer,
    typename std::iterator_traits<Iterator>::reference> {
 public:
  StepIterator(Iterator it, std::ptrdiff_t step) : it_(it), step_(step) {
  }
  operator Iterator() const {
    return it_;
  }
  typename std::iterator_traits<Iterator>::reference operator*() const {
    return *it_;
  }
  bool operator==(const StepIterator& rhs) const {
    assert(step_ == rhs.step_);
    return it_ == rhs.it_;
  }
  bool operator!=(const StepIterator& rhs) const {
    return !(*this == rhs);
  }
  StepIterator& operator++() {
    it_ = std::next(it_, step_);
    return *this;
  }
  StepIterator operator++(int) {
    StepIterator temp = *this;
    it_ = std::next(it_, step_);
    return temp;
  }
 private:
  Iterator it_;
  std::ptrdiff_t step_;
};

} // namespace iterator

using iterator::BitIterator;
using iterator::CountingIterator;
using iterator::StepIterator;

#endif /* COMMON_ITERATOR_H_ */
