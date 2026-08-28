# CampfireTogether ESPFE bridge setup

Campfire's supported `Campfire_OnObjectPlaced` and `Campfire_OnObjectRemoved` events are multi-argument SKSE ModEvents. A native SKSE plugin cannot consume those arguments through the simple `SKSE::ModCallbackEvent` sink, so CampfireTogether uses a tiny Papyrus listener quest as a bridge.

## Create `CampfireTogether.esp`

Create a new ESP in Creation Kit or xEdit and flag it ESL (ESPFE).

1. Create a Quest named `CFT_BridgeQuest`.
2. Enable **Start Game Enabled**.
3. Attach the compiled `CampfireTogetherBridge` script to the quest.
4. Save the plugin as `CampfireTogether.esp`.
5. Put the resulting file at `plugin/CampfireTogether.esp` in the repository.

The script contains no direct form references to Campfire, so the ESP does not technically need Campfire.esm as a master. Campfire remains a runtime requirement because it is the producer of the two ModEvents.

`build/_release.bat` compiles both Papyrus scripts and automatically copies `plugin/CampfireTogether.esp` into the release package when the file exists.

## Why not patch Campfire scripts?

Patching `_Camp_ObjectPlacementThreadManager.psc` or other Campfire scripts would create conflicts with Campfire updates and other compatibility mods. The public Campfire ModEvent API is the supported integration point and keeps CampfireTogether isolated.
