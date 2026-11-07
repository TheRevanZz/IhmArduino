#pragma once
#include "config.h"
#include "Enum.h"

//Permet de d�finir un subject, tout en sp�cifiant le nom du subject
#define MAKE_SUBJECT_BIS(type,name,sub_name)\
  static_assert(ValidSubjectType(#type), "Le type n'est pas coherent");\
  inline constexpr auto KSubName##type##name = MakeSubjectName(#type "_" DEVICE_NAME "_" #sub_name);\
  inline constexpr mps::Subject kSub##type##name = mps::makeSubject(KSubName##type##name.data());

//Permet de d�finir un subject, le nom du subject celui le m�me que name (utilis� pour la variable aussi)
#define MAKE_SUBJECT(type,name)\
	MAKE_SUBJECT_BIS(type,name,name)
  
#define MAKE_SUBJECT_NO_DATA_SEND(type)\
  static_assert(ValidSubjectType(#type), "Le type n'est pas coherent");\
  inline constexpr auto KSubName##type = MakeSubjectName(#type "_" DEVICE_NAME);\
  inline constexpr mps::Subject kSub##type = mps::makeSubject(KSubName##type.data());

