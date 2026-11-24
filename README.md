# TTWVR Field Weapons

Pick up a loose gun or melee weapon in Fallout: New Vegas VR and use it immediately. Fallout temporarily equips the copy in your hand. Guns use the ammo you carry.

## Install

1. Set up Tale of Two Realities VR **1.1.7**, xNVSE and JIP LN NVSE.
2. Install the [Field Weapons release ZIP](https://github.com/ethanfel/TTWVR-Field-Weapons/releases) through your mod manager, or extract it into the game’s `Data` folder. The installed file is `Data/NVSE/Plugins/TTWVRFieldWeapons.dll`.

## Controls

| Action | Control |
| --- | --- |
| Pick up and use | Grab the weapon with either hand and hold grip. Use TTWVR’s normal combat controls. |
| Drop or throw | Release grip. Your hand’s motion determines the throw. |
| Keep | While gripping, hold **A** on the right hand or **X** on the left for 0.4 seconds. Keep the button down as you release grip. |

Dropping the weapon restores your previous one when its inventory copy can be identified. Condition and installed mods stay with the dropped weapon. A dropped gun remembers its magazine count when picked up again with the same ammo type.

You can borrow one weapon at a time. Quest weapons, scripted pickups and explosives follow TTWVR’s ordinary pickup behavior. Saving while holding a borrowed weapon keeps it in your inventory when that save is loaded.

Tested in Fallout through Proton with simulated VR controllers. Physical pouch reloading and thrown impacts against NPCs still need headset testing.

## Build

Run `make package` with MinGW-w64’s i686 C++ compiler, a native C++ compiler and `zip`. The mod-manager ZIP is written to `build/`. Source is licensed under GPL-3.0-only.
