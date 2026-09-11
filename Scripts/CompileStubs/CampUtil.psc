Scriptname CampUtil Hidden

; Compile-only declarations used by CampfireTogether overrides.
; This file is never compiled or packaged.

Bool Function LegalToCampHere(Bool abIgnoreSetting = False) Global
    Return True
EndFunction

Bool Function IsCrimeToPlaceInTowns(Form akBaseObject) Global
    Return False
EndFunction

Function SendEvent_OnObjectPlaced(ObjectReference akObjectReference) Global
EndFunction

Bool Function IsTrackedFollower(Actor akActor) Global
    Return False
EndFunction

Bool Function IsRefInInterior(ObjectReference akReference) Global
    Return False
EndFunction

Bool Function PlayerCanPlaceObjects(Bool abShowMessage = True, Bool abPlayerBusyCheck = True) Global
    Return True
EndFunction

_Camp_Compatibility Function GetCompatibilitySystem() Global
    Return None
EndFunction

Bool Function GetSKSELoaded() Global
    Return True
EndFunction

FallbackEventEmitter Function GetEventEmitter_PlayerHit() Global
    Return None
EndFunction

FallbackEventEmitter Function GetEventEmitter_InstinctsStartSearch() Global
    Return None
EndFunction

FallbackEventEmitter Function GetEventEmitter_InstinctsStopSearch() Global
    Return None
EndFunction

Int Function GetTrackedFollowerCount() Global
    Return 0
EndFunction

Actor Function GetTrackedFollower(Int aiIndex) Global
    Return None
EndFunction

Int Function GetTrackedAnimalCount() Global
    Return 0
EndFunction

Actor Function GetTrackedAnimal() Global
    Return None
EndFunction
