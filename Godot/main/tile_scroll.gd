extends ScrollContainer

@export var snap_step: float = 352

var _tween: Tween

func _ready() -> void:
	scroll_ended.connect(_snap)
	scroll_started.connect(func(): if _tween: _tween.kill())

func _snap() -> void:
	var target := roundf(scroll_horizontal / snap_step) * snap_step
	target = clampf(target, 0.0, _max_scroll())
	if _tween:
		_tween.kill()
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "scroll_horizontal", int(target), .5)

func _max_scroll() -> float:
	return get_h_scroll_bar().max_value - get_h_scroll_bar().page
