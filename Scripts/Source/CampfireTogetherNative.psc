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

Bool Function ConsumeLocalBuildIntent() Global Native
Function ReportRemoteBuildSuppressed(Actor akCaster) Global Native
Function ReportRemoteBedrollAccess(ObjectReference akBedroll, ObjectReference akTent) Global Native
Function BridgeReady() Global Native
