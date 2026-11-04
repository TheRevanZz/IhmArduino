## @file mps_helpers.gd
## @brief Fonctions helpers pour l'utilisation de l'extension MPS
class_name MPSUtils

## Taille d'un payload de heartbeat : state u8 + counter u16 + uptime_ms u32 (spec §6.2).
const HEARTBEAT_PAYLOAD_SIZE := 7

## @brief Décode un heartbeat `hb_...`.
## @param payload  Octets reçus.
## @return {"state": int (0 = OK, 1 = dégradé, 2 = défaut), "counter": int, "uptime_ms": int},
##         ou {} si la taille est incorrecte.
static func decode_heartbeat(payload: PackedByteArray) -> Dictionary:
	if payload.size() != HEARTBEAT_PAYLOAD_SIZE:
		return {}
	return {
		"state": payload.decode_u8(0),
		"counter": payload.decode_u16(1),
		"uptime_ms": payload.decode_u32(3),
	}
