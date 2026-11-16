extends Label


# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	update_slider_value(%SwitchDelaySlider.value)
	pass # Replace with function body.


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	pass
	
func update_slider_value(value: float) -> void:
	text = str(roundi(value))
	%SwitchDelaySlider.value = value
