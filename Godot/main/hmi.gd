## @file hmi.gd
## @brief IHM de classe : connexion à la passerelle, grille de tuiles, journal. FOURNI.
##
## @details L'IHM ouvre et ferme la connexion du singleton MPSSerialClient ; elle ne route aucun message :
## chaque tuile s'abonne elle-même à ses subjects (voir core/tile.gd). Le journal affiche l'adresse MAC et le
## canal de la passerelle, que les groupes recopient dans le config.h de leur capteur.
extends Control

## Défilement automatique de la grille, en pixels par seconde (0 : désactivé). Utile sur l'écran de classe.
@export var auto_scroll_speed := 0.0
## Pause en haut et en bas de la grille pendant le défilement automatique, en secondes.
@export var auto_scroll_pause_seconds := 3.0

## Fichier où l'IHM retient le dernier port où une passerelle a répondu, pour s'y reconnecter au démarrage.
const SETTINGS_PATH := "user://hmi.cfg"
## Délai laissé à une passerelle pour ouvrir la session après l'ouverture du port, en secondes.
const GATEWAY_PROBE_TIMEOUT_SECONDS := 5.0
## Pause entre deux recherches complètes quand aucune passerelle n'a répondu, en secondes.
const SEARCH_RETRY_DELAY_SECONDS := 5.0
## Intervalle de vérification de la session pendant l'essai d'un port, en secondes.
const PROBE_POLL_INTERVAL_SECONDS := 0.1

## Tuiles chargées, dans l'ordre d'affichage.
var tiles: Array[Tile] = []
## Sens du défilement automatique (1 : vers le bas, -1 : vers le haut) et pause restante.
var auto_scroll_direction := 1
var auto_scroll_pause_remaining := 0.0
## Position de défilement en pixels, en flottant (ScrollContainer n'accepte que des entiers).
var auto_scroll_position := 0.0
## Port ouvert en ce moment ("" si aucun).
var current_port_name := ""
## Numéro de la recherche en cours ; l'incrémenter interrompt la recherche (nouvel appel, fermeture).
var search_generation := 0
## Vrai pendant une recherche automatique de la passerelle.
var is_searching := false
## Vrai quand l'IHM ferme elle-même le port : port_closed ne doit alors pas relancer de recherche.
var is_closing_on_purpose := false

@onready var tile_scroll: ScrollContainer = %TileScroll
@onready var tile_grid: TileGrid = %TileGrid


func _ready() -> void:
	if OS.has_feature("linux") and OS.has_feature("arm64"):
		Input.mouse_mode = Input.MOUSE_MODE_HIDDEN  # écran tactile de la classe : pas de curseur (le PC garde sa souris)
	MPSSerialClient.port_opened.connect(_on_port_opened)
	MPSSerialClient.port_closed.connect(_on_port_closed)
	MPSSerialClient.session_opened.connect(_on_session_opened)
	MPSSerialClient.session_lost.connect(_on_session_lost)
	MPSSerialClient.gateway_identified.connect(_on_gateway_identified)
	MPSSerialClient.node_online.connect(_on_node_online)
	MPSSerialClient.node_offline.connect(_on_node_offline)
	MPSSerialClient.send_failed.connect(_on_send_failed)
	MPSSerialClient.command_rejected.connect(_on_command_rejected)
	MPSSerialClient.port_error.connect(_on_port_error)

	_load_tiles()
	_set_connected(MPSSerialClient.is_port_open())
	if not MPSSerialClient.is_port_open():
		try_connect()  # aucune intervention au démarrage : dernier port connu, puis tous les ports


func _exit_tree() -> void:
	_stop_search()
	_close_port()


# ------------------------------------------------------------------------------------------------
# Tuiles
# ------------------------------------------------------------------------------------------------

## Charge les tuiles et les place dans la grille ; chaque tuile s'abonne elle-même en entrant dans l'arbre.
func _load_tiles() -> void:
	var errors := PackedStringArray()
	tiles = TileLoader.load_tiles(errors)
	for error in errors:
		printerr("Tuile ignorée : %s" % error)
	for tile in tiles:
		tile.problem_reported.connect(_on_tile_problem_reported.bind(tile))  # avant add_child : _ready() abonne
		tile_grid.add_child(tile)
	print("%d tuile(s) chargée(s)." % tiles.size())


func _on_tile_problem_reported(text: String, tile: Tile) -> void:
	printerr("%s : %s" % [tile.name, text])


func _on_node_online(node_id: int, mac_address: String, device_name: String) -> void:
	print("Appareil en ligne : %s (identifiant %d, %s)" % [device_name, node_id, mac_address])


func _on_node_offline(node_id: int, reason_code: int) -> void:
	printerr("Appareil %d hors ligne (%s)" % [
			node_id, "redémarrage" if reason_code == 1 else "plus de nouvelles"])


# ------------------------------------------------------------------------------------------------
# Connexion à la passerelle
# ------------------------------------------------------------------------------------------------

## Ouvre un port sans rien mémoriser : le port n'est retenu qu'une fois la session ouverte (_on_session_opened).
## @return true si le port est ouvert.
func _open_port(port_name: String) -> bool:
	var result := MPSSerialClient.connect_to_gateway(port_name, 921600, "mps_hmi")
	if result != MPSSerialClient.OK:
		printerr("Ouverture de %s impossible : %s" % [port_name, MPSSerialClient.get_last_error()])
		return false
	current_port_name = port_name
	_set_connected(true)
	return true


## Ferme le port sans déclencher de nouvelle recherche.
func _close_port() -> void:
	is_closing_on_purpose = true
	MPSSerialClient.disconnect_from_gateway()  # émet port_closed tout de suite
	is_closing_on_purpose = false
	current_port_name = ""


## @brief Cherche la passerelle et s'y connecte, sans intervention.
## @details Ferme le port ouvert s'il y en a un, puis essaie le dernier port où une passerelle a répondu, puis
## chaque port détecté. Recommence toutes les SEARCH_RETRY_DELAY_SECONDS tant qu'aucune session ne s'ouvre.
## Appelée au démarrage et quand le port est perdu ; un nouvel appel remplace la recherche en cours.
func try_connect() -> void:
	_stop_search()
	if MPSSerialClient.is_port_open():
		_close_port()
	var generation := search_generation
	is_searching = true
	while generation == search_generation:
		var candidates := _candidate_ports()
		if candidates.is_empty():
			printerr("Aucun port série détecté.")
		for port_name in candidates:
			if await _probe_port(port_name, generation):
				print("Passerelle trouvée sur %s." % port_name)
				is_searching = false
				return
			if generation != search_generation:
				return  # recherche interrompue
		printerr("Aucune passerelle trouvée, nouvel essai dans %d s." % int(SEARCH_RETRY_DELAY_SECONDS))
		await get_tree().create_timer(SEARCH_RETRY_DELAY_SECONDS).timeout


## Interrompt la recherche automatique en cours, s'il y en a une.
func _stop_search() -> void:
	search_generation += 1
	is_searching = false


## Ports à essayer, dans l'ordre : le dernier port retenu (même s'il n'apparaît pas dans la liste, comme le port
## virtuel du simulateur), puis les ports détectés.
func _candidate_ports() -> PackedStringArray:
	var candidates := PackedStringArray()
	var settings := ConfigFile.new()
	if settings.load(SETTINGS_PATH) == OK:
		var saved_port: String = settings.get_value("connection", "port", "")
		if not saved_port.is_empty():
			candidates.append(saved_port)
	for port_name in MPSSerialClient.list_serial_ports():
		if not candidates.has(port_name):
			candidates.append(port_name)
	return candidates


## Ouvre un port et attend l'ouverture de la session. Ferme le port si aucune passerelle ne répond.
## @return true si une passerelle a répondu sur ce port.
func _probe_port(port_name: String, generation: int) -> bool:
	print("Essai de %s..." % port_name)
	if not _open_port(port_name):
		return false
	var waited_seconds := 0.0
	while waited_seconds < GATEWAY_PROBE_TIMEOUT_SECONDS:
		await get_tree().create_timer(PROBE_POLL_INTERVAL_SECONDS).timeout
		if generation != search_generation:
			return false  # recherche remplacée ou arrêtée : son successeur gère le port
		if MPSSerialClient.is_gateway_reachable():
			return true
		if not MPSSerialClient.is_port_open():
			break  # port perdu pendant l'essai
		waited_seconds += PROBE_POLL_INTERVAL_SECONDS
	_close_port()
	printerr("%s : pas de passerelle." % port_name)
	return false


## Retient le port où la passerelle a répondu, pour le prochain démarrage.
func _save_port(port_name: String) -> void:
	var settings := ConfigFile.new()
	settings.set_value("connection", "port", port_name)
	settings.save(SETTINGS_PATH)


func _set_connected(connected: bool) -> void:
	print("En attente de la passerelle..." if connected else "Déconnecté")


func _on_port_opened() -> void:
	_set_connected(true)


func _on_port_closed() -> void:
	_set_connected(false)
	if is_closing_on_purpose or is_searching:
		return
	# Port perdu sans que l'IHM l'ait fermé (câble débranché, passerelle disparue) : on la recherche.
	current_port_name = ""
	printerr("Port perdu, recherche de la passerelle.")
	try_connect.call_deferred()  # pas pendant l'émission du signal par l'extension


func _on_session_opened(_node_id: int) -> void:
	print("Passerelle connectée")
	print("Session ouverte avec la passerelle.")
	_save_port(current_port_name)


func _on_session_lost(_reason_code: int) -> void:
	printerr("Passerelle injoignable, nouvelle tentative...")
	printerr("Session perdue, reconnexion automatique.")


func _on_gateway_identified(mac_address: String, wifi_channel: int) -> void:
	print("Passerelle : %s, canal %d (à recopier dans le config.h de votre capteur)" % [
			mac_address, wifi_channel])


func _on_send_failed(sequence_number: int, _subject_id: int, reason_code: int, status_code: int) -> void:
	printerr("Envoi échoué (séquence %d, raison %d, statut %d)" % [
			sequence_number, reason_code, status_code])


func _on_command_rejected(subject_name: String, error_code: int) -> void:
	printerr("Commande refusée : %s (code %d)" % [subject_name, error_code])


func _on_port_error(message: String) -> void:
	printerr("Erreur du port : %s" % message)
	_set_connected(MPSSerialClient.is_port_open())
