Scriptname CampfireTogetherBridge extends Quest

Event OnInit()
    RegisterCampfireEvents()
    CampfireTogetherNative.BridgeReady()
EndEvent

Function RegisterCampfireEvents()
    UnregisterForModEvent("Campfire_OnObjectPlaced")
    UnregisterForModEvent("Campfire_OnObjectRemoved")

    RegisterForModEvent("Campfire_OnObjectPlaced", "Campfire_OnObjectPlaced")
    RegisterForModEvent("Campfire_OnObjectRemoved", "Campfire_OnObjectRemoved")
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
