# UnitOutline

Reaction-colored silhouette outline on your target and mouseover unit for the WoW 1.12.1 client
(build 5875, Direct3D 9). A small DLL plus a GUI addon.

- Red hostile, yellow neutral, green friendly. Weapons and mounts are outlined with the unit.
- Settings live in an ESC menu button, "Unit Outline": enable, target, mouseover, thickness,
  opacity, three colors. No slash commands.

## Install

1. Get `UnitOutline.dll` from the Actions artifact (`UnitOutline-x86`) and load it like any other
   client DLL (same way as UnitXP SP3, for example).
2. Copy `addon/UnitOutline` into `Interface/AddOns`.

## Build

The GitHub workflow builds it with clang-cl and xwin. Locally:

    make -f UnitOutline.mk XWIN_ROOT=./xwinSDK MINHOOK_DIR=./MinHook_134

`CXXDEFS=/DOUTLINE_DEBUG` builds a variant that prints "selected model was in no batch" to the
debug output when a selected unit's model is never matched in the scene.

## How it works

The scene's per-batch draw functions are hooked to know which M2 model is being drawn. When it is
a selected unit's model, the D3D9 draw call is repeated flat-colored into an off-screen mask. At the
end of the world render the mask is dilated into a ring and blended over the frame, before the UI.
The addon pushes settings through `UnitOutline.Config`, which the DLL registers on the first world
render.

Addresses are for 1.12.1.5875 only. MinHook is BSD-2-Clause (Tsuda Kageyu).
