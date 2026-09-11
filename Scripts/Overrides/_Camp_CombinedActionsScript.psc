scriptname _Camp_CombinedActionsScript extends ActiveMagicEffect

Message property _Camp_CombinedActionsMenu auto
Actor property PlayerRef auto
Spell property _Camp_CampfireSpell auto
Spell property _Camp_CreateItemSpell auto
Spell property _Camp_HarvestWoodSpell auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Actor sourceActor = akCaster
    if !sourceActor
        sourceActor = akTarget
    endif

    if !CampfireTogetherNative.ConsumeLocalCampfirePower("resourcefulness", sourceActor)
        return
    endif

    int i = _Camp_CombinedActionsMenu.Show()
    if i == 0
        CampfireTogetherNative.AuthorizeNestedCampfirePower("harvest")
        _Camp_HarvestWoodSpell.Cast(PlayerRef, PlayerRef)
    elseif i == 1
        CampfireTogetherNative.AuthorizeNestedCampfirePower("build")
        _Camp_CampfireSpell.Cast(PlayerRef, PlayerRef)
    elseif i == 2
        CampfireTogetherNative.AuthorizeNestedCampfirePower("create")
        _Camp_CreateItemSpell.Cast(PlayerRef, PlayerRef)
    elseif i == 3
        ; Exit
    endif
EndEvent
