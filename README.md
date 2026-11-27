# TTWVR Field Weapons

Pick up a loose gun or melee weapon in Fallout: New Vegas VR and use it immediately. Fallout temporarily equips the copy in your hand. Loaded guns bring their ammunition with them. You can fire it or eject the magazine using TTWVR’s usual controls.

## Install

1. Set up Tale of Two Realities VR **1.1.8** (1.1.7 also supported), xNVSE and JIP LN NVSE. NPC throws also use kNVSE Animation Plugin.
2. Install the [Field Weapons release ZIP](https://github.com/ethanfel/TTWVR-Field-Weapons/releases) through your mod manager, or extract it into the game’s `Data` folder. The DLL and settings file go in `Data/NVSE/Plugins/`.

To upgrade, replace the previous Field Weapons ZIP in your mod manager.

## Controls

| Action | Control |
| --- | --- |
| Pick up and use | Grab with either hand, or point at a distant weapon, hold grip and flick it toward you. Keep grip held after the catch to use the weapon. |
| Drop or throw | Release grip. Your hand’s motion determines the throw. |
| Keep | While gripping, hold **A** on the right hand or **X** on the left for **one full second**. Keep the button down as you release grip. |

A/X still operates TTWVR’s native controls. On the 10mm pistol it can eject the magazine. Release A/X before releasing grip to drop or throw after using that control.

Dropping a gun leaves its loaded rounds with it. To store the ammunition separately, eject the magazine and pick it up. On a 10mm pistol the chambered round stays in the gun. An empty gun needs a reload. Dropping a borrowed weapon restores your previous one when its inventory copy can be identified. Condition and installed mods stay with the dropped weapon.

You can borrow one weapon at a time. Quest weapons, scripted pickups and explosives follow TTWVR’s ordinary pickup behavior. Saving while holding a borrowed weapon keeps it in your inventory when that save is loaded.

A thrown weapon damages the first NPC or creature it physically hits. Heavier weapons and faster impacts deal more damage. Gentle drops are harmless, and the weapon stays in the world for you to pick up again.

A hard hit can also stagger the target once. Wider, taller actors resist lighter throws. For example, a thrown pistol can stagger a raider while a super mutant takes a heavier or faster throw. The target plays a brief hit animation if its skeleton has a compatible one.

## Optional holster throws

In `Data/NVSE/Plugins/TTWVRFieldWeapons.ini`, set `ThrowableWeapons=1` under `[Holsters]` and restart the game. The default is `0`.

Draw a weapon from a numbered body holster and keep grip held. Release grip to drop or throw it. Pick it up again, hold **A** (right) or **X** (left) for one second, then release grip while holding the button to store it. The original slot is remembered. TTWVR can fill it with an identical spare while the weapon is on the ground. If you assign a different item, it keeps the slot.

## NPC combat

A human opponent can throw their equipped pistol at a visible target within about five metres when the magazine is empty and they have no compatible spare ammo. Enemies with rounds to spare can reload normally. Companions keep their weapons. Miniguns and missile launchers fall outside this rule. Set `ThrowEmptyPistols=0` under `[NPC]` to turn off NPC throws independently.

The pistol leaves the NPC’s hand during the throwing animation. It stays empty when picked up. An NPC who finds more ammo can reload it and throw it again after firing the last round. You can dodge it and pick it up where it lands. A hit uses the same weight and speed rules as a player throw.

## Build

Run `make package` with MinGW-w64’s i686 C++ compiler, a native C++ compiler and `zip`. The mod-manager ZIP is written to `build/`. Source is licensed under GPL-3.0-only.
