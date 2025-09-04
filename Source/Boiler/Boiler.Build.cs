using UnrealBuildTool;
using System.IO;

public class Boiler : ModuleRules
{
    public Boiler(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Public dependencies
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Projects"
            });

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

        // Private dependencies
        // PrivateDependencyModuleNames.AddRange(
        // new string[]
        // {
	    //	"SparseVolumeTexture",	// For the legacy OpenVDB importer reference, that actually uses the OpenVDB SDK
		// }
        // );
    }
}
