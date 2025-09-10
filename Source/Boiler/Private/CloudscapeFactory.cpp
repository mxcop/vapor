#include "CloudscapeFactory.h"

#if WITH_EDITOR

#define LOCTEXT_NAMESPACE "UCloudscapeFactory"

UCloudscapeFactory::UCloudscapeFactory(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = true;
	SupportedClass = nullptr; // This factory supports multiple classes, so SupportedClass needs to be nullptr

	Formats.Add(TEXT("vdb;OpenVDB Format"));
}

FText UCloudscapeFactory::GetDisplayName() const {
	return LOCTEXT("CloudscapeFactoryDescription", "Cloudscape");
}

void UCloudscapeFactory::CleanUp() {
	Super::CleanUp();
}

#endif

#undef LOCTEXT_NAMESPACE
