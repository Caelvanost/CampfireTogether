Scriptname CampCampfire extends ObjectReference

Int Property campfire_stage Auto Hidden
Int Property campfire_size Auto Hidden
ObjectReference Property myFuelLit Auto Hidden
ObjectReference Property myFuelUnlit Auto Hidden
ObjectReference Property myLight Auto Hidden
ObjectReference Property mySitFurniture1 Auto Hidden
ObjectReference Property mySitFurniture2 Auto Hidden
ObjectReference Property mySitFurniture3 Auto Hidden
ObjectReference Property mySitFurniture4 Auto Hidden
GlobalVariable Property _Camp_PerkRank_Resourceful Auto

Float Function GetRemainingDisplayTime()
    Return 0.0
EndFunction

Function SetFuel(Activator akFuelLit, Activator akFuelUnlit, Light akLight, Int aiBurnDuration, Bool abFuelRefresh = false)
EndFunction

Function LightFire(Bool abFuelRefresh = false)
EndFunction

Function PutOutFire()
EndFunction

Function BurnToEmbers()
EndFunction

Function BurnToAshes()
EndFunction
