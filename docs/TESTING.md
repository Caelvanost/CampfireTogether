# CampfireTogether v0.2.0 test plan

## Scope

v0.2.0 keeps the validated v0.1.4 lifecycle and adds:

- authoritative local camp state;
- plugin/local FormID identity for both base object and CELL;
- SKSE co-save persistence;
- late-join/reconnect snapshots;
- stale-state reconciliation;
- pending remote state for unloaded cells;
- materialization when the recorded CELL becomes fully loaded.

Still not covered:

- automatic adoption of camps that existed before v0.2.0 first tracked them;
- campfire fuel/burn/lit-state synchronization;
- broader tent state such as lantern state;
- shared ownership/revision conflict handling.

## Installation

Install the same `CampfireTogether-v0.2.0.zip` on both machines.

CampfireTogether must win these two Campfire script conflicts:

- `Scripts/_Camp_SpawnCampfire.pex`
- `Scripts/_Camp_CampTentNPCBedrollScript.pex`

Fully quit Skyrim on both machines after installation. v0.2.0 uses protocol v3 and is intentionally incompatible with v0.1.x, so both clients must run v0.2.0.

## Expected startup log

Both logs should contain:

- `Campfire Together v0.2.0 loading`
- `CFT SERIALIZATION READY id=CFT2`
- `CFT LOCAL BUILD INTENT input sink READY`
- `CFT CELL TRACKER READY event=TESCellFullyLoadedEvent`
- `CFT PAPYRUS native bridge READY`
- `CFT STRPM READY ... proxyListener=1`
- `CFT PAPYRUS Campfire event listener READY`

If `proxyListener=0`, reconnect/late-join handling is not considered validated.

## Test A — v0.1.4 lifecycle regression

1. Connect P1 and P2 normally and stand together outside.
2. P1 uses `Build Campfire`.
3. P1 enters placement; P2 must not.
4. P1 logs `CFT LOCAL BUILD INTENT armed` then `consumed`.
5. P2 logs `CFT REMOTE BUILD CAMPFIRE suppressed` if STR replicated the magic effect.
6. P1 finishes the campfire. P2 must see it.
7. P1 places a tent. P2 must see it.
8. P2 may use the remote spare/follower bedroll as already validated in v0.1.4.
9. P1 packs up the tent. It disappears on P2.
10. P1 destroys the campfire. It disappears on P2.
11. Remote teardown must still produce `CFT LOCAL REMOVE suppressed remote teardown`, not an echo loop.
12. Repeat P2 -> P1.

New v0.2.0 PLACE logs should include both descriptors, for example:

```text
base=Campfire.esm:00031DAF cell=Skyrim.esm:XXXXXXXX
```

## Test B — late join in the same cell

1. Start P1 in a Skyrim Together session without P2 connected.
2. P1 places one campfire and one tent.
3. Confirm two `CFT LOCAL PLACE` entries with non-zero event IDs.
4. Do not touch the objects again.
5. Connect P2 to the existing session while P1 remains near the camp.
6. Logs should show the peer/snapshot exchange:
   - `CFT STRPM PROXY added`
   - `CFT PEER available`
   - `CFT SNAPSHOT REQUEST`
   - `CFT SNAPSHOT TX complete ... objects=2`
7. P2 should log:
   - `CFT SNAPSHOT RX begin`
   - two `CFT REMOTE STATE stored` lines with non-zero `snapshot=` IDs;
   - two `CFT REMOTE PLACE created` lines;
   - `CFT SNAPSHOT RX complete`.
8. The existing campfire and tent must appear without P1 replacing them.
9. There must be exactly one copy of each.

## Test C — disconnect / reconnect

1. Keep the two P1-owned objects active.
2. Disconnect P2 from Skyrim Together while Skyrim remains running.
3. P2 should receive a proxy `removed`/`cleared` event and clear P1 remote state/mirrors.
4. Reconnect P2 without touching the camp on P1.
5. A new snapshot exchange should occur automatically.
6. Both objects must reappear once, with no duplicates.

## Test D — removal while peer is away

1. Disconnect P2.
2. While P2 is away, P1 packs up the tent and destroys the campfire.
3. Reconnect P2.
4. P1 should send `CFT SNAPSHOT TX complete ... objects=0`.
5. Neither object may reappear on P2.

## Test E — SKSE co-save persistence

Use camps newly placed while v0.2.0 is installed.

1. P1 places one campfire and one tent.
2. Create a new manual save after both are fully placed.
3. P1 should log `CFT STATE SAVE objects=2`.
4. Fully quit Skyrim on P1.
5. Restart Skyrim and load that exact save.
6. P1 should log `CFT STATE LOAD objects=2` with a non-zero `nextEvent`.
7. Connect P2 after the load.
8. Without touching the camp on P1, P2 must receive both objects through a snapshot.
9. Pack up the tent and destroy the campfire after the load.
10. The REMOVE operations should still use the restored non-zero event IDs and disappear correctly on P2.
11. Save again after both removals; P1 should log `CFT STATE SAVE objects=0`.

## Test F — cell-aware pending state and return

This validates the important v0.2.0 cell-streaming behavior.

1. P1 and P2 are connected in exterior/interior **CELL A**.
2. P1 places a campfire and tent in CELL A and both players confirm them.
3. Note the `cell=PluginName:LocalFormID` descriptor in the logs.
4. Move both players far enough away or through a transition so CELL A unloads and enter **CELL B**.
5. Do not dismantle the camp in CELL A.
6. Trigger a reconnect or state exchange while the players are in CELL B (disconnect/reconnect P2 is sufficient).
7. P2 should receive the authoritative P1 state, but CFT must **not** create the CELL A camp in CELL B.
8. Expected P2 log for the two objects while A is unloaded:
   - `CFT REMOTE STATE stored ... cell=<CELL A>`
   - `CFT REMOTE MATERIALIZE pending unloaded cell ...`
9. Return both players to CELL A.
10. When A finishes loading, P2 should log:
    - `CFT CELL loaded cell=<CELL A> pendingState=2`
    - `CFT REMOTE PLACE created` for the pending campfire/tent if their prior runtime mirrors did not survive streaming.
11. The campfire and tent must appear at their original positions in CELL A, never in CELL B.
12. There must be no duplicate copy after returning.

## Test G — multiple cells in one snapshot

1. Under v0.2.0, leave one active camp in CELL A.
2. Travel to CELL B and place a second camp there.
3. Save and reload P1 so `CFT STATE LOAD` restores both camps.
4. Connect/reconnect P2 while in CELL B.
5. P2 receives a snapshot containing state from both cells.
6. The CELL B camp may materialize immediately.
7. The CELL A camp should remain pending while A is unloaded.
8. Travel back to CELL A; its camp should materialize there.
9. No camp may be instantiated into the wrong cell simply because the sender proxy currently occupies another cell.

## Test H — stale-state reconciliation

1. Begin with at least one P1-owned remote state known by P2.
2. Cause P2 to miss its REMOVE, if practical.
3. Let P2 receive a later full snapshot from P1 that no longer contains that event ID.
4. At `SNAPSHOT_END`, the baseline event absent from `seenEvents` should be removed.
5. Expected log includes non-zero `staleStateRemoved` and, if currently materialized, non-zero `staleMirrorsRemoved`.
6. Materialized stale objects must use Campfire's normal `TakeDown()` cleanup path.

If disconnect cleanup already erased all remote state, `baseline=0` / zero stale counts are also valid.

## Test I — reverse ownership

Repeat late join, reconnect, save/load, and cell-return tests with P2 as owner and P1 as receiver.

Each player persists only the camps they personally placed.

## Upgrade limitation

A campfire or tent already present in a save before v0.2.0 ever tracked it has no CFT ownership entry. Loading such an older save may therefore show the local Campfire object normally while `CFT STATE LOAD objects=0` is reported.

That is expected for v0.2.0. Use newly placed camps for persistence tests.

## Useful v0.2.0 log markers

- `CFT SERIALIZATION READY`
- `CFT STATE SAVE`
- `CFT STATE LOAD`
- `CFT CELL TRACKER READY`
- `CFT CELL loaded`
- `CFT STRPM PROXY added/updated/removed`
- `CFT PEER available/unavailable`
- `CFT SNAPSHOT REQUEST`
- `CFT SNAPSHOT TX complete`
- `CFT SNAPSHOT RX begin`
- `CFT SNAPSHOT RX complete`
- `CFT REMOTE STATE stored`
- `CFT REMOTE MATERIALIZE pending unloaded cell`
- `CFT REMOTE PLACE created`
- `CFT LOCAL REMOVE suppressed remote teardown`

Keep both `CampfireTogether.log` files after each test.
