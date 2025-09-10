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
};
