extends Tile

# ---- CONSTANTES ----
const DEVICE_NAME = "sensor_d"
const SUB_HB = "hb_" + DEVICE_NAME
const SUB_COUNTER = "status_" + DEVICE_NAME + "_counter"
const SUB_SET_LIGHT_COLOR = "set_" + DEVICE_NAME + "_light_color"
const SUB_STATUS_LIGHT_COLOR = "status_" + DEVICE_NAME + "_light_color"
const SUB_SET_SWITCH_THRESHOLD = "set_" + DEVICE_NAME + "_switch_threshold"
const SUB_STATUS_SWITCH_THRESHOLD = "status_" + DEVICE_NAME + "_switch_threshold"
## Délai sans heartbeat au-delà duquel le capteur est considéré comme bloqué (3,5 périodes de 1 s).
const HB_TIMEOUT_S := 3.5
## Durée d'allumage de l'icône de heartbeat à chaque réception.
const HB_BLINK_S := 0.5

var count = 0;

# ---- VARIABLES ----
var _node_id: int = -1
var _seconds_since_hb := INF
# Variable "_connected" avec setter personnalisé pour appeler la MAJ de l'UI au changement d'état
var _connected: bool = false:
	set(value):
		if value == _connected: return
		_connected = value
		if _connected:
			_on_sensor_connected()
		else:
			_on_sensor_disconnected()
			

#func _ready() -> void:
	#super._ready()
	
func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		publish(SUB_SET_LIGHT_COLOR, PackedByteArray([0,0,0]), false)


func get_subscriptions() -> PackedStringArray:
	var subscriptions: PackedStringArray = []
	subscriptions.append(SUB_HB)
	subscriptions.append(SUB_COUNTER)
	return subscriptions


## Source principale du node_id : la passerelle annonce le capteur par son nom dès son enregistrement,
## avant ses premières publications (dont l'état republié dans onSessionOpened côté Arduino).
func on_node_online(node_id: int, _mac_address: String, device_name: String) -> void:
	if device_name != DEVICE_NAME: return
	_node_id = node_id  # nouvel identifiant possible après un redémarrage du capteur
	%IDLabel.text = "node_id: %d" % _node_id


func on_node_offline(node_id: int, _reason_code: int) -> void:
	if node_id == _node_id: _connected = false


func on_gateway_reachability_changed(is_gateway_reachable: bool) -> void:
	if is_gateway_reachable: return
	_connected = false
	_node_id = -1  # la passerelle peut redistribuer les identifiants en redémarrant


func on_message(subject_name: String, source_id: int, _payload: PackedByteArray) -> void:
	if subject_name == SUB_HB: _on_hb(source_id)
	if source_id != _node_id: return # ne traitez que les messages dédiés à votre capteur
	# Ajoutez vos messages à la suite
	if subject_name == SUB_COUNTER: _on_counter(_payload)
	if subject_name == SUB_STATUS_LIGHT_COLOR: _on_light_color(_payload)
	if subject_name == SUB_STATUS_SWITCH_THRESHOLD: _on_switch_threshold(_payload)


func _on_counter(_payload : PackedByteArray):
	%CounterLabel.text = "Compteur: " + str(_payload.get(0))

func _on_switch_threshold(_payload : PackedByteArray):
	var value = _payload.decode_u8(0)
	%SliderLabel.update_slider_value(value)

func _process(delta: float) -> void:
	_seconds_since_hb += delta
	%HeartbeatIcon.state = _seconds_since_hb < HB_BLINK_S
	if _connected and _seconds_since_hb >= HB_TIMEOUT_S:
		_connected = false  # plus de heartbeat : code du capteur bloqué ou capteur hors de portée


func _on_sensor_connected():
	_on_light_power_button_toggled(false)
	%IDLabel.text = "node_id: %d" % _node_id
	%ConnectedIcon.state = true


func _on_sensor_disconnected():
	%IDLabel.text = "node_id: ∅"
	%ConnectedIcon.state = false

## @brief Traite un heartbeat : (re)connecte le capteur et relance la temporisation.
## @param source_id  Identifiant de l'appareil émetteur.
func _on_hb(source_id: int):
	# Secours si la tuile n'a pas vu on_node_online (IHM lancée après le capteur, par exemple)
	if _node_id == -1: _node_id = source_id
	if source_id != _node_id: return
	_seconds_since_hb = 0.0
	_connected = true


func _on_light_power_button_toggled(toggled_on: bool) -> void:
	print("toggle")
	
	var color_picker_button : ColorPickerButton = %ColorPickerButton
	if toggled_on:
		var color := transform_color(color_picker_button.color)
		print("true")
		print(color)
		var payload = PackedByteArray(Array(color))
		publish(SUB_SET_LIGHT_COLOR, payload, true)
	else:
		print("false")
		var color := [0,0,0]
		var payload = PackedByteArray(color)
		publish(SUB_SET_LIGHT_COLOR, payload, true)

func transform_color(color: Color) -> Array[int]:
	return [
		clampi(roundi(color.r * 255.0),0,255), 
		clampi(roundi(color.g * 255.0),0,255), 
		clampi(roundi(color.b * 255.0),0,255)
	]
	
func _on_color_picker_button_color_changed(color: Color) -> void:
	if !%LightPowerButton.button_pressed:
		return;

	var finalColor := transform_color(color)
	var payload = PackedByteArray(finalColor)
	publish(SUB_SET_LIGHT_COLOR, payload, true)
	
func _on_light_color(_payload : PackedByteArray) -> void:
	var r := _payload.decode_u8(0) / 255.0
	var g := _payload.decode_u8(1) / 255.0
	var b := _payload.decode_u8(2) / 255.0
	%ColorPickerButton.color = Color(r,g,b);
	
	if r <= 0 && g <= 0 && b <= 0:
		%LightPowerButton.button_pressed = false
	else:
		%LightPowerButton.button_pressed = true

	


func _on_switch_delay_slider_value_changed(value: float) -> void:
	var payload = PackedByteArray([value])
	publish(SUB_SET_SWITCH_THRESHOLD, payload, true)
