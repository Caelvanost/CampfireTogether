# CampfireTogether ESPFE bridge setup

Campfire's supported `Campfire_OnObjectPlaced` and `Campfire_OnObjectRemoved` events are multi-argument SKSE ModEvents. CampfireTogether therefore uses a tiny Start Game Enabled Papyrus quest to receive those events and forward them to the native SKSE plugin.

## No Creation Kit step is required

`CampfireTogether.esp` is generated from the version-controlled Spriggit YAML source under:

- `plugin/CampfireTogether/RecordData.yaml`
- `plugin/CampfireTogether/spriggit-meta.json`
- `plugin/CampfireTogether/Quests/`

The plugin contains one quest:

- EditorID: `CFT_BridgeQuest`
- FormID: `0x800`
- Flag: `StartGameEnabled`
- Attached script: `CampfireTogetherBridge`

The Skyrim plugin header uses Mutagen/Spriggit's `Small` flag (`0x00000200`), so the generated `CampfireTogether.esp` is an ESPFE.

The bridge quest contains no direct form references to Campfire, so the ESP does not technically need `Campfire.esm` as a master. Campfire remains a runtime requirement because it produces the ModEvents and provides the runtime scripts used by the compatibility layer.

## Papyrus compatibility overrides in v0.1.1

The placement/removal bridge still uses Campfire's public ModEvent API and does not replace the core placement system. v0.1.1 does, however, intentionally override two narrowly scoped Campfire scripts to handle STR-specific behavior that Campfire's public API does not expose:

- `CampConjureObjectEffect.pex` prevents a remotely replicated Build Campfire magic effect from launching a second placement flow on the receiving player's machine;
- `_Camp_CampTentNPCBedrollScript.pex` permits the local player to use a follower/spare bedroll only when its parent tent is a remote root tracked by CampfireTogether.

The source copies live under `Scripts/Overrides`. `Scripts/CompileStubs` contains only declarations needed to compile those overrides without copying the entire Campfire source tree into this repository. Compile stubs are never packaged.

At runtime the real Campfire scripts provide `CampUtil`, `TentSystem`, `CampTent`, `_CampInternal`, and the rest of Campfire's implementation.

CampfireTogether must win the two PEX file conflicts above in the mod manager.

## Remote teardown

Remote tent/campfire roots are not deleted blindly. The SKSE plugin resolves the bound Papyrus object and dispatches its native Campfire `TakeDown()` method:

- `CampTent.TakeDown()` for tents;
- `CampCampfire.TakeDown()` for campfires.

This lets Campfire remove its own child references such as shelter meshes, lanterns, lights, bedrolls, fuel, embers and triggers. The removal ModEvent emitted by that remote teardown is temporarily suppressed so it is not echoed back through STRPM.

If the expected Campfire script is not bound, CampfireTogether falls back to generic reference deletion.

## Release build

`build/_release.bat` performs the complete build:

1. configure CMake/vcpkg;
2. build `CampfireTogether.dll`;
3. compile the two bridge scripts and the two Campfire compatibility overrides to PEX;
4. restore the repo-local Spriggit 0.40.1 .NET tool and deserialize the YAML source into `CampfireTogether.esp`;
5. package DLL + four PEX + ESP into the release ZIP.

Expected packaged scripts for v0.1.1:

- `CampfireTogetherNative.pex`
- `CampfireTogetherBridge.pex`
- `CampConjureObjectEffect.pex`
- `_Camp_CampTentNPCBedrollScript.pex`

Spriggit is pinned in `.config/dotnet-tools.json`, so no global Spriggit installation is required. The first build requires network access so `dotnet tool restore` can fetch the pinned tool and Spriggit translation package.
