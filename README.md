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

Dropping a gun leaves its loaded rounds with it. To store the ammunition separately, eject the magazine and pick it up. Returning rounds to the ammo pouch plays that ammo type’s pickup sound. On a 10mm pistol the chambered round stays in the gun. An empty gun needs a reload. Dropping a borrowed weapon restores your previous one when its inventory copy can be identified. Condition and installed mods stay with the dropped weapon.

You can borrow one weapon at a time. Quest weapons, scripted pickups and explosives follow TTWVR’s ordinary pickup behavior. Saving while holding a borrowed weapon keeps it in your inventory when that save is loaded.

A thrown weapon damages the first NPC or creature it physically hits. Heavier weapons and faster impacts deal more damage. Gentle drops are harmless, and the weapon stays in the world for you to pick up again.

A hard hit can also stagger the target once. Wider, taller actors resist lighter throws. For example, a thrown pistol can stagger a raider while a super mutant takes a heavier or faster throw. The target plays a brief hit animation if its skeleton has a compatible one.

## INI settings

Edit `Data/NVSE/Plugins/TTWVRFieldWeapons.ini` and restart the game. Set an option to `1` to turn it on or `0` to turn it off. With the defaults, you can throw weapons you pick up in the world. Weapons drawn from your holsters use the usual TTWVR controls.

| Section | Setting | Default | What it does when on |
| --- | --- | --- | --- |
| `[Holsters]` | `HoldToKeep` | `1` | To keep a weapon, hold **A** (right hand) or **X** (left hand) for one second. Keep the button pressed as you release grip. |
| `[Holsters]` | `ThrowableWeapons` | `0` | Draw a weapon from a numbered holster and release grip to drop or throw it. The dropped weapon shows **[Yours]** and remembers its slot. |
| `[Holsters]` | `PhysicalHolstering` | `0` | Release grip inside a numbered holster to store a field pickup there. The previous item stays in inventory. To return a **[Yours]** weapon, use its original holster. |
| `[Holsters]` | `ReturnOwnedAtAnySlot` | `0` | With `PhysicalHolstering=1`, release a **[Yours]** weapon at any enabled holster to send it home. For example, returning a slot 7 pistol at slot 6 sends it to 7 and keeps the item in slot 6. |
| `[NPC]` | `ThrowEmptyPistols` | `1` | Human opponents can throw an empty pistol at close range once they run out of spare ammo. Requires kNVSE Animation Plugin. |

If you drop your pistol and assign a field pickup to its slot, the dropped pistol loses its **[Yours]** label. You can pick it up and assign it to a different slot.

### My recommended settings

I use physical holsters to keep weapons and leave A/X free for TTWVR’s usual controls.

| Section | Setting | Value | In-game behavior |
| --- | --- | --- | --- |
| `[Holsters]` | `HoldToKeep` | `0` | Turns off the A/X hold-to-keep shortcut. You can still use A/X to eject a magazine. |
| `[Holsters]` | `ThrowableWeapons` | `1` | Draw your own weapon, then let go away from a holster to drop it. Swing and let go to throw it. |
| `[Holsters]` | `PhysicalHolstering` | `1` | To keep a weapon you found, release it in a numbered holster. The previous item stays in your inventory. |
| `[Holsters]` | `ReturnOwnedAtAnySlot` | `1` | Release a weapon marked **[Yours]** at any enabled holster to send it back to its original slot. |
| `[NPC]` | `ThrowEmptyPistols` | `1` | Nearby human enemies can throw an empty pistol when they run out of spare ammo. Requires kNVSE. |

For example, if slot 6 holds a Stimpak, returning your slot 7 pistol there keeps the Stimpak in slot 6. Storing a pistol you just found there gives the pistol slot 6 and leaves the Stimpak in your inventory.

## Field Lights and Advanced Holsters

You can keep a pistol and an R-3 flare in the same holster with Field Weapons **1.1.8-r11**, Advanced Holsters **r3**, and Field Lights **r12**. In MO2, place the [r12 flare patch](https://github.com/ethanfel/TTWVR-Advanced-Holsters/releases/download/v1.1.8-r2/TTWVR-Advanced-Holsters-Field-Lights-r12.zip) below Field Lights so its DLL wins.

Assign the pistol as the main item and **R-3 Flares** as the alternate. Grip and pull to draw the pistol. Hold trigger as you grip to draw the flare. With `ThrowableWeapons=1`, release the pistol away from a holster to drop or throw it. Return a capped flare to the holster you drew it from.

## NPC combat

A human opponent can throw their equipped pistol at a visible target within about five metres when the magazine is empty and they have no compatible spare ammo. Enemies with rounds to spare can reload normally. Companions keep their weapons. Miniguns and missile launchers fall outside this rule.

The pistol leaves the NPC’s hand during the throwing animation. It stays empty when picked up. An NPC who finds more ammo can reload it and throw it again after firing the last round. You can dodge it and pick it up where it lands. A hit uses the same weight and speed rules as a player throw.

## Build

Run `make package` with MinGW-w64’s i686 C++ compiler, a native C++ compiler and `zip`. The mod-manager ZIP is written to `build/`. Source is licensed under GPL-3.0-only.
