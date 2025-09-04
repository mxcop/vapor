#include "BoilerModule.h"

THIRD_PARTY_INCLUDES_START
#define M_PI 3.14
#define M_PI_2 6.28
#include <openvdb/openvdb.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "Boiler"

void FBoiler::StartupModule() {
	const FString VdbDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Vapor"))->GetBaseDir(), TEXT("Content/cloud.vdb"));

	openvdb::initialize();
	openvdb::io::File file(TCHAR_TO_UTF8(*VdbDir));
	const uint64_t size = file.getSize();
	// UE_LOG(LogInit, Warning, TEXT("The file size is: %u"), size);

#if PLATFORM_WINDOWS
	const FString FormattedMessage = FString::Printf(TEXT("The file size is: %u"), size);
	//FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *VdbDir, TEXT("Debug"));
	FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
		*FormattedMessage, TEXT("Debug"));
#endif
}

void FBoiler::ShutdownModule() {

}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBoiler, Boiler);
