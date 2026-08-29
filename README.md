# Campfire Together

Compatibility plugin for **Campfire – Complete Camping System** and **Skyrim Together Reborn**.

Campfire Together synchronizes Campfire objects created dynamically by players so other Skyrim Together clients can see and increasingly interact with the same campsite.

## Development status

Current branch target: **v0.2.0 prototype**.

Implemented:

- listens to Campfire's supported `Campfire_OnObjectPlaced` and `Campfire_OnObjectRemoved` ModEvents through a minimal Papyrus quest bridge;
- forwards placement/removal packets through STRPluginMessagingAPI v2;
- resolves the sending player's STR proxy through ProxyResolver v1;
- serializes base forms as **origin plugin filename + local FormID**, then resolves them against the receiving client's own load order;
- serializes the owning **CELL** the same way, so persistent state keeps the correct interior/exterior cell instead of using the sender proxy's current cell as a generic spawn anchor;
- tracks local camp ownership as authoritative CFT state rather than only as one-shot network events;
- persists that local ownership registry in the SKSE co-save;
- exchanges complete snapshots when a peer becomes available and after save-load/new-game transitions;
- stores remote authoritative state even when its target cell is currently unloaded;
- materializes remote mirrors only when their recorded target cell is attached/loaded, using `TESCellFullyLoadedEvent` to retry pending state after travel;
- reconciles snapshots with `BEGIN -> PLACE entries -> END`, removing stale remote state/mirrors that are no longer present in the owner's authoritative state;
- tears down remote state when a STR peer disappears so reconnects can rebuild cleanly;
- tracks materialized remote root references for deterministic removal and duplicate suppression;
- invokes Campfire's own `CampTent.TakeDown()` / `CampCampfire.TakeDown()` for remote teardown so Campfire can clean up its child objects;
- suppresses the removal ModEvent produced by a remote teardown so it is not echoed back over STRPM;
- detects a real local Skyrim `Shout`/Power input in the DLL and requires that short-lived intent before `Build Campfire` can start placement;
- gates the actual `_Camp_CampfireSpell` effect path (`_Camp_Indicator_CampfireEffect` -> `_Camp_SpawnCampfire`) so a remotely replicated `Build Campfire` cast cannot start a second local placement flow;
- allows the local player to use a follower/spare bedroll only when that bedroll belongs to a remote CampfireTogether tent.

## v0.2.0 authoritative state and snapshots

Protocol v3 adds explicit snapshot control packets:

1. `SNAPSHOT_REQUEST`
2. `SNAPSHOT_BEGIN`
3. zero or more snapshot `PLACE` state entries
4. `SNAPSHOT_END`

Each locally owned campfire/tent keeps a stable CFT event ID plus:

- base plugin filename + local FormID;
- cell plugin filename + local FormID;
- position and rotation;
- tent/campfire flag.

That registry is written into the SKSE co-save.

When another STR peer appears, both sides proactively exchange their current state. A player who joins after a camp was already placed can therefore receive that camp without the owner placing it again.

At snapshot end, remote events that existed before the snapshot but were not present in the owner's new authoritative state are removed. This recovers from missed REMOVE packets and reconnects.

## Cell-aware materialization

A snapshot can describe camps in several cells. CampfireTogether therefore separates:

- **remote state** — the authoritative description received from the owner;
- **remote mirror** — the runtime Campfire object currently instantiated on this client.

Receiving a snapshot does not blindly call `PlaceObjectAtMe` from the sender's current proxy cell. CFT first resolves the recorded CELL. If that cell is not attached, the state remains pending without creating an object in the wrong cell.

When the CELL becomes fully loaded, the `TESCellFullyLoadedEvent` listener retries pending state for that exact cell. CFT prefers the local player or sender proxy as a placement anchor when either is already in the target cell; otherwise it can use an existing reference from the attached cell. The mirror is then moved to the authoritative position/rotation.

This is the basis for travel/streaming reconstruction: a camp can remain in authoritative multiplayer state while its cell is unloaded and reappear when that cell is loaded again.

## SKSE co-save persistence

Only **locally owned** camp state is persisted. Remote mirrors are never treated as authoritative save state; they are reconstructed from the owning player's snapshot.

On save, CFT writes the active local ownership registry to the SKSE co-save. On load, it restores that registry, clears stale remote session tracking, broadcasts the restored local snapshot and requests current snapshots from connected peers.

### Upgrade note

Camps that already existed in a save **before v0.2.0 first tracked them** do not have a CampfireTogether ownership record and cannot yet be auto-adopted. New campfires/tents placed while v0.2.0 is installed are tracked and persist across subsequent saves/loads.

## Form identity / load order

CampfireTogether does not send sender-side runtime FormIDs such as `0x14031DAF`. It sends defining plugin + local FormID descriptors, for example `Campfire.esm:0031DAF`.

The receiver resolves those descriptors with its own `TESDataHandler`, so base objects and cells may have different runtime load indices on Player1 and Player2 without breaking CFT state reconstruction.

Both machines still need compatible content plugins with the same filenames and local records.

## Campfire script overrides

v0.2.0 intentionally ships the same two targeted Campfire script overrides already validated in v0.1.4:

- `Scripts/_Camp_SpawnCampfire.pex` — allows the real `Build Campfire` placement effect only when the DLL consumes a recent local `Shout`/Power input intent;
- `Scripts/_Camp_CampTentNPCBedrollScript.pex` — allows the local player to use a spare/follower bedroll when its parent tent is a tracked remote CampfireTogether tent.

`CampConjureObjectEffect.pex` is **not** overridden.

**CampfireTogether must win these two file conflicts against Campfire in the mod manager.** Other mods that replace either script may need a compatibility patch.

The source copies are kept under `Scripts/Overrides`. Compile-only API stubs live under `Scripts/CompileStubs`; those stubs are never packaged and the real Campfire scripts are used at runtime.

## Current limitations

- Requires the small `CampfireTogether.esp` listener quest described in `docs/PLUGIN_SETUP.md`.
- Protocol v3 requires v0.2.0 on both clients; v0.1.x packets are intentionally incompatible.
- Pre-v0.2.0 camps are not automatically discovered/adopted from an old save.
- v0.2.0 reconstructs root campfire/tent state; Campfire fuel, burn duration, lit/unlit state and similar internal campfire state are not synchronized yet.
- Broader tent state such as lantern state is not synchronized yet.
- Shared ownership/revision conflict handling for objects modified by multiple players is not implemented yet.
- Remote spare-bedroll interaction remains an early compatibility feature; richer main-bedroll-style interaction options come later.

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

The release build produces:

- `CampfireTogether.dll`;
- `CampfireTogetherNative.pex`;
- `CampfireTogetherBridge.pex`;
- `_Camp_SpawnCampfire.pex`;
- `_Camp_CampTentNPCBedrollScript.pex`;
- `CampfireTogether.esp` (ESPFE, generated by Spriggit);
- `dist/CampfireTogether-v0.2.0.zip`.

The script uses `C:\dev\vcpkg` and `C:\Games\Steam\steamapps\common\Skyrim Special Edition` by default, matching the current development environment. Set `VCPKG_ROOT` or `SKYRIM_PATH` to override them.

See `docs/TESTING.md` for the two-player test procedure.
