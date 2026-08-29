# CampfireTogether v0.1.3 test plan

## Scope

v0.1.3 validates the full create/use/remove lifecycle for the first two important Campfire roots: campfires and tents, plus the corrected local-input gate on Campfire's actual `Build Campfire` effect path.

Still not covered:

- late-join / reconnect snapshots;
- persistence across different cells;
- load-order-independent base-form serialization;
- synchronization of internal campfire burn/fuel state;
- shared authority/revisions for objects modified by both players;
- broader tent state synchronization such as lantern state.

## Installation / file conflicts

Install the same `CampfireTogether-v0.1.3.zip` on both machines.

CampfireTogether intentionally overrides these two Campfire scripts and **must win their file conflicts**:

- `Scripts/_Camp_SpawnCampfire.pex`
- `Scripts/_Camp_CampTentNPCBedrollScript.pex`

`Scripts/CampConjureObjectEffect.pex` is no longer part of CampfireTogether v0.1.3. If the previous v0.1.2 was installed manually rather than through a mod manager, remove the stale CampfireTogether copy before testing so Campfire's original script is restored.

Fully quit Skyrim on both machines after installing v0.1.3 before starting the test. Do not test by replacing the PEX while a game session is already running.

## Expected startup log

On both machines, `Documents/My Games/Skyrim Special Edition/SKSE/CampfireTogether.log` should contain:

- `Campfire Together v0.1.3 loading`
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
7. Player2 should not log `CFT LOCAL BUILD INTENT armed` or `CFT LOCAL BUILD INTENT consumed` from Player1's remote cast.
8. Finish placing the campfire on Player1.
9. Player1 should log `CFT LOCAL PLACE` then `CFT STRPM TX`.
10. Player2 should log `CFT STRPM RX` then `CFT REMOTE PLACE created`.
11. Confirm the campfire root and its normal Campfire visuals appear on Player2.

Repeat with Player2 as the builder.

### What v0.1.3 specifically proves

The installed `Campfire.esm` maps `_Camp_CampfireSpell` to `_Camp_Indicator_CampfireEffect`, whose VMAD script is `_Camp_SpawnCampfire`. v0.1.3 gates that script directly. Therefore a successful Test A should show the local client consuming the intent at the beginning of `_Camp_SpawnCampfire.OnEffectStart`, while the remote client returns before `CampPlacementIndicator.Ready()` is called.

## Test B — Remote tent and spare bedroll

1. Player1 places a tent that has a spare/follower bedroll.
2. Confirm Player2 sees the complete tent, including its locally initialized Campfire child objects.
3. Player2 activates the spare/follower bedroll.
4. The message `This is a follower's bedroll` must **not** appear for this remote tent.
5. Player2 should be allowed to enter/use that bedroll normally.
6. Player2's log should contain `CFT REMOTE BEDROLL ACCESS`.
7. Confirm the primary bedroll remains usable normally by Player1.

Control check: if Player2 places their own local tent, Player2 should still receive Campfire's normal follower-bedroll restriction when trying to use that tent's spare bedroll. CampfireTogether must not globally unlock follower beds.

## Test C — Complete remote tent teardown

1. Player1 dismantles the tent from Test B.
2. Player1 should log `CFT LOCAL REMOVE` with the matching non-zero event ID and send a remove packet.
3. Player2 should receive the packet and log `CFT REMOTE TENT TEARDOWN dispatched`.
4. Campfire's own `CampTent.TakeDown()` should remove the root and all children.
5. Verify **all** of these disappear on Player2:
   - tent/shelter mesh;
   - primary bedroll;
   - spare/follower bedroll(s);
   - lantern model;
   - lantern light;
   - any visible child clutter.
6. When Campfire emits its local removal callback as a result of the remote `TakeDown()`, Player2 should log `CFT LOCAL REMOVE suppressed remote teardown` and must not send that removal back to Player1.

## Test D — Complete remote campfire teardown

1. Player1 destroys the campfire.
2. Player2 should log `CFT REMOTE CAMPFIRE TEARDOWN dispatched`.
3. Verify the campfire and all visible child effects/objects disappear on Player2.
4. Player2 should log `CFT LOCAL REMOVE suppressed remote teardown` rather than echoing the removal to Player1.

## Test E — Reverse direction

Repeat Tests A through D with Player2 creating the campfire/tent and Player1 acting as the remote client.

## Important

v0.1.3 still sends runtime base FormIDs. Both machines must therefore use the same mod list and load order for the test. A plugin-name + local-FormID descriptor will replace this before a public release.

Keep both `CampfireTogether.log` files after the test even if everything appears correct; the lifecycle logs are useful for verifying that remote teardown does not echo packets back and that the corrected `_Camp_SpawnCampfire` guard is being exercised.
