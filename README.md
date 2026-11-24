# TTWVR Field Weapons

Pick up a loose gun or melee weapon in Fallout: New Vegas VR and use it immediately. Fallout temporarily equips the copy in your hand. Guns use the ammo you carry.

## Install

1. Set up Tale of Two Realities VR **1.1.7**, xNVSE and JIP LN NVSE.
2. Install the [Field Weapons release ZIP](https://github.com/ethanfel/TTWVR-Field-Weapons/releases) through your mod manager, or extract it into the game’s `Data` folder. The installed file is `Data/NVSE/Plugins/TTWVRFieldWeapons.dll`.

## Controls

| Action | Control |
| --- | --- |
| Pick up and use | Grab with either hand, or point at a distant weapon, hold grip and flick it toward you. Keep grip held after the catch to use the weapon. |
| Drop or throw | Release grip. Your hand’s motion determines the throw. |
| Keep | While gripping, hold **A** on the right hand or **X** on the left for **one full second**. Keep the button down as you release grip. |

A/X still operates TTWVR’s native controls. On the 10mm pistol it can eject the magazine. Release A/X before releasing grip to drop or throw after using that control.

Dropping the weapon restores your previous one when its inventory copy can be identified. Condition and installed mods stay with the dropped weapon. A dropped gun remembers its magazine count when picked up again with the same ammo type.

You can borrow one weapon at a time. Quest weapons, scripted pickups and explosives follow TTWVR’s ordinary pickup behavior. Saving while holding a borrowed weapon keeps it in your inventory when that save is loaded.

A thrown weapon damages the first NPC or creature it physically hits. Heavier weapons and faster impacts deal more damage. Gentle drops are harmless, and the weapon stays in the world for you to pick up again.

A hard hit can also stagger the target once. Wider, taller actors resist lighter throws. For example, a thrown pistol can stagger a raider while a super mutant takes a heavier or faster throw. The target plays a brief hit animation if its skeleton has a compatible one.

Physical pouch reloading still needs headset testing.

## Build

Run `make package` with MinGW-w64’s i686 C++ compiler, a native C++ compiler and `zip`. The mod-manager ZIP is written to `build/`. Source is licensed under GPL-3.0-only.
