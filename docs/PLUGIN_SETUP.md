# CampfireTogether ESPFE bridge setup

Campfire's supported `Campfire_OnObjectPlaced` and `Campfire_OnObjectRemoved` events are multi-argument SKSE ModEvents. CampfireTogether uses a tiny Start Game Enabled Papyrus quest to receive those events and forward them to the native SKSE plugin.

## No Creation Kit step is required

`CampfireTogether.esp` is generated from the version-controlled Spriggit source under `plugin/CampfireTogether`.

The plugin contains one Start Game Enabled quest:

- EditorID: `CFT_BridgeQuest`
- FormID: `0x800`
- attached script: `CampfireTogetherBridge`

The bridge quest contains no direct Campfire form references, so the ESP does not technically need `Campfire.esm` as a master. Campfire remains a runtime requirement because it emits the ModEvents and provides the scripts used by the compatibility layer.

## Papyrus compatibility overrides

v0.2.0 ships the same two targeted Campfire overrides validated in v0.1.4:

- `_Camp_SpawnCampfire.pex` gates Campfire's actual `Build Campfire` magic-effect path so a remotely replicated cast cannot start placement;
- `_Camp_CampTentNPCBedrollScript.pex` permits the local player to use a follower/spare bedroll only when its parent tent is a tracked remote CampfireTogether tent.

`CampConjureObjectEffect.pex` is not overridden.

CampfireTogether must win the two PEX conflicts above in the mod manager. Compile-only declarations under `Scripts/CompileStubs` are never packaged.

## Remote teardown

Materialized remote tent/campfire roots are torn down through Campfire's own Papyrus methods:

- `CampTent.TakeDown()` for tents;
- `CampCampfire.TakeDown()` for campfires.

This lets Campfire remove child references such as shelter meshes, bedrolls, lanterns/lights and campfire child effects. The removal ModEvent produced by that teardown is suppressed so it is not echoed back through STRPM.

## v0.2.0 authoritative state

The DLL keeps an authoritative registry only for camps placed by the local player. Each entry contains:

- stable CFT event ID;
- base plugin + local FormID;
- parent CELL plugin + local FormID;
- position/rotation;
- tent/campfire flag.

The local registry is persisted with the SKSE serialization interface under unique ID `CFT2`.

Remote authoritative state is received through protocol-v3 snapshots. A remote state entry can exist without a runtime mirror while its recorded CELL is unloaded. `TESCellFullyLoadedEvent` triggers a retry when that CELL is loaded. This prevents persistent snapshots from creating old camps in the sender proxy's current cell.

Remote mirrors themselves are not authoritative co-save state and are rebuilt from the owning player's snapshot.

## Release build

`build/_release.bat` performs the complete build:

1. configure CMake/vcpkg;
2. build `CampfireTogether.dll`;
3. compile the native bridge, event bridge and the two Campfire compatibility overrides;
4. deserialize the Spriggit source into `CampfireTogether.esp`;
5. package DLL + PEX files + ESP into `dist/CampfireTogether-v0.2.0.zip`.

Expected packaged scripts:

- `CampfireTogetherNative.pex`
- `CampfireTogetherBridge.pex`
- `_Camp_SpawnCampfire.pex`
- `_Camp_CampTentNPCBedrollScript.pex`
