#pragma once
#include <string>
#include <string_view>
#include <array>

/// @brief Liste les types de subject pris en charge par le projet.
/// Chaque valeur est utilisée à la fois dans l'énumération ESubjectType
/// et dans la génération de noms de variables et de subjects.
#define SUBJECT_TYPE \
  X(Set)             \
  X(Cmd)             \
  X(Status)          \
  X(Hb)

/// @brief Types possibles pour un subject MPS.
enum class ESubjectType
{
#define X(value) value,
  SUBJECT_TYPE
#undef X
};

/// @brief Vérifie qu'un nom de variable commence par PREFIX_SUBJECT
/// et correspond à l'un des types déclarés dans SUBJECT_TYPE.
/// @param value Nom de la variable du subject.
/// @return true si le nom est valide, false sinon.
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

/// @brief Récupère le type de sujet à partir du nom de variable
/// @param varname Nom de la variable
/// @return Nom du type trouvé (Set, Cmd, Status ou Hb), ou nullptr
/// si aucun type reconnu ne suit le préfixe PREFIX_SUBJECT.
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

/// @brief Met en minuscule un caractère
/// @param c Caractère à convertir.
/// @return c converti en minuscule s'il s'agit d'une lettre ASCII majuscule,
/// sinon c inchangé.
inline constexpr char toLower(char c)
{
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/// @brief Crée un nom de subject à partir du type, du nom de l'appareil
/// et d'un suffixe optionnel.
/// @tparam N Taille du tableau contenant le nom de l'appareil.
/// @param type Type de subject (Set, Cmd, Status ou Hb).
/// @param device Nom de l'appareil
/// @param suffix Suffixe facultatif ajouté à la fin du nom.
/// @return Tableau de caractères contenant le nom terminé par '\0'.
/// Le résultat suit le format "type_device" ou "type_device_suffix".
template <size_t N>
constexpr auto makeSubjectName(
    std::string_view type,
    const char (&device)[N],
    std::string_view suffix = {})
{
    constexpr size_t MaxSize = 100;

    std::array<char, MaxSize> result{};

    size_t i = 0;

    auto append = [&](std::string_view str, bool lower = true)
    {
        for (char c : str)
        {
            if (i < MaxSize - 1)
                result[i++] = lower ? toLower(c) : c;
        }
    };

    append(type);
    append("_", false);
    append(device);

    if (!suffix.empty())
    {
        append("_",false);
        append(suffix);
    }

    append("\0");

    return result;
}