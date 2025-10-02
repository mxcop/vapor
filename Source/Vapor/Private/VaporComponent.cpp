#include "VaporComponent.h"

#include "VaporExtension.h"

UVaporComponent::UVaporComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {

}

AVapor::AVapor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	PrimaryActorTick.bCanEverTick = true;
	VaporComponent = CreateDefaultSubobject<UVaporComponent>(TEXT("VaporComponent"));
	VaporComponent->SetupAttachment(RootComponent);
}

void UVaporComponent::IntoRenderData(FCloudscapeRenderData& RenderData) const {
	RenderData.Absorption = Absorption;
	RenderData.Density = Density;
	RenderData.PrimaryNearStep = PrimaryNearStep;
	RenderData.PrimaryStepPerDistance = PrimaryStepPerDistance;
	RenderData.PrimaryMinSDFStep = PrimaryMinSDFStep;
	RenderData.SecondaryStep = SecondaryStep;
	RenderData.SecondaryExtinctThreshold = SecondaryExtinctThreshold;
	RenderData.NoiseFreq = NoiseFrequency;
}
