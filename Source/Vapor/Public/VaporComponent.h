#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "VaporComponent.generated.h"

/* Vapor Instance Component */
UCLASS(MinimalAPI)
class UVaporComponent : public UPrimitiveComponent {
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	TObjectPtr<class UVaporCloud> CloudAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	FVector3f Absorption = FVector3f(1.2f, 1.0f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float Density = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float PrimaryNearStep = 300.0f; // cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float PrimaryStepPerDistance = 0.003662f; // 1.0 / (16384.0 / 60.0)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float PrimaryMinSDFStep = 300.0f; // cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float SecondaryStep = 800.0f; // cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float SecondaryExtinctThreshold = 0.99f; // %

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	float NoiseFrequency = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloudscape")
	bool Debug = false;

	void IntoRenderData(class FCloudscapeRenderData& RenderData) const;
};

/* Vapor Instance Actor */
UCLASS(MinimalAPI)
class AVapor : public AActor {
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
