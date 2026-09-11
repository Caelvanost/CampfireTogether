Scriptname CampfireTogetherStateBridge extends Quest

Float Property PollInterval = 2.0 Auto

Event OnInit()
    CampfireTogetherNative.StateBridgeReady()
    RegisterForSingleUpdate(PollInterval)
EndEvent

Event OnUpdate()
    PollCampfires()
    RegisterForSingleUpdate(PollInterval)
EndEvent

Function PollCampfires()
    Int count = CampfireTogetherNative.GetTrackedCampfireCount()
    Int i = 0
    While i < count
        ObjectReference ref = CampfireTogetherNative.GetTrackedCampfire(i)
        CampCampfire fire = ref as CampCampfire
        If fire
            If CampfireTogetherNative.CampfireStateNeedsApply(ref)
                ApplyAuthoritativeState(ref, fire)
                CampfireTogetherNative.AcknowledgeCampfireState(ref)
            Else
                ReportObservedState(ref, fire)
            EndIf
        EndIf
        i += 1
    EndWhile
EndFunction

Float Function GetResourcefulMultiplier(CampCampfire fire)
    Int rank = 0
    If fire._Camp_PerkRank_Resourceful
        rank = fire._Camp_PerkRank_Resourceful.GetValueInt()
    EndIf
    Return 1.0 + (rank * 0.25)
EndFunction

Float Function GetFullFuelHours(CampCampfire fire)
    Float baseHours = 0.0
    If fire.campfire_size == 1
        baseHours = 1.0
    ElseIf fire.campfire_size == 2
        baseHours = 3.0
    ElseIf fire.campfire_size == 3
        baseHours = 6.0
    ElseIf fire.campfire_size == 4
        baseHours = 12.0
    EndIf
    Return baseHours * GetResourcefulMultiplier(fire)
EndFunction

Function ReportObservedState(ObjectReference ref, CampCampfire fire)
    Form fuelLit = None
    Form fuelUnlit = None
    Form lightForm = None

    If fire.myFuelLit
        fuelLit = fire.myFuelLit.GetBaseObject()
    EndIf
    If fire.myFuelUnlit
        fuelUnlit = fire.myFuelUnlit.GetBaseObject()
    EndIf
    If fire.myLight
        lightForm = fire.myLight.GetBaseObject()
    EndIf

    Float reportedHours = fire.GetRemainingDisplayTime()
    If fire.campfire_stage >= 3 && fire.campfire_size > 0
        ; Campfire reports 0 remaining while fuel is placed but unlit. Send the
        ; prospective full duration instead so another client can light the same
        ; shared fuel with the correct burn duration.
        reportedHours = GetFullFuelHours(fire)
    EndIf

    CampfireTogetherNative.ReportCampfireState(ref, fire.campfire_stage, fire.campfire_size, reportedHours, fuelLit, fuelUnlit, lightForm)
EndFunction

Function ApplyAuthoritativeState(ObjectReference ref, CampCampfire fire)
    Int desiredStage = CampfireTogetherNative.GetDesiredCampfireStage(ref)
    Int desiredSize = CampfireTogetherNative.GetDesiredCampfireSize(ref)
    Float desiredRemaining = CampfireTogetherNative.GetDesiredCampfireRemainingHours(ref)

    Activator desiredFuelLit = CampfireTogetherNative.GetDesiredCampfireFuelLit(ref) as Activator
    Activator desiredFuelUnlit = CampfireTogetherNative.GetDesiredCampfireFuelUnlit(ref) as Activator
    Light desiredLight = CampfireTogetherNative.GetDesiredCampfireLight(ref) as Light

    If desiredStage <= 0
        If fire.campfire_stage != 0
            fire.BurnToAshes()
        EndIf
        fire.campfire_stage = 0
        fire.campfire_size = 0
        Return
    ElseIf desiredStage == 1
        If fire.campfire_stage != 1
            fire.BurnToEmbers()
        EndIf
        fire.campfire_stage = 1
        fire.campfire_size = 0
        Return
    EndIf

    Bool fuelMismatch = false
    If desiredFuelLit && desiredFuelUnlit && desiredLight
        If !fire.myFuelLit || !fire.myFuelUnlit || !fire.myLight
            fuelMismatch = true
        ElseIf fire.myFuelLit.GetBaseObject() != desiredFuelLit
            fuelMismatch = true
        ElseIf fire.myFuelUnlit.GetBaseObject() != desiredFuelUnlit
            fuelMismatch = true
        ElseIf fire.myLight.GetBaseObject() != desiredLight
            fuelMismatch = true
        EndIf
    EndIf

    Float localMultiplier = GetResourcefulMultiplier(fire)
    If localMultiplier <= 0.0
        localMultiplier = 1.0
    EndIf

    Int burnHours = Math.Floor((desiredRemaining / localMultiplier) + 0.5)
    If burnHours < 1
        burnHours = 1
    EndIf

    If desiredStage == 2 && desiredFuelLit && desiredFuelUnlit && desiredLight
        ; Always rebuild the fuel when applying a remote burning state. Calling
        ; LightFire() alone reuses this client's stale private burn_duration and
        ; caused cases such as 5h45 on P1 becoming 0h45 on P2.
        fire.campfire_stage = 2
        fire.campfire_size = desiredSize
        fire.SetFuel(desiredFuelLit, desiredFuelUnlit, desiredLight, burnHours, true)
    ElseIf fuelMismatch
        fire.campfire_stage = desiredStage
        fire.campfire_size = desiredSize
        fire.SetFuel(desiredFuelLit, desiredFuelUnlit, desiredLight, burnHours, true)
    Else
        If desiredStage == 2 && fire.campfire_stage != 2
            fire.LightFire(true)
        ElseIf desiredStage >= 3 && fire.campfire_stage == 2
            fire.PutOutFire()
        EndIf
    EndIf

    fire.campfire_stage = desiredStage
    fire.campfire_size = desiredSize
EndFunction
