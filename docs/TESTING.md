# CampfireTogether v0.1.4 test plan

## Scope

v0.1.4 keeps the validated v0.1.3 campfire/tent lifecycle and replaces sender-side runtime base FormIDs with load-order-independent form descriptors: **origin plugin filename + local FormID**.

Still not covered:

- late-join / reconnect snapshots;
- persistence/state reconstruction across cells;
- synchronization of internal campfire burn/fuel state;
- shared authority/revisions for objects modified by both players;
- broader tent state synchronization such as lantern state.

## Installation / file conflicts

Install the same `CampfireTogether-v0.1.4.zip` on both machines.

CampfireTogether intentionally overrides these two Campfire scripts and **must win their file conflicts**:

- `Scripts/_Camp_SpawnCampfire.pex`
- `Scripts/_Camp_CampTentNPCBedrollScript.pex`

`Scripts/CampConjureObjectEffect.pex` is not part of CampfireTogether anymore. If an old manual install left that file behind, remove the stale CampfireTogether copy so Campfire's original script is restored.

Fully quit Skyrim on both machines after installing v0.1.4 before starting the test. Do not replace DLL/PEX files while a game session is running.

v0.1.4 uses protocol v2 and is intentionally incompatible with v0.1.3 network packets. Both clients must run v0.1.4.

## Expected startup log

On both machines, `Documents/My Games/Skyrim Special Edition/SKSE/CampfireTogether.log` should contain:

- `Campfire Together v0.1.4 loading`
- `CFT LOCAL BUILD INTENT input sink READY`
- `CFT PAPYRUS native bridge READY`
- `CFT STRPM READY`
- `CFT PAPYRUS Campfire event listener READY`

If the last line is absent, the ESPFE quest/script bridge is not active.

## Test A — Build Campfire power isolation

1. Connect Player1 and Player2 to the same Skyrim Together session and stand together outside.
2. Player1 equips/uses Campfire's `Build Campfire` power and presses the local Shout/Power key.
3. Player1 should enter the normal Campfire placement flow.
4. Player1 should log `CFT LOCAL BUILD INTENT armed` followed by `CFT LOCAL BUILD INTENT consumed`.
5. Player2 must **not** enter Campfire placement mode and must not see a placement indicator.
6. If STR replicated the magic effect, Player2's CampfireTogether log should contain `CFT REMOTE BUILD CAMPFIRE suppressed`.
7. Finish placing the campfire on Player1.
8. Player1 should log `CFT LOCAL PLACE` then `CFT STRPM TX`.
9. The TX line should identify the base as `plugin:localFormID`, not only as a runtime FormID.
10. Player2 should log `CFT STRPM RX` then `CFT REMOTE PLACE created`.
11. Confirm the campfire root and its normal Campfire visuals appear on Player2.

Repeat with Player2 as the builder.

## Test B — Remote tent and spare bedroll

1. Player1 places a tent that has a spare/follower bedroll.
2. Confirm Player2 sees the complete tent, including its locally initialized Campfire child objects.
3. Player2 activates the spare/follower bedroll.
4. The message `This is a follower's bedroll` must **not** appear for this remote tent.
5. Player2 should be allowed to use that bedroll.
6. Player2's log should contain `CFT REMOTE BEDROLL ACCESS`.
7. Confirm the primary bedroll remains usable normally by Player1.

Control check: if Player2 places their own local tent, Player2 should still receive Campfire's normal follower-bedroll restriction when trying to use that tent's spare bedroll. CampfireTogether must not globally unlock follower beds.

The richer interaction options of the main bedroll are not part of v0.1.4; spare-bedroll feature parity will be handled later.

## Test C — Complete remote tent teardown

1. Player1 dismantles the tent from Test B.
2. Player1 should log `CFT LOCAL REMOVE` with the matching non-zero event ID and send a remove packet.
3. Player2 should receive the packet and log `CFT REMOTE TENT TEARDOWN dispatched`.
4. Campfire's own `CampTent.TakeDown()` should remove the root and all children.
5. Verify the tent mesh, primary bedroll, spare bedroll(s), lantern/light and visible child clutter disappear on Player2.
6. Player2 should log `CFT LOCAL REMOVE suppressed remote teardown` and must not send that removal back to Player1.

## Test D — Complete remote campfire teardown

1. Player1 destroys the campfire.
2. Player2 should log `CFT REMOTE CAMPFIRE TEARDOWN dispatched`.
3. Verify the campfire and all visible child effects/objects disappear on Player2.
4. Player2 should log `CFT LOCAL REMOVE suppressed remote teardown` rather than echoing the removal to Player1.

## Test E — Reverse direction

Repeat Tests A through D with Player2 creating the campfire/tent and Player1 acting as the remote client.

## Test F — Load-order-independent form resolution

This is the new v0.1.4 regression test.

1. Keep the same Campfire content installed on both machines.
2. If practical in your Skyrim Together setup, arrange the load order so the plugin defining a synchronized Campfire object receives a different runtime load index on Player1 and Player2. A harmless plugin placed before it on only one test profile is sufficient to shift a normal full-plugin index.
3. Connect both players and place a campfire or tent.
4. On the sender, note the `CFT LOCAL PLACE` / `CFT STRPM TX` descriptor. It should look like `PluginName.esm:00ABCDEF` (the exact plugin and local ID depend on the object).
5. On the receiver, `CFT STRPM RX` must show the **same plugin + local FormID**.
6. `CFT REMOTE PLACE created` may show a different `runtimeBase=` value from the sender. That difference is expected and is the point of the test.
7. Remove the object and verify the remote teardown still succeeds.
8. There must be no `baseFound=0` or `no mirror match` error for the tested object.

If changing load order is undesirable, the normal tests still verify that protocol v2 is being used; Test F is the direct proof that the new descriptor survives differing runtime indices.

## Important

CampfireTogether v0.1.4 no longer requires identical runtime load indices for synchronized base forms. It does still require the receiving client to have the plugin named in the packet and a compatible record at the same local FormID.

Keep both `CampfireTogether.log` files after the test. For v0.1.4, the useful fields are `base=PluginName:LocalFormID` and `runtimeBase=`; comparing those between clients makes load-order resolution failures immediately visible.
