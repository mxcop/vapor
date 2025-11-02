#include "VaporComponent.h"

#include "VaporExtension.h"

UVaporComponent::UVaporComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {

}

AVapor::AVapor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	PrimaryActorTick.bCanEverTick = true;
	VaporComponent = CreateDefaultSubobject<UVaporComponent>(TEXT("VaporComponent"));
	VaporComponent->SetupAttachment(RootComponent);
}

FVector3f UVaporComponent::GetAbsorption() const {
	const float LogR = FMath::Loge(FMath::Max(0.001f, Transmittance.R));
	const float LogG = FMath::Loge(FMath::Max(0.001f, Transmittance.G));
	const float LogB = FMath::Loge(FMath::Max(0.001f, Transmittance.B));
	return FVector3f(2.0f - LogR, 2.0f - LogG, 2.0f - LogB);
}

/// Calculate the threshold path density at which the absorption reaches a given threshold.
static float CalcPathDensityThreshold(const float AbsorptionThreshold, const FVector3f Absorption) {
	return -FMath::Loge(AbsorptionThreshold) / FMath::Min3(Absorption.X, Absorption.Y, Absorption.Z);
}

void UVaporComponent::IntoRenderData(FCloudscapeRenderData& RenderData) const {
	RenderData.Absorption = ColorSpecifier == ECloudColorSpecifier::Absorption ? FVector3f(Absorption) : GetAbsorption();
	RenderData.AmbientLuminance = FVector3f(AmbientLuminance);
	RenderData.Density = Density;
	RenderData.ProfileWidth = ProfileWidth;
	RenderData.PrimaryNearStep = PrimaryNearStep;
	RenderData.PrimaryStepPerDistance = PrimaryStepPerDistance;
	RenderData.PrimaryMinSDFStep = PrimaryMinSDFStep;
	RenderData.DirectScattering = DirectScattering;
	RenderData.MultiScattering = MultiScattering;
	RenderData.AmbientScattering = AmbientScattering;
	RenderData.SecondaryStep = SecondaryStep;
	RenderData.SecondaryExtinctThreshold = CalcPathDensityThreshold(SecondaryExtinctThreshold / 100.0f, RenderData.Absorption);
	RenderData.NoiseFreq = NoiseFrequency;
	RenderData.WindSpeed = WindSpeed;
}
