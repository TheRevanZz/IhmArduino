#pragma once
#include <string>
#include <string_view>
#include <array>

#define SUBJECT_TYPE \
  X(Set)             \
  X(Cmd)             \
  X(Status)          \
  X(Hb)

enum class ESubjectType
{
#define X(value) value,
  SUBJECT_TYPE
#undef X
};

inline constexpr bool validSubjectVarname(const char *value)
{

  std::string_view str_view{value};
  if (!str_view.starts_with(PREFIX_SUBJECT))
    return false;

  str_view.remove_prefix(std::string_view(PREFIX_SUBJECT).size());

#define X(v)                    \
  if (str_view.starts_with(#v)) \
    return true;
  SUBJECT_TYPE
#undef X
  return false;
}

inline constexpr char* getSubjectTypeFromVarName(const char *varname)
{
  std::string_view str_view{varname};
  str_view.remove_prefix(std::string_view(PREFIX_SUBJECT).size());

#define X(v)                    \
  if (str_view.starts_with(#v)) \
    return #v;
  SUBJECT_TYPE
#undef X
  return nullptr;
}

inline constexpr char toLower(char c)
{
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

template <size_t N>
constexpr auto makeSubjectName(
    std::string_view type,
    const char (&device)[N],
    std::string_view suffix = {})
{
    constexpr size_t MaxSize = 100;

    std::array<char, MaxSize> result{};

    size_t i = 0;

    auto append = [&](std::string_view str)
    {
        for (char c : str)
        {
            if (i < MaxSize - 1)
                result[i++] = toLower(c);
        }
    };

    append(type);
    append("_");
    append(device);

    if (!suffix.empty())
    {
        append("_");
        append(suffix);
    }

    append("\0");

    return result;
}