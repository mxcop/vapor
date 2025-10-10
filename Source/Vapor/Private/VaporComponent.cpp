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

void UVaporComponent::IntoRenderData(FCloudscapeRenderData& RenderData) const {
	RenderData.Absorption = ColorSpecifier == ECloudColorSpecifier::Absorption ? FVector3f(Absorption) : GetAbsorption();
	RenderData.AmbientLuminance = FVector3f(AmbientStrength, AmbientStrength, AmbientStrength);
	RenderData.Density = Density;
	RenderData.ProfileWidth = ProfileWidth;
	RenderData.PrimaryNearStep = PrimaryNearStep;
	RenderData.PrimaryStepPerDistance = PrimaryStepPerDistance;
	RenderData.PrimaryMinSDFStep = PrimaryMinSDFStep;
	RenderData.DirectScattering = DirectScattering;
	RenderData.MultiScattering = MultiScattering;
	RenderData.AmbientScattering = AmbientScattering;
	RenderData.SecondaryStep = SecondaryStep;
	RenderData.SecondaryExtinctThreshold = SecondaryExtinctThreshold / 100.0f;
	RenderData.NoiseFreq = NoiseFrequency;
	RenderData.WindSpeed = WindSpeed;
}
