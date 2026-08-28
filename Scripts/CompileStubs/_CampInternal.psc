Scriptname _CampInternal Hidden

; Compile-only declarations used by CampfireTogether overrides.
; This file is never compiled or packaged.

ObjectReference Function PlaceAndWaitFor3DLoaded(ObjectReference akOrigin, Form FormToPlace, Int Count = 1, Bool ForcePersist = False, Bool bDisableInteraction = False) Global
	Return None
EndFunction

Quest Function GetCrimeTrackingQuest() Global
	Return None
EndFunction

ReferenceAlias Function GetCrimeIllegalItemAlias(Int aiAlias) Global
	Return None
EndFunction

Function CampDebug(Int aiSeverity, String asLogMessage) Global
EndFunction
