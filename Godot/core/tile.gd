## @file tile.gd
## @brief Classe de base de toutes les tuiles de l'IHM. FOURNI : ne pas modifier.
##
## @details Une tuile affiche un appareil du réseau MPS. Elle s'abonne elle-même, à son entrée dans l'arbre,
## aux subjects qu'elle déclare dans get_subscriptions() : le singleton MPSSerialClient n'appelle on_message()
## que pour ces subjects (patron Observer). L'IHM ne fait que la connexion à la passerelle ; elle ne route rien.
##
## Pour créer votre tuile : nouvelle scène héritée de res://core/tile_base.tscn, enregistrée sous
## res://tiles/<nom_de_votre_appareil>/tile.tscn, avec un script qui commence par `extends Tile` et redéfinit
## les méthodes utiles ci-dessous. Toutes ont un comportement par défaut : un journal dans la console, si
## is_logging_enabled est coché. Si vous redéfinissez _ready() ou _exit_tree(), appelez super().
class_name Tile
extends Panel

## Émis quand un abonnement ou une publication est refusé ; l'IHM l'écrit dans son journal.
## @param text  Description du problème.
signal problem_reported(text: String)

## Affiche dans la console Godot les événements reçus par la tuile.
@export var is_logging_enabled := false


func _ready() -> void:
	for subject_name in get_subscriptions():
		var result := MPSSerialClient.subscribe_to(subject_name, on_message)
		if result != MPSSerialClient.OK:
			problem_reported.emit("abonnement à « %s » refusé (code %d)" % [subject_name, result])
	MPSSerialClient.node_online.connect(on_node_online)
	MPSSerialClient.node_offline.connect(on_node_offline)
	MPSSerialClient.gateway_reachability_changed.connect(on_gateway_reachability_changed)
	on_gateway_reachability_changed(MPSSerialClient.is_gateway_reachable())


func _exit_tree() -> void:
	MPSSerialClient.unsubscribe_all(self)
	MPSSerialClient.node_online.disconnect(on_node_online)
	MPSSerialClient.node_offline.disconnect(on_node_offline)
	MPSSerialClient.gateway_reachability_changed.disconnect(on_gateway_reachability_changed)


## @brief Liste des subjects que la tuile veut recevoir. Appelée une fois, à l'entrée de la tuile dans l'arbre.
## @return Les noms de subjects ; un nom invalide est signalé dans le journal de l'IHM et ignoré.
func get_subscriptions() -> PackedStringArray:
	return PackedStringArray()


## @brief Un message est arrivé sur l'un des subjects de get_subscriptions().
## @param subject_name  Nom du subject.
## @param source_id  Identifiant de l'appareil émetteur.
## @param payload  Octets du message : vérifiez leur nombre avant de les lire.
func on_message(subject_name: String, source_id: int, payload: PackedByteArray) -> void:
	log_message("message %s de %d : %s" % [subject_name, source_id, payload.hex_encode()])


## @brief Un appareil vient de s'enregistrer auprès de la passerelle.
## @param node_id  Identifiant attribué par la passerelle.
## @param mac_address  Adresse MAC de l'appareil (« B0:A6:04:00:74:E0 »).
## @param device_name  Nom de l'appareil (« sensor_a »).
func on_node_online(node_id: int, mac_address: String, device_name: String) -> void:
	log_message("appareil en ligne : %s (%d, %s)" % [device_name, node_id, mac_address])


## @brief Un appareil a quitté le réseau.
## @param node_id  Identifiant de l'appareil.
## @param reason_code  0 : plus de nouvelles depuis 15 s ; 1 : l'appareil a redémarré.
func on_node_offline(node_id: int, reason_code: int) -> void:
	log_message("appareil %d hors ligne (raison %d)" % [node_id, reason_code])


## @brief La liaison avec la passerelle s'ouvre ou se perd. Appelée aussi une fois au chargement de la tuile.
## @param is_gateway_reachable  true quand la tuile peut publier, false sinon.
func on_gateway_reachability_changed(is_gateway_reachable: bool) -> void:
	log_message("passerelle %s" % ("joignable" if is_gateway_reachable else "injoignable"))


## @brief Publie un message sur le réseau MPS.
## @param subject_name  Nom du subject.
## @param payload  Octets du message (238 au plus).
## @param request_acknowledgement  true pour les set_ et les cmd_.
## @return MPSSerialClient.OK, NOT_REGISTERED si la passerelle est injoignable, ou un autre code d'erreur.
func publish(subject_name: String, payload: PackedByteArray, request_acknowledgement := false) -> int:
	log_message("publie %s : %s" % [subject_name, payload.hex_encode()])
	var result := MPSSerialClient.publish(subject_name, payload, request_acknowledgement)
	if result == MPSSerialClient.NOT_REGISTERED or result == MPSSerialClient.PORT_CLOSED:
		problem_reported.emit("passerelle injoignable, %s non envoyé" % subject_name)
	elif result != MPSSerialClient.OK:
		problem_reported.emit("publication de %s refusée (code %d)" % [subject_name, result])
	return result


## @brief Écrit une ligne dans la console Godot si is_logging_enabled est coché.
## @param text  Texte à écrire ; il est précédé du nom de la tuile.
func log_message(text: String) -> void:
	if is_logging_enabled:
		print("[%s] %s" % [name, text])
