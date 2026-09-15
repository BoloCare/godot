extends Node

# The line run.ps1 waits for: proof that the engine booted and read main.gd out of the pck.
func _ready() -> void:
	var v := Engine.get_version_info()
	print("BOOT: Godot %s on %s %s (%s), user://=%s" % [v["string"], OS.get_name(), OS.get_version(), OS.get_model_name(), OS.get_user_data_dir()])
	get_tree().quit()
