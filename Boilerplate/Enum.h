#pragma once
#include <string>
#include <array>

#define SUBJECT_TYPE \
  X(Set) \
  X(Cmd) \
  X(Status) \
  X(Hb)

enum class ESubjectType {
#define X(value) value,
  SUBJECT_TYPE
#undef X
};

inline constexpr bool ValidSubjectType(const char* value)
{
  #define X(v) \ 
    if(#v == value) \
      return true;
    SUBJECT_TYPE
#undef X
  return false;
}

inline constexpr char toLower(char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

template <size_t N>
constexpr auto MakeSubjectName(
    const char (&type)[N]
)
{
    // N et N2 incluent chacun le caractère '\0'.
    std::array<char, N> result{};

    for(int i = 0; i < N - 1; ++i)
      result[i] = toLower(type[i]);

    result[N - 1] = '\0';

    return result;
}

