# Campfire Together

Compatibility plugin for **Campfire – Complete Camping System** and **Skyrim Together Reborn**.

The project aims to synchronize Campfire objects placed dynamically by players (campfires, tents and other placeable camping objects) so that every player in the Skyrim Together party can see the same camp.

## Status

Early development.

## Planned architecture

- Campfire placement/removal events are captured through a small Papyrus bridge.
- CampfireTogether forwards normalized object data through STRPluginMessagingAPI.
- Remote clients create local mirror references for visual synchronization.
- The placing player remains authoritative for the camp object.
- Remote mirror references are tracked to prevent feedback loops and duplicate spawning.

## Target

- Skyrim Special Edition / Anniversary Edition runtime 1.6.1170
- SKSE64
- Address Library
- Campfire
- Skyrim Together Reborn
- STRPluginMessagingAPI
