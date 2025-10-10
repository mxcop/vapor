#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "VaporComponent.generated.h"

UENUM()
enum class ECloudColorSpecifier {
	Absorption,
	Transmittance
};

/* Vapor Instance Component */
UCLASS(MinimalAPI)
class UVaporComponent : public UPrimitiveComponent {
	GENERATED_UCLASS_BODY()

	/* -===- Cloud Volume Section -===- */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Volume")
	TObjectPtr<class UVaporCloud> CloudAsset;

	UPROPERTY(EditAnywhere, Category = "Cloud Volume")
	ECloudColorSpecifier ColorSpecifier = ECloudColorSpecifier::Absorption;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Volume", meta = (HideAlphaChannel, EditCondition = "ColorSpecifier==ECloudColorSpecifier::Absorption", EditConditionHides))
	FLinearColor Absorption = FLinearColor(1.0f, 1.0f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Volume", meta = (HideAlphaChannel, EditCondition = "ColorSpecifier==ECloudColorSpecifier::Transmittance", EditConditionHides))
	FLinearColor Transmittance = FLinearColor(0.9f, 0.9f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Volume")
	float Density = 0.01f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Volume", meta = (Units = "Centimeters", ToolTip = "Width of the dimensional profile."))
	float ProfileWidth = 16000.0f; // cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Volume", meta = (Units = "Hertz", ToolTip = "Frequency of the noise applied to the cloud."))
	float NoiseFrequency = 0.0001f; // hz
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Volume", meta = (Units = "CentimetersPerSecondSquared"))
	FVector3f WindSpeed = FVector3f(0.0f, 0.0f, 400.0f); // cm/s2

	/* -===- Cloud Lighting Section -===- */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Lighting")
	bool DirectScattering = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Lighting")
	bool MultiScattering = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Lighting")
	bool AmbientScattering = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Lighting", meta = (Units = "Times", EditCondition = "AmbientScattering"))
	float AmbientStrength = 1.0f;

	/* -===- Cloud Quality Section -===- */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Quality", meta = (Units = "Centimeters"))
	float PrimaryNearStep = 200.0f; // cm
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Quality", meta = (Units = "Times"))
	float PrimaryStepPerDistance = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Quality", meta = (Units = "Centimeters"))
	float PrimaryMinSDFStep = 200.0f; // cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Quality", meta = (Units = "Centimeters"))
	float SecondaryStep = 800.0f; // cm
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Quality", meta = (Units = "Percent"))
	float SecondaryExtinctThreshold = 0.0f; // %

	/* -===- Cloud Debug Section -===- */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Debug")
	bool Debug = false;

	/** @brief Get the absorption of the cloud, based on the transmittance. */
	FVector3f GetAbsorption() const;

	/** @brief Insert this cloud components data into a render data struct. */
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
