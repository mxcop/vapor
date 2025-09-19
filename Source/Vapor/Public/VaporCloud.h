#include "CoreMinimal.h"

#include "VaporCloud.generated.h"

UCLASS()
class UVaporCloud : public UObject {
	GENERATED_UCLASS_BODY()

public:
	/* Cloud density data field (0..1) */
	UPROPERTY(VisibleAnywhere, Category = "Textures")
	class UVolumeTexture* DensityField = nullptr;

	/* Cloud SDF data field, for ray-marching */
	UPROPERTY(VisibleAnywhere, Category = "Textures")
	class UVolumeTexture* SignedDistanceField = nullptr;
};
