using UnrealBuildTool;

public class ObshagaEditorTarget : TargetRules
{
	public ObshagaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Obshaga");
	}
}
