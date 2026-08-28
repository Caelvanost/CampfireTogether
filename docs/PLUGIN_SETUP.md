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

The plugin header is ESL-flagged (`LightMaster`), so the generated `CampfireTogether.esp` is an ESPFE.

The script contains no direct form references to Campfire, so the ESP does not technically need `Campfire.esm` as a master. Campfire remains a runtime requirement because it produces the two ModEvents.

## Release build

`build/_release.bat` performs the complete build:

1. configure CMake/vcpkg;
2. build `CampfireTogether.dll`;
3. compile `CampfireTogetherNative.psc` and `CampfireTogetherBridge.psc` to PEX;
4. restore the repo-local Spriggit 0.40.1 .NET tool and deserialize the YAML source into `CampfireTogether.esp`;
5. package DLL + PEX + ESP into the release ZIP.

Spriggit is pinned in `.config/dotnet-tools.json`, so no global Spriggit installation is required. The first build requires network access so `dotnet tool restore` can fetch the pinned tool and Spriggit translation package.

## Why not patch Campfire scripts?

Patching `_Camp_ObjectPlacementThreadManager.psc` or other Campfire scripts would create conflicts with Campfire updates and other compatibility mods. The public Campfire ModEvent API is the supported integration point and keeps CampfireTogether isolated.
