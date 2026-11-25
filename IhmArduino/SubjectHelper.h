#pragma once
#include "config.h"
#include "Enum.h"
#include <string_view>

/// @brief Déclare un subject MPS à partir du nom de sa variable.
///
/// Le nom de la variable doit commencer par PREFIX_SUBJECT puis contenir
/// un type reconnu : Set, Cmd, Status ou Hb. Le suffixe permet de distinguer
/// plusieurs subjects ayant le même type.
///
/// Cette macro génère :
/// - une constante contenant le nom de chaîne du subject ;
/// - une vérification du nom de variable ;
/// - une vérification du nom de subject ;
/// - l'objet mps::Subject correspondant.
///
/// @param varname Nom de la variable, par exemple kSubStatusCounter.
/// @param suffix Suffixe du nom de subject, par exemple reset_counter.
#define CREATE_SUBJECT(varname, suffix)                                                                                      \
  static_assert(validSubjectVarname(#varname), "Le nom de variable du subject n'est pas valide : " #varname);                \
  inline constexpr auto varname##SubName =                                                                                   \
      makeSubjectName(std::string_view(getSubjectTypeFromVarName(#varname)),                                                 \
                      DEVICE_NAME, std::string_view(#suffix));                                                               \
  static_assert(mps::isValidSubjectName(varname##SubName.data()), "Le nom du subject n'est pas valide"); \
  inline constexpr mps::Subject varname =                                                                                    \
      mps::makeSubject(varname##SubName.data());
