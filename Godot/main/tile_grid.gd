@tool
class_name TileGrid
extends Container

@export_range(1, 64) var rows: int = 3:
	set(v):
		rows = maxi(v, 1)
		queue_sort()
		update_minimum_size()

@export var spacing: Vector2 = Vector2(8, 8):
	set(v):
		spacing = v
		queue_sort()
		update_minimum_size()

# Taille des cellules : celle de la plus grande tuile (pas de redimensionnement automatique)
func _cell_size() -> Vector2:
	var s := Vector2.ZERO
	for c in get_children():
		if c is Control and c.visible:
			s = s.max(c.get_combined_minimum_size())
	return s

func _get_visible_children() -> Array[Control]:
	var out: Array[Control] = []
	for c in get_children():
		if c is Control and c.visible:
			out.append(c)
	return out

func _get_minimum_size() -> Vector2:
	var n := _get_visible_children().size()
	if n == 0:
		return Vector2.ZERO
	var cell := _cell_size()
	var cols := ceili(float(n) / rows)
	var used_rows := mini(rows, n)
	return Vector2(
		cols * cell.x + (cols - 1) * spacing.x,
		used_rows * cell.y + (used_rows - 1) * spacing.y)

func _notification(what: int) -> void:
	if what == NOTIFICATION_SORT_CHILDREN:
		var cell := _cell_size()
		var i := 0
		for c in _get_visible_children():
			var col := i / rows   # division entière
			var row := i % rows
			fit_child_in_rect(c, Rect2(
				Vector2(col * (cell.x + spacing.x), row * (cell.y + spacing.y)),
				cell))
			i += 1
	elif what == NOTIFICATION_CHILD_ORDER_CHANGED:
		update_minimum_size()
