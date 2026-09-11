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

Function ReportObservedState(ObjectReference ref, CampCampfire fire)
    Form fuelLit = None
    Form fuelUnlit = None
    Form light = None

    If fire.myFuelLit
        fuelLit = fire.myFuelLit.GetBaseObject()
    EndIf
    If fire.myFuelUnlit
        fuelUnlit = fire.myFuelUnlit.GetBaseObject()
    EndIf
    If fire.myLight
        light = fire.myLight.GetBaseObject()
    EndIf

    CampfireTogetherNative.ReportCampfireState(ref, fire.campfire_stage, fire.campfire_size, fire.GetRemainingDisplayTime(), fuelLit, fuelUnlit, light)
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

    Int burnHours = Math.Ceiling(desiredRemaining)
    If burnHours < 1
        burnHours = 1
    EndIf

    If fuelMismatch
        ; SetFuel chooses lit/unlit assets based on campfire_stage, so establish
        ; the desired stable stage first and let Campfire build its own children.
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

    ; Campfire's helpers intentionally normalize some transitions (for example
    ; SetFuel -> PlaceFuel sets stage 3). Restore the authoritative stable value.
    fire.campfire_stage = desiredStage
    fire.campfire_size = desiredSize
EndFunction
