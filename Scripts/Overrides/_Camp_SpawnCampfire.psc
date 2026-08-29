Scriptname _Camp_SpawnCampfire extends ActiveMagicEffect

import CampUtil

Activator Property _Camp_Indicator_Campfire Auto
GlobalVariable Property _Camp_Setting_CampfireMode Auto
Message Property _Camp_CampfireModeSelect Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    if !CampfireTogetherNative.ConsumeLocalBuildIntent()
        Actor sourceActor = akCaster
        if !sourceActor
            sourceActor = akTarget
        endif

        CampfireTogetherNative.ReportRemoteBuildSuppressed(sourceActor)
        return
    endif

    if PlayerCanPlaceObjects()
        int mode = _Camp_Setting_CampfireMode.GetValueInt()
        if mode < 0 || mode > 1
            int i = _Camp_CampfireModeSelect.Show()
            if i == 0
                ; Quick
                _Camp_Setting_CampfireMode.SetValueInt(0)
            elseif i == 1
                ; Realistic
                _Camp_Setting_CampfireMode.SetValueInt(1)
            endif

            SendEvent_SaveSettingToProfile("campfire_mode", _Camp_Setting_CampfireMode.GetValueInt())
        endif

        ObjectReference f = Game.GetPlayer().PlaceAtMe(_Camp_Indicator_Campfire)
        (f as CampPlacementIndicator).Ready()
    endif
EndEvent

; @NOFALLBACK
Function SendEvent_SaveSettingToProfile(String asSettingName, Int aiSettingValue)
    if GetCompatibilitySystem().isSKYUILoaded
        int handle = ModEvent.Create("Campfire_SaveSettingToProfile")
        if handle
            ModEvent.PushString(handle, asSettingName)
            ModEvent.PushInt(handle, aiSettingValue)
            ModEvent.Send(handle)
        endif
    endif
EndFunction
