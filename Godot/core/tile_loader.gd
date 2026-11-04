## @file tile_loader.gd
## @brief Trouve et charge les tuiles des groupes. FOURNI : ne pas modifier.
##
## @details Chaque tuile est la scène `tile.tscn` d'un sous-dossier de res://tiles/. La recherche passe par
## ResourceLoader.list_directory() (Godot 4.4 et suivants), qui fonctionne aussi dans un projet exporté où les
## scènes ne sont plus des fichiers .tscn.
class_name TileLoader
extends RefCounted

## Dossier qui contient un sous-dossier par tuile.
const TILES_DIRECTORY := "res://tiles/"
## Nom de la scène attendue dans chaque sous-dossier.
const TILE_SCENE_NAME := "tile.tscn"


## @brief Instancie toutes les tuiles trouvées, dans l'ordre alphabétique des dossiers.
## @param errors  Reçoit un message par dossier ignoré (scène absente, racine qui n'est pas une Tile).
## @return Les tuiles instanciées, pas encore ajoutées à l'arbre.
static func load_tiles(errors: PackedStringArray) -> Array[Tile]:
	var tiles: Array[Tile] = []
	var folder_names := Array(ResourceLoader.list_directory(TILES_DIRECTORY))
	folder_names.sort()
	for folder_name: String in folder_names:
		if not folder_name.ends_with("/"):
			continue  # un fichier posé directement dans tiles/ n'est pas une tuile
		var scene_path := TILES_DIRECTORY + folder_name + TILE_SCENE_NAME
		if not ResourceLoader.exists(scene_path):
			errors.append("%s : pas de %s" % [folder_name, TILE_SCENE_NAME])
			continue
		var scene := load(scene_path) as PackedScene
		var instance := scene.instantiate() if scene else null
		if not instance is Tile:
			errors.append("%s : la racine de %s n'est pas une Tile" % [folder_name, TILE_SCENE_NAME])
			if instance:
				instance.free()
			continue
		instance.name = folder_name.trim_suffix("/")
		tiles.append(instance)
	return tiles
