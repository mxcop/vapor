#include "VaporComponent.h"

UVaporComponent::UVaporComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {

}

AVapor::AVapor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	PrimaryActorTick.bCanEverTick = true;
	VaporComponent = CreateDefaultSubobject<UVaporComponent>(TEXT("VaporComponent"));
	VaporComponent->SetupAttachment(RootComponent);
}
