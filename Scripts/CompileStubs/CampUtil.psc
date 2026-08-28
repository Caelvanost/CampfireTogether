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
