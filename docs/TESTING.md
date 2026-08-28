# CampfireTogether v0.1.0 test plan

## Scope

The first prototype targets live visual placement/removal synchronization while both players are connected and in the same area.

Not yet covered:

- late-join / reconnect snapshots;
- persistence across different cells;
- full functional ownership of remote tents/campfires;
- load-order-independent base-form serialization;
- synchronization of internal campfire burn/fuel state.

## Expected startup log

On both machines, `Documents/My Games/Skyrim Special Edition/SKSE/CampfireTogether.log` should contain:

- `Campfire Together v0.1.0 loading`
- `CFT PAPYRUS native bridge READY`
- `CFT STRPM READY`
- `CFT PAPYRUS Campfire event listener READY`

If the last line is absent, the ESPFE quest/script bridge is not active.

## Placement test

1. Connect Player1 and Player2 to the same Skyrim Together session.
2. Stand in the same exterior cell and confirm each player can see the other.
3. Player1 places one Campfire tent.
4. Player1 log should show `CFT LOCAL PLACE` then `CFT STRPM TX`.
5. Player2 log should show `CFT STRPM RX` then `CFT REMOTE PLACE created`.
6. Confirm the tent is visible on Player2.
7. Repeat in the opposite direction.

Then repeat with a campfire.

## Removal test

1. Pick up/remove the object on the player who originally placed it.
2. Owner log should show `CFT LOCAL REMOVE` with the same non-zero event ID when matching succeeds.
3. Remote log should show `CFT REMOTE REMOVE deleted`.
4. Confirm the remote mirror disappears.

## Important

This prototype sends the runtime base FormID. Both machines must therefore use the same mod list and load order for the test. A plugin-name + local-FormID descriptor will replace this before a public release.
