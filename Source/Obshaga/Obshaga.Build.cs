using UnrealBuildTool;

public class Obshaga : ModuleRules
{
	public Obshaga(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Прямой вызов Steam нужен для одного: искать комнату по всему миру (см. ObshagaSessionSubsystem.cpp).
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("SteamShared");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks");
			PrivateDefinitions.Add("OBSHAGA_WITH_STEAM=1");
		}
		else
		{
			PrivateDefinitions.Add("OBSHAGA_WITH_STEAM=0");
		}

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AIModule", "GameplayTasks", "Json", "RenderCore", "RHI", "OnlineSubsystem", "OnlineSubsystemUtils", "ApplicationCore" });
	}
}
