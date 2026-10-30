# Host-owned offline balancing satellite: the agent search core never includes RPG.
# L2
eve_declare_module(NAME rpg_balance DIR rpg/balance LAYER 2
                   SCRIPT RPGBalance SLOT rpgBalance
                   DEPS agent rpg
                   GROUP 2d 3d)
