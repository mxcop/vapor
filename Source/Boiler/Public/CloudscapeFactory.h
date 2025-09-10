#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"

#include "CloudscapeFactory.generated.h"

/* Responsible for creating and importing Cloudscape objects. */
UCLASS(hidecategories = Object, MinimalAPI)
class UCloudscapeFactory : public UFactory, public FReimportHandler {
	GENERATED_UCLASS_BODY()
public:
	/* ===== *
	 * Factory Interface.
	 * ===== */
	inline bool ShouldShowInNewMenu() const override { return false; };
	FText GetDisplayName() const override;
	inline bool ConfigureProperties() override { return true; };

	/* Asset Creation */
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override { return nullptr; };
	inline bool CanCreateNew() const override { return true; };

	/* Asset Importing */
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
		const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override { return nullptr; };
	virtual bool FactoryCanImport(const FString& Filename) override {
		UE_LOG(LogTemp, Warning, TEXT("Tried to import file! %s"), *Filename);
		return FPaths::GetExtension(Filename) == TEXT("vdb");
	};
	inline bool DoesSupportClass(UClass* Class) override { return true; };
	virtual UClass* ResolveSupportedClass() override { return nullptr; };

	virtual void CleanUp() override;

	/* ===== *
	 * Reimport Handler Interface.
	 * ===== */
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override { return false; };
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override {};
	virtual EReimportResult::Type Reimport(UObject* Obj) override { return EReimportResult::Succeeded; };

private:
	UObject* ImportInternal(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, bool& bOutOperationCanceled, bool bIsReimport) {};
};

#endif // WITH_EDITOR
