using UnrealBuildTool;

public class Obshaga : ModuleRules
{
	public Obshaga(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AIModule", "GameplayTasks", "Json", "RenderCore", "RHI", "OnlineSubsystem", "OnlineSubsystemUtils", "ApplicationCore" });
	}
}
