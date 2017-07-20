#pragma once
#include <type_traits>

// Enum class bitwise operators
#define IMPLEMENT_ENUM_CLASS_BITWISE_OPERATORS(type_)                                                                  \
  inline type_ operator&(type_ lhs, type_ rhs)                                                                         \
  {                                                                                                                    \
    return static_cast<type_>(static_cast<std::underlying_type<type_>::type>(lhs) &                                    \
                              static_cast<std::underlying_type<type_>::type>(rhs));                                    \
  }                                                                                                                    \
  inline type_ operator|(type_ lhs, type_ rhs)                                                                         \
  {                                                                                                                    \
    return static_cast<type_>(static_cast<std::underlying_type<type_>::type>(lhs) |                                    \
                              static_cast<std::underlying_type<type_>::type>(rhs));                                    \
  }                                                                                                                    \
  inline type_ operator^(type_ lhs, type_ rhs)                                                                         \
  {                                                                                                                    \
    return static_cast<type_>(static_cast<std::underlying_type<type_>::type>(lhs) ^                                    \
                              static_cast<std::underlying_type<type_>::type>(rhs));                                    \
  }                                                                                                                    \
  inline type_ operator~(type_ val)                                                                                    \
  {                                                                                                                    \
    return static_cast<type_>(~static_cast<std::underlying_type<type_>::type>(val));                                   \
  }                                                                                                                    \
  inline type_& operator&=(type_& lhs, type_ rhs)                                                                      \
  {                                                                                                                    \
    lhs = static_cast<type_>(static_cast<std::underlying_type<type_>::type>(lhs) &                                     \
                             static_cast<std::underlying_type<type_>::type>(rhs));                                     \
    return lhs;                                                                                                        \
  }                                                                                                                    \
  inline type_& operator|=(type_& lhs, type_ rhs)                                                                      \
  {                                                                                                                    \
    lhs = static_cast<type_>(static_cast<std::underlying_type<type_>::type>(lhs) |                                     \
                             static_cast<std::underlying_type<type_>::type>(rhs));                                     \
    return lhs;                                                                                                        \
  }                                                                                                                    \
  inline type_& operator^=(type_& lhs, type_ rhs)                                                                      \
  {                                                                                                                    \
    lhs = static_cast<type_>(static_cast<std::underlying_type<type_>::type>(lhs) ^                                     \
                             static_cast<std::underlying_type<type_>::type>(rhs));                                     \
    return lhs;                                                                                                        \
  }
