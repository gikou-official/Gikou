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

#ifndef USI_H_
#define USI_H_

#include <cassert>
#include <algorithm>
#include <string>
#include <map>
#include <mutex>

/**
 * USI (Universal Shogi Interface) で、GUIと通信するためのクラスです.
 *
 * なお、USIプロトコルの詳細は、http://www.geocities.jp/shogidokoro/usi.html
 * を参照してください。
 */
class Usi {
 public:
  /**
   * USIプロトコルによる通信を開始します.
   */
  static void Start();
};

/**
 * USIオプションを記憶しておくためのクラスです.
 * USIオプションのspinとcheckのみに対応した、簡易な実装になっています。
 */
class UsiOption {
 public:

  enum Type {
    kNoType, kCheck, kSpin, kFileName,
  };

  /**
   * ダミーのデフォルトコンストラクタです.
   *
   * <p>下記のUsiOptionsクラスで利用されている std::map::operator[]は、mapped_typeが
   * default constructibleであることを要求しているため、このコンストラクタがないと
   * コンパイルが通りません。
   * <p>参考: http://en.cppreference.com/w/cpp/container/map/operator_at
   */
  UsiOption()
      : value_(0),
        default_value_(0),
        min_(0),
        max_(0),
        type_(kNoType) {
  }

  /**
   * USIオプション（check box）のコンストラクタです.
   * @param default_value USIオプションの初期値
   */
  UsiOption(bool default_value)
      : value_(default_value),
        default_value_(default_value),
        min_(0),
        max_(1),
        type_(kCheck) {
  }

  /**
   * USIオプション（spin）のコンストラクタです.
   * @param default_value USIオプションの初期値
   * @param min USIオプションの最小値
   * @param max USIオプションの最大値
   */
  UsiOption(int default_value, int min, int max)
      : value_(std::min(std::max(default_value, min), max)),
        default_value_(default_value),
        min_(min),
        max_(max),
        type_(kSpin) {
    assert(min <= default_value && default_value <= max);
  }

  /**
   * USIオプション（filename）のコンストラクタです.
   * @param default_string USIオプションの初期値
   */
  UsiOption(const char* default_filename)
      : value_(0),
        string_(default_filename),
        default_string_(default_filename),
        default_value_(0),
        min_(0),
        max_(0),
        type_(kFileName) {
  }

  /**
   * USIオプションの値を設定します.
   */
  UsiOption& operator=(const std::string& value) {
    if (type_ == kCheck) {
      value_ = static_cast<int>(value == "true");
    } else if (type_ == kSpin) {
      value_ = std::min(std::max(std::stoi(value), min_), max_);
    } else if (type_ == kFileName) {
      string_ = value;
    }
    return *this;
  }

  operator int() const {
    return value_;
  }

  int default_value() const {
    return default_value_;
  }

  const std::string& string() const {
    return string_;
  }

  const std::string& default_string() const {
    return default_string_;
  }

  int min() const {
    return min_;
  }

  int max() const {
    return max_;
  }

  Type type() const {
    return type_;
  }

 private:
  int value_;
  std::string string_, default_string_;
  const int default_value_, min_, max_;
  const Type type_;
};

/**
 * 複数のUSIオプションを格納するためのコンテナです.
 */
class UsiOptions {
 public:
  /**
   * USIオプションの初期値などをあらかじめ設定するためのコンストラクタです.
   */
  UsiOptions();

  /**
   *　USIオプションの一覧を標準出力にプリントします.
   *
   * 標準出力への出力例：
   * <pre>
   * option name Threads type spin default 1 min 1 max 64
   * option name USI_Hash type spin default 256 min 1 max 65536
   * option name USI_Ponder type check default false
   * </pre>
   */
  void PrintListOfOptions();

  /**
   * USIオプション名からUSIオプション値を参照します.
   * @param key USIオプション名
   * @return USIオプションの値
   */
  UsiOption& operator[](const std::string& key) {
    return map_[key];
  }

  /**
   * USIオプション名からUSIオプション値を参照します.
   *
   * 使用上の注意：この関数は読み取り専用です。必ずkeyが存在する場合のみ使用してください。
   * @param key USIオプション名
   * @return USIオプションの値
   */
  UsiOption operator[](const std::string& key) const {
    assert(map_.find(key) != map_.end());
    return map_.find(key)->second;
  }

 private:
  std::map<std::string, UsiOption> map_;
};

#endif /* USI_H_ */
