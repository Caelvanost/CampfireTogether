Scriptname CampfireTogetherBridge extends Quest

Event OnInit()
    RegisterCampfireEvents()
    RegisterRemoteMaterializationEvent()
    CampfireTogetherNative.BridgeReady()
EndEvent

Function RegisterCampfireEvents()
    UnregisterForModEvent("Campfire_OnObjectPlaced")
    UnregisterForModEvent("Campfire_OnObjectRemoved")

    RegisterForModEvent("Campfire_OnObjectPlaced", "Campfire_OnObjectPlaced")
    RegisterForModEvent("Campfire_OnObjectRemoved", "Campfire_OnObjectRemoved")
EndFunction

Function RegisterRemoteMaterializationEvent()
    UnregisterForModEvent("CFT_RemoteMaterialize")
    RegisterForModEvent("CFT_RemoteMaterialize", "CFT_RemoteMaterialize")
EndFunction

Event Campfire_OnObjectPlaced(Form akPlacedObject, float afPositionX, float afPositionY, float afPositionZ, float afAngleX, float afAngleY, float afAngleZ, bool abIsTent)
    ObjectReference placedRef = akPlacedObject as ObjectReference
    If placedRef
        CampfireTogetherNative.ReportPlaced(placedRef, afPositionX, afPositionY, afPositionZ, afAngleX, afAngleY, afAngleZ, abIsTent)
    EndIf
EndEvent

Event Campfire_OnObjectRemoved(Form akBaseObject, float afPositionX, float afPositionY, float afPositionZ, float afAngleX, float afAngleY, float afAngleZ, bool abIsTent)
    If akBaseObject
        CampfireTogetherNative.ReportRemoved(akBaseObject, afPositionX, afPositionY, afPositionZ, afAngleX, afAngleY, afAngleZ, abIsTent)
    EndIf
EndEvent

Event CFT_RemoteMaterialize(String asEventName, String asStringArg, Float afRequestID, Form akBaseObject)
    Int requestID = afRequestID as Int
    If requestID <= 0 || !akBaseObject || !CampfireTogetherNative.IsRemoteMaterializationRequestValid(requestID)
        Return
    EndIf

    Static xMarkerBase = Game.GetFormFromFile(0x0000003B, "Skyrim.esm") as Static
    If !xMarkerBase
        CampfireTogetherNative.ReportRemoteMaterializationFailed(requestID)
        Return
    EndIf

    ObjectReference playerRef = Game.GetPlayer()
    ObjectReference spawnMarker = playerRef.PlaceAtMe(xMarkerBase, 1, True, False)
    If !spawnMarker
        CampfireTogetherNative.ReportRemoteMaterializationFailed(requestID)
        Return
    EndIf

    Int markerTry = 0
    While !spawnMarker.Is3DLoaded() && markerTry < 100
        Utility.Wait(0.01)
        markerTry += 1
    EndWhile

    If !spawnMarker.Is3DLoaded()
        spawnMarker.Disable()
        spawnMarker.Delete()
        CampfireTogetherNative.ReportRemoteMaterializationFailed(requestID)
        Return
    EndIf

    Float targetX = CampfireTogetherNative.GetRemoteMaterializationX(requestID)
    Float targetY = CampfireTogetherNative.GetRemoteMaterializationY(requestID)
    Float targetZ = CampfireTogetherNative.GetRemoteMaterializationZ(requestID)
    Float targetAngleX = CampfireTogetherNative.GetRemoteMaterializationAngleX(requestID)
    Float targetAngleY = CampfireTogetherNative.GetRemoteMaterializationAngleY(requestID)
    Float targetAngleZ = CampfireTogetherNative.GetRemoteMaterializationAngleZ(requestID)

    ; Match Campfire's own placement strategy: move an XMarker first, then spawn from it.
    ; Papyrus SetPosition performs the engine-side cell migration that a raw C++
    ; coordinate write does not reliably reproduce for already-loaded exterior cells.
    spawnMarker.SetPosition(targetX, targetY, targetZ)
    spawnMarker.SetAngle(targetAngleX, targetAngleY, targetAngleZ)

    ObjectReference campItem = spawnMarker.PlaceAtMe(akBaseObject, 1, True, False)

    spawnMarker.Disable()
    spawnMarker.Delete()
    spawnMarker = None

    If !campItem
        CampfireTogetherNative.ReportRemoteMaterializationFailed(requestID)
        Return
    EndIf

    campItem.SetPosition(targetX, targetY, targetZ)
    campItem.SetAngle(targetAngleX, targetAngleY, targetAngleZ)

    Int itemTry = 0
    While !campItem.Is3DLoaded() && itemTry < 100
        Utility.Wait(0.01)
        itemTry += 1
    EndWhile

    CampfireTogetherNative.ReportRemoteMaterialized(requestID, campItem)
EndEvent
