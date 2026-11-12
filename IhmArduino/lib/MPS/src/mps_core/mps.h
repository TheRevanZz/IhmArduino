/**
 * @file mps.h
 * @brief Point d'entrée unique du cœur MPS v1 : inclut tous les modules sans dépendance de plateforme.
 *
 * @details Les moteurs Client (mps_client.h) et Gateway (mps_gateway.h) ne sont pas inclus ici :
 * un nœud n'a pas besoin du moteur de passerelle, et inversement.
 */
#pragma once

#include "mps_cobs.h"
#include "mps_crc16.h"
#include "mps_duplicate_window.h"
#include "mps_endian.h"
#include "mps_frame.h"
#include "mps_protocol.h"
#include "mps_subject.h"
