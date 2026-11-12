#pragma once
#include "config.h"
#include "Enum.h"
#include <string_view>

#define CREATE_SUBJECT(varname, suffix)                                                                                      \
  static_assert(validSubjectVarname(#varname), "Le nom de variable du subject n'est pas valide : " #varname);                \
  inline constexpr auto varname##SubName =                                                                                   \
      makeSubjectName(std::string_view(getSubjectTypeFromVarName(#varname)),                                                 \
                      DEVICE_NAME, std::string_view(#suffix));                                                               \
  static_assert(mps::isValidSubjectName(varname##SubName.data()), "Le nom du subject n'est pas valide"); \
  inline constexpr mps::Subject varname =                                                                                    \
      mps::makeSubject(varname##SubName.data());
