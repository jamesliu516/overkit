#ifndef OVK_CORE_ITERATOR_TRAITS_HPP_LOADED
#define OVK_CORE_ITERATOR_TRAITS_HPP_LOADED

#include <ovk/core/Requires.hpp>

#include <iterator>
#include <type_traits>

namespace ovk {
namespace core {

namespace iterator_traits_internal {
template <typename T> constexpr std::true_type IsIteratorTest(typename std::iterator_traits<T>::
  iterator_category *) { return {}; }
template <typename T> constexpr std::false_type IsIteratorTest(...) { return {}; }
}
template <typename T> constexpr bool IsIterator() {
  return decltype(iterator_traits_internal::IsIteratorTest<T>(nullptr))::value;
}

namespace iterator_traits_internal {
template <typename T, typename=void> struct alias_helper;
template <typename T> struct alias_helper<T, OVK_SPECIALIZATION_REQUIRES(IsIterator<T>())> {
  using difference_type = typename std::iterator_traits<T>::difference_type;
  using value_type = typename std::iterator_traits<T>::value_type;
  using pointer = typename std::iterator_traits<T>::pointer;
  using reference = typename std::iterator_traits<T>::reference;
  using iterator_category = typename std::iterator_traits<T>::iterator_category;
};
template <typename T> struct alias_helper<T, OVK_SPECIALIZATION_REQUIRES(!IsIterator<T>())> {
  using difference_type = std::false_type;
  using value_type = std::false_type;
  using pointer = std::false_type;
  using reference = std::false_type;
  using iterator_category = std::false_type;
};
}
template <typename T> using iterator_difference_type = typename iterator_traits_internal::
  alias_helper<T>::difference_type;
template <typename T> using iterator_value_type = typename iterator_traits_internal::
  alias_helper<T>::value_type;
template <typename T> using iterator_pointer_type = typename iterator_traits_internal::
  alias_helper<T>::pointer;
template <typename T> using iterator_reference_type = typename iterator_traits_internal::
  alias_helper<T>::reference;
template <typename T> using iterator_category = typename iterator_traits_internal::
  alias_helper<T>::iterator_category;

template <typename T, OVK_FUNCTION_REQUIRES(IsIterator<T>())> constexpr bool IsInputIterator() {
  return
    std::is_same<iterator_category<T>, std::input_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::forward_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::bidirectional_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::random_access_iterator_tag>::value;
}
template <typename T, OVK_FUNCTION_REQUIRES(!IsIterator<T>())> constexpr bool IsInputIterator() {
  return false;
}

template <typename T, OVK_FUNCTION_REQUIRES(IsIterator<T>())> constexpr bool IsOutputIterator() {
  return
    std::is_same<iterator_category<T>, std::output_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::forward_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::bidirectional_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::random_access_iterator_tag>::value;
}
template <typename T, OVK_FUNCTION_REQUIRES(!IsIterator<T>())> constexpr bool IsOutputIterator() {
  return false;
}

template <typename T, OVK_FUNCTION_REQUIRES(IsIterator<T>())> constexpr bool IsForwardIterator() {
  return
    std::is_same<iterator_category<T>, std::forward_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::bidirectional_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::random_access_iterator_tag>::value;
}
template <typename T, OVK_FUNCTION_REQUIRES(!IsIterator<T>())> constexpr bool IsForwardIterator() {
  return false;
}

template <typename T, OVK_FUNCTION_REQUIRES(IsIterator<T>())> constexpr bool
  IsBidirectionalIterator() {
  return
    std::is_same<iterator_category<T>, std::bidirectional_iterator_tag>::value ||
    std::is_same<iterator_category<T>, std::random_access_iterator_tag>::value;
}
template <typename T, OVK_FUNCTION_REQUIRES(!IsIterator<T>())> constexpr bool
  IsBidirectionalIterator() {
  return false;
}

template <typename T, OVK_FUNCTION_REQUIRES(IsIterator<T>())> constexpr bool
  IsRandomAccessIterator() {
  return std::is_same<iterator_category<T>, std::random_access_iterator_tag>::value;
}
template <typename T, OVK_FUNCTION_REQUIRES(!IsIterator<T>())> constexpr bool
  IsRandomAccessIterator() {
  return false;
}

namespace is_const_iterator_internal {
  template <typename T> struct test : std::false_type {};
  template <typename U> struct test<const U *> : std::true_type {};
}
template <typename T, OVK_FUNCTION_REQUIRES(IsIterator<T>())> constexpr bool IsConstIterator() {
  return is_const_iterator_internal::test<iterator_pointer_type<T>>::value;
}
template <typename T, OVK_FUNCTION_REQUIRES(!IsIterator<T>())> constexpr bool IsConstIterator() {
  return false;
}

namespace iterator_traits_internal {
template <typename T, typename=void> struct deref_type_helper;
template <typename T> struct deref_type_helper<T, OVK_SPECIALIZATION_REQUIRES(IsIterator<T>())> {
  using type = decltype(*std::declval<T>());
};
template <typename T> struct deref_type_helper<T, OVK_SPECIALIZATION_REQUIRES(!IsIterator<T>())> {
  using type = std::false_type;
};
}
template <typename T> using iterator_deref_type = typename iterator_traits_internal::
  deref_type_helper<T>::type;

}}

#endif
