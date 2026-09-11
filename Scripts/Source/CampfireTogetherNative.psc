Scriptname CampfireTogetherNative Hidden

Function ReportPlaced(ObjectReference akPlacedObject, float afPositionX, float afPositionY, float afPositionZ, float afAngleX, float afAngleY, float afAngleZ, bool abIsTent) Global Native
Function ReportRemoved(Form akBaseObject, float afPositionX, float afPositionY, float afPositionZ, float afAngleX, float afAngleY, float afAngleZ, bool abIsTent) Global Native
Bool Function IsRemoteCampObject(ObjectReference akReference) Global Native

Bool Function IsRemoteMaterializationRequestValid(Int aiRequestID) Global Native
Float Function GetRemoteMaterializationX(Int aiRequestID) Global Native
Float Function GetRemoteMaterializationY(Int aiRequestID) Global Native
Float Function GetRemoteMaterializationZ(Int aiRequestID) Global Native
Float Function GetRemoteMaterializationAngleX(Int aiRequestID) Global Native
Float Function GetRemoteMaterializationAngleY(Int aiRequestID) Global Native
Float Function GetRemoteMaterializationAngleZ(Int aiRequestID) Global Native
Function ReportRemoteMaterialized(Int aiRequestID, ObjectReference akReference) Global Native
Function ReportRemoteMaterializationFailed(Int aiRequestID) Global Native

Int Function GetTrackedCampfireCount() Global Native
ObjectReference Function GetTrackedCampfire(Int aiIndex) Global Native
Bool Function CampfireStateNeedsApply(ObjectReference akReference) Global Native
Int Function GetDesiredCampfireStage(ObjectReference akReference) Global Native
Int Function GetDesiredCampfireSize(ObjectReference akReference) Global Native
Float Function GetDesiredCampfireRemainingHours(ObjectReference akReference) Global Native
Form Function GetDesiredCampfireFuelLit(ObjectReference akReference) Global Native
Form Function GetDesiredCampfireFuelUnlit(ObjectReference akReference) Global Native
Form Function GetDesiredCampfireLight(ObjectReference akReference) Global Native
ObjectReference Function GetAssignedCampfireSeat(ObjectReference akCampfire, ObjectReference akSeat1, ObjectReference akSeat2, ObjectReference akSeat3, ObjectReference akSeat4) Global Native
Function ReportCampfireState(ObjectReference akReference, Int aiStage, Int aiSize, Float afRemainingHours, Form akFuelLit, Form akFuelUnlit, Form akLight) Global Native
Function AcknowledgeCampfireState(ObjectReference akReference) Global Native
Function StateBridgeReady() Global Native

Bool Function ConsumeLocalCampfirePower(String asPowerTag, Actor akCaster) Global Native
Bool Function AuthorizeNestedCampfirePower(String asPowerTag) Global Native
Bool Function ConsumeLocalBuildIntent() Global Native
Function ReportRemoteBuildSuppressed(Actor akCaster) Global Native
Function ReportRemoteBedrollAccess(ObjectReference akBedroll, ObjectReference akTent) Global Native
Function BridgeReady() Global Native
