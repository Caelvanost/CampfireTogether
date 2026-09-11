scriptname _Camp_InstinctsController extends ActiveMagicEffect
{Maintains Instincts state-related things.}

import math
import CampUtil
import _CampInternal
import CommonHelperFunctions

Actor property PlayerRef auto
Message property _Camp_VisionPowerErrorMounted auto
Spell property _Camp_SurvivalVisionPower auto
GlobalVariable property _Camp_PerkRank_KeenSenses auto
GlobalVariable property _Camp_Setting_InstinctsFindTinder auto
GlobalVariable property _Camp_Setting_InstinctsFindFlora auto
GlobalVariable property _Camp_IsUsingInstincts auto

Quest property _Camp_InstinctsTreeTinderSearch auto
Quest property _Camp_InstinctsOtherTinderSearch auto
Quest property _Camp_InstinctsTreeFloraSearch auto
Quest property _Camp_InstinctsOtherFloraSearch auto
Spell property _Camp_SurvivalVisionPowerDetectSpell auto
Spell property _Camp_SurvivalVisionPowerDetectSpellIndoors auto
Spell property _Camp_SurvivalVisionPowerGuideSpell auto

float playerX = 0.0
float playerY = 0.0

bool searching = false
bool cft_allowed = false

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Actor sourceActor = akCaster
    if !sourceActor
        sourceActor = akTarget
    endif
    if !CampfireTogetherNative.ConsumeLocalCampfirePower("instincts", sourceActor)
        return
    endif
    cft_allowed = true

    if _Camp_IsUsingInstincts.GetValueInt() == 1
        _Camp_IsUsingInstincts.SetValueInt(2)
        StartSearching()
        searching = true
    endif
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
    if !cft_allowed
        return
    endif

    Utility.Wait(1)
    _Camp_IsUsingInstincts.SetValueInt(1)
    StopSearching()
    searching = false
EndEvent

function StartSearching()
    if PlayerRef.IsOnMount()
        _Camp_VisionPowerErrorMounted.Show()
        Dispel()
        return
    endif
    RegisterForAnimationEvent(PlayerRef, "FootLeft")
    FallbackEventEmitter emitterHit = GetEventEmitter_PlayerHit()
    emitterHit.RegisterActiveMagicEffectForModEventWithFallback("Campfire_PlayerHit", "PlayerHit", self)
    if GetSKSELoaded()
        RegisterForMenu("Dialogue Menu")
    endif

    if IsRefInInterior(PlayerRef)
        _Camp_SurvivalVisionPowerDetectSpellIndoors.Cast(PlayerRef)
    else
        _Camp_SurvivalVisionPowerDetectSpell.Cast(PlayerRef)
    endif
    _Camp_SurvivalVisionPowerGuideSpell.Cast(PlayerRef)

    playerX = PlayerRef.GetPositionX()
    playerY = PlayerRef.GetPositionY()
    RegisterEventsOnSearchQuests()
    SendEvent_InstinctsStartSearch()
    RegisterForSingleUpdate(5)
endFunction

function RegisterEventsOnSearchQuests()
    if GetSKSELoaded()
        if _Camp_Setting_InstinctsFindTinder.GetValueInt() == 2
            FallbackEventEmitter emitterStart = GetEventEmitter_InstinctsStartSearch()
            FallbackEventEmitter emitterStop = GetEventEmitter_InstinctsStopSearch()
            emitterStart.RegisterFormForModEventWithFallback("Campfire_InstinctsStartSearch", "InstinctsStartSearch", _Camp_InstinctsTreeTinderSearch)
            emitterStop.RegisterFormForModEventWithFallback("Campfire_InstinctsStopSearch", "InstinctsStopSearch", _Camp_InstinctsTreeTinderSearch)
            emitterStart.RegisterFormForModEventWithFallback("Campfire_InstinctsStartSearch", "InstinctsStartSearch", _Camp_InstinctsOtherTinderSearch)
            emitterStop.RegisterFormForModEventWithFallback("Campfire_InstinctsStopSearch", "InstinctsStopSearch", _Camp_InstinctsOtherTinderSearch)
        endif
        if _Camp_Setting_InstinctsFindFlora.GetValueInt() == 2
            FallbackEventEmitter emitterStart = GetEventEmitter_InstinctsStartSearch()
            FallbackEventEmitter emitterStop = GetEventEmitter_InstinctsStopSearch()
            emitterStart.RegisterFormForModEventWithFallback("Campfire_InstinctsStartSearch", "InstinctsStartSearch", _Camp_InstinctsTreeFloraSearch)
            emitterStop.RegisterFormForModEventWithFallback("Campfire_InstinctsStopSearch", "InstinctsStopSearch", _Camp_InstinctsTreeFloraSearch)
            emitterStart.RegisterFormForModEventWithFallback("Campfire_InstinctsStartSearch", "InstinctsStartSearch", _Camp_InstinctsOtherFloraSearch)
            emitterStop.RegisterFormForModEventWithFallback("Campfire_InstinctsStopSearch", "InstinctsStopSearch", _Camp_InstinctsOtherFloraSearch)
        endif
    endif
endFunction

Event OnUpdate()
    if searching
        if PlayerRef.IsOnMount()
            _Camp_VisionPowerErrorMounted.Show()
            Dispel()
            return
        endif

        if IsRefInInterior(PlayerRef)
            _Camp_SurvivalVisionPowerDetectSpellIndoors.Cast(PlayerRef)
        else
            _Camp_SurvivalVisionPowerDetectSpell.Cast(PlayerRef)
        endif

        float newX = PlayerRef.GetPositionX()
        float newY = PlayerRef.GetPositionY()

        float dist = GetDistance2DCoords(playerX, playerY, newX, newY)
        if dist > 256.0
            RefreshObjects()
            _Camp_SurvivalVisionPowerGuideSpell.Cast(PlayerRef)
        endif

        playerX = newX
        playerY = newY

        RegisterForSingleUpdate(5)
    endif
EndEvent

Event PlayerHit(Form akAggressor, Form akSource, Form akProjectile)
    if akSource as Spell
        if (akSource as Spell).IsHostile()
            Dispel()
        endif
    else
        Dispel()
    endif
endEvent

Event OnMenuOpen(string menuName)
    if menuName == "Dialogue Menu"
        Dispel()
    endif
EndEvent

Event OnAnimationEvent(ObjectReference akSource, string asEventName)
    if akSource == PlayerRef && asEventName == "FootLeft"
        if _Camp_PerkRank_KeenSenses.GetValueInt() == 0
            CancelEffect_Movement()
        elseif !PlayerRef.IsSneaking()
            CancelEffect_Movement()
        endif
    endif
EndEvent

function CancelEffect_Movement()
    UnregisterForUpdate()
    Dispel()
endFunction

function StopSearching()
    SendEvent_InstinctsStopSearch()
    PlayerRef.DispelSpell(_Camp_SurvivalVisionPower)
    PlayerRef.DispelSpell(_Camp_SurvivalVisionPowerGuideSpell)
endFunction

function RefreshObjects()
    SendEvent_InstinctsStopSearch()
    Utility.Wait(0.2)
    RegisterEventsOnSearchQuests()
    SendEvent_InstinctsStartSearch()
endFunction

function SendEvent_InstinctsStartSearch()
    FallbackEventEmitter emitter = GetEventEmitter_InstinctsStartSearch()
    int handle = emitter.Create("Campfire_InstinctsStartSearch")
    if handle
        emitter.Send(handle)
    endif
endFunction

function SendEvent_InstinctsStopSearch()
    FallbackEventEmitter emitter = GetEventEmitter_InstinctsStopSearch()
    int handle = emitter.Create("Campfire_InstinctsStopSearch")
    if handle
        emitter.Send(handle)
    endif
endFunction
