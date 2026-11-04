extends TextureRect
class_name StateIcon

@export var state: bool = true:
	set(value):
		if value == state: return
		state = value
		if is_node_ready(): _tween_state()
@export var _modulate_time: float = .25
@export var _color_on: Color = Color(0xffffffff)
@export var _color_off: Color = Color(0xffffff80)

var _modulate_tween: Tween

func _enter_tree() -> void:
	modulate = _color_on if state else _color_off # ~ opérateur ternaire

func _tween_state() -> void:
	if _modulate_tween: _modulate_tween.kill()
	_modulate_tween = create_tween()
	var target_color := _color_on if state else _color_off
	_modulate_tween.tween_property(self, "modulate", target_color, _modulate_time).set_trans(Tween.TRANS_QUAD)
