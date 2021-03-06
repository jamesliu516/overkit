// Copyright (c) 2020 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_TEXT_PROCESSING_HPP_INCLUDED
#define OVK_CORE_TEXT_PROCESSING_HPP_INCLUDED

#include <ovk/core/Array.hpp>
// Can't include Debug.hpp because it depends on this header
#include <ovk/core/Debug.h>
#include <ovk/core/Global.hpp>

#include <cstdlib>
#include <cstring>
#include <string>

namespace ovk {
namespace core {

inline std::string FormatNumber(size_t N);
inline std::string FormatNumber(long long N);
inline std::string FormatNumber(int N);

template <typename IntegerType> std::string FormatNumber(IntegerType N,
  const std::string &PluralLabel, const std::string &SingularLabel);

std::string StringReplace(const std::string &String, const std::string &Substring, const std::string
  &Replacement);

template <typename... Ts> std::string StringPrint(const std::string &Format, const Ts &... Args);

}}

#include <ovk/core/TextProcessing.inl>

#endif
