#include "VaporModule.h"

#define LOCTEXT_NAMESPACE "Vapor"

void FVapor::StartupModule() {
	/* Setup the shader source directory */
	if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/Plugins/Vapor"))) {
		const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Vapor"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugins/Vapor"), PluginShaderDir);
	}
}

void FVapor::ShutdownModule() {

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVapor, Vapor);
