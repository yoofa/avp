/*
 * Lookup.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef LOOKUP_H
#define LOOKUP_H

#include <utility>
#include <vector>

namespace avp {

template <typename T, typename U>
struct Lookup {
  Lookup(std::initializer_list<std::pair<T, U>> list);

  bool lookup(const T& from, U* to) const;
  bool rlookup(const U& from, T* to) const;

  template <
      typename V,
      typename = typename std::enable_if<!std::is_same<T, V>::value>::type>
  inline bool map(const T& from, V* to) const {
    return lookup(from, to);
  }

  template <
      typename V,
      typename = typename std::enable_if<!std::is_same<T, V>::value>::type>
  inline bool map(const V& from, T* to) const {
    return rlookup(from, to);
  }

 private:
  std::vector<std::pair<T, U>> mTable;
};

template <typename T, typename U>
Lookup<T, U>::Lookup(std::initializer_list<std::pair<T, U>> list)
    : mTable(list) {}

template <typename T, typename U>
bool Lookup<T, U>::lookup(const T& from, U* to) const {
  for (auto elem : mTable) {
    if (elem.first == from) {
      *to = elem.second;
      return true;
    }
  }
  return false;
}

template <typename T, typename U>
bool Lookup<T, U>::rlookup(const U& from, T* to) const {
  for (auto elem : mTable) {
    if (elem.second == from) {
      *to = elem.first;
      return true;
    }
  }
  return false;
}
} /* namespace avp */

#endif /* !LOOKUP_H */
