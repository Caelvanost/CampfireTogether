# Campfire Together

Compatibility plugin for **Campfire – Complete Camping System** and **Skyrim Together Reborn**.

Campfire Together synchronizes Campfire objects created dynamically by players so other Skyrim Together clients can see the same campsite.

## Development status

Current branch target: **v0.1.0 prototype**.

Implemented in the prototype architecture:

- listens to Campfire's supported `Campfire_OnObjectPlaced` and `Campfire_OnObjectRemoved` ModEvents through a minimal Papyrus quest bridge;
- forwards placement/removal packets through STRPluginMessagingAPI v2;
- resolves the sending player's STR proxy through ProxyResolver v1;
- uses that proxy as the cell anchor for remote object creation;
- mirrors position and rotation;
- tracks remote references for deterministic removal and duplicate suppression;
- deletes runtime mirrors on save/new-game state reset.

## Current limitations

- Requires the small `CampfireTogether.esp` listener quest described in `docs/PLUGIN_SETUP.md`.
- v0.1.0 sends runtime base FormIDs and therefore assumes identical load order on both PCs.
- No late-join/reconnect snapshot yet.
- Campfire fuel/burn-state synchronization is not implemented yet.
- Remote copies are initially intended as visual mirrors; functional interaction will be validated separately.

## Target

- Skyrim SE/AE runtime 1.6.1170
- SKSE64
- Address Library
- Campfire
- Skyrim Together Reborn
- STRPluginMessagingAPI

## Build

From PowerShell at the repository root:

```powershell
.\build\_release.bat
```

The script uses `C:\dev\vcpkg` and `C:\Games\Steam\steamapps\common\Skyrim Special Edition` by default, matching the current development environment. Set `VCPKG_ROOT` or `SKYRIM_PATH` to override them.

See `docs/TESTING.md` for the two-player test procedure.
