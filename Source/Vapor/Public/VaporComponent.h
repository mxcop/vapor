#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "VaporComponent.generated.h"

/* Vapor Instance Component */
UCLASS(MinimalAPI)
class UVaporComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	TObjectPtr<class USparseVolumeTexture> SparseVolumeTexturePreview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	FVector3f Absorption = FVector3f(1.2f, 1.0f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float Density = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float MinStepSize = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float InnerStepSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float StepSizeMult = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float ExtinctionThreshold = 0.001f;
};

/* Vapor Instance Actor */
UCLASS(MinimalAPI)
class AVapor : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Atmosphere, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UVaporComponent> VaporComponent;

#if WITH_EDITOR
	virtual bool ActorTypeSupportsDataLayer() const override { return true; }
#endif

public:
	/** Returns UVaporComponent subobject */
	UVaporComponent* GetComponent() const { return VaporComponent; }
};
