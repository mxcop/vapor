#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"
#include "VaporCloud.h"

#include "CloudscapeFactory.generated.h"

/* Responsible for creating and importing Cloudscape objects. */
UCLASS()
class UCloudscapeFactory : public UFactory {
	GENERATED_UCLASS_BODY()

public:
	FText GetDisplayName() const override;

	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
		const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;

	inline bool DoesSupportClass(UClass* Class) override;
	virtual UClass* ResolveSupportedClass() override;
	virtual bool FactoryCanImport(const FString& Filename) override;

private:
	UVaporCloud* CreateVolumeTextureFromVDB(const FString& Filename, UObject* InParent, FName InName, EObjectFlags Flags);
};

#endif // WITH_EDITOR
