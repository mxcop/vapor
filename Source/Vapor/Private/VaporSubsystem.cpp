#include "VaporSubsystem.h"
#include "VaporExtension.h"
#include "SceneViewExtension.h"

void UVaporSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
	VaporExtension = FSceneViewExtensions::NewExtension<FVaporExtension>();
	UE_LOG(LogTemp, Log, TEXT("Vapor: Subsystem initialized & SceneViewExtension created"));
}

void UVaporSubsystem::Deinitialize() {
	{
		VaporExtension->IsActiveThisFrameFunctions.Empty();

		FSceneViewExtensionIsActiveFunctor IsActiveFunctor;

		IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
		{
			return TOptional<bool>(false);
		};

		VaporExtension->IsActiveThisFrameFunctions.Add(IsActiveFunctor);
	}

	VaporExtension.Reset();
	VaporExtension = nullptr;
}
