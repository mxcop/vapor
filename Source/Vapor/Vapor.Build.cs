using UnrealBuildTool;
using System.IO;

public class Vapor : ModuleRules
{
    public Vapor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "Renderer",
            "RHI",
            "Projects",
            "SparseVolumeTexture"
            });

        var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

        PrivateIncludePaths.AddRange(
            new string[] {
				// Required to find PostProcessing includes f.ex. screenpass.h & TranslucentPassResource.h
				Path.Combine(EngineDir, "Source/Runtime/Renderer/Private"),
                Path.Combine(EngineDir, "Source/Runtime/Renderer/Internal")
            }
        );

        // Specific to OpenVDB support
        if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
        {
            bUseRTTI = true;
            bDisableAutoRTFMInstrumentation = true; // AutoRTFM cannot be used with exceptions
            bEnableExceptions = true;
            PublicDefinitions.Add("OPENVDB_AVAILABLE=1");

            AddEngineThirdPartyPrivateStaticDependencies(Target,
                "IntelTBB",
                "Blosc",
                "zlib",
                "Boost",
                "OpenVDB"
            );
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            bUseRTTI = false;
            bEnableExceptions = false;
            PublicDefinitions.Add("OPENVDB_AVAILABLE=1");

            AddEngineThirdPartyPrivateStaticDependencies(Target,
                "IntelTBB",
                "Blosc",
                "zlib",
                "Boost",
                "OpenVDB"
            );
        }
        else
        {
            PublicDefinitions.Add("OPENVDB_AVAILABLE=0");
        }
    }
}
