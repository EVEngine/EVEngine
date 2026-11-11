# Host-owned offline balancing satellite: the agent search core never includes RPG.
# L2
eve_declare_module(NAME rpg_balance DIR rpg/balance LAYER 2
                   SCRIPT RPGBalance SLOT rpgBalance
                   DEPS agent rpg
                   GROUP 2d 3d)

# L1 -- rpg: domain actor factory, no map/graphics dependency.
eve_declare_module(NAME rpg_spawn LIB EVRPGSpawn DIR rpg/spawn LAYER 1
                   DEPS rpg spawn definitions GROUP 2d 3d)
