Scriptname CampfireTogetherRemoteBridge extends Quest

Event OnInit()
    RegisterRemoteMaterializationEvent()
EndEvent

Function RegisterRemoteMaterializationEvent()
    UnregisterForModEvent("CFT_RemoteMaterialize")
    RegisterForModEvent("CFT_RemoteMaterialize", "CFT_RemoteMaterialize")
EndFunction

Event CFT_RemoteMaterialize(String asEventName, String asStringArg, Float afRequestID, Form akBaseObject)
    Int requestID = afRequestID as Int
    If requestID <= 0 || !akBaseObject || !CampfireTogetherNative.IsRemoteMaterializationRequestValid(requestID)
        Return
    EndIf

    Static xMarkerBase = Game.GetFormFromFile(0x0000003B, "Skyrim.esm") as Static
    ObjectReference playerRef = Game.GetPlayer()
    If !xMarkerBase || !playerRef
        CampfireTogetherNative.ReportRemoteMaterializationFailed(requestID)
        Return
    EndIf

    ; Follow Campfire's own placement strategy. Do NOT force persistence here:
    ; a forced-persistent exterior reference is owned by the worldspace persistent
    ; cell instead of the real loaded grid cell, which is exactly what CFT must avoid.
    ObjectReference spawnMarker = playerRef.PlaceAtMe(xMarkerBase, 1, False, False)
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

    spawnMarker.SetPosition(targetX, targetY, targetZ)
    spawnMarker.SetAngle(targetAngleX, targetAngleY, targetAngleZ)

    ; Spawn from the relocated marker, also without ForcePersist. This makes Skyrim
    ; attach the new reference to the marker's actual exterior grid cell.
    ObjectReference campItem = spawnMarker.PlaceAtMe(akBaseObject, 1, False, False)

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
