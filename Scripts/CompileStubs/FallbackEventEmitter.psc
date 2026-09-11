Scriptname FallbackEventEmitter extends Form

Int Function Create(String asEventName)
    Return 0
EndFunction

Function Send(Int aiHandle)
EndFunction

Function RegisterActiveMagicEffectForModEventWithFallback(String asEventName, String asCallbackName, ActiveMagicEffect akReceiver)
EndFunction

Function RegisterFormForModEventWithFallback(String asEventName, String asCallbackName, Form akReceiver)
EndFunction
