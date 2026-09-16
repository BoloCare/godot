extends SceneTree

# Writes project/ into a pck with the engine's own PCKPacker, so no export template or preset
# is needed. Run by build.ps1 with a desktop Godot 4.7:
#   godot --headless --path platform/uwp/deploy/project -s ../pack.gd -- <out.pck>
func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		push_error("pack.gd: pass the output pck path after --")
		quit(1)
		return
	var packer := PCKPacker.new()
	var err := packer.pck_start(args[0])
	if err != OK:
		push_error("pack.gd: cannot write %s (%d)" % [args[0], err])
		quit(1)
		return
	for f in ["project.godot", "main.tscn", "main.gd"]:
		packer.add_file("res://" + f, "res://" + f)
	packer.flush(true)
	print("packed ", args[0])
	quit(0)
