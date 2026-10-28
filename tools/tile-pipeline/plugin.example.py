"""Example project extension: inject a naming policy without changing EVEngine."""


def register(registry):
    def tag_by_name(context, options):
        prefix = options.get("blockedPrefix", "wall_")
        for tile in context.tiles:
            if tile.name.startswith(prefix):
                tile.metadata["walkable"] = False
                tile.metadata.setdefault("tags", []).append("blocked")

    registry.register("project.tag_by_name", tag_by_name)
