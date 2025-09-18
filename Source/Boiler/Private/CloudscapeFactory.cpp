#include "CloudscapeFactory.h"

#if WITH_EDITOR

#include "Engine/VolumeTexture.h"

/* OpenVDB Includes */
THIRD_PARTY_INCLUDES_START
#include "openvdb/openvdb.h"
#include "openvdb/Grid.h"
#include "openvdb/tools/Interpolation.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "UCloudscapeFactory"

UCloudscapeFactory::UCloudscapeFactory(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	SupportedClass = UVolumeTexture::StaticClass();
	Formats.Add(TEXT("vdb;OpenVDB Format"));
	bCreateNew = false;
	bEditorImport = true;
	bEditAfterNew = true;
	ImportPriority = DefaultImportPriority;
}

FText UCloudscapeFactory::GetDisplayName() const {
	return LOCTEXT("CloudscapeFactoryDescription", "Cloudscape");
}

bool UCloudscapeFactory::DoesSupportClass(UClass* Class) {
	return Class == UVolumeTexture::StaticClass();
}

UClass* UCloudscapeFactory::ResolveSupportedClass() {
	return UVolumeTexture::StaticClass();
}

bool UCloudscapeFactory::FactoryCanImport(const FString& Filename) {
	return FPaths::GetExtension(Filename).Equals(TEXT("vdb"), ESearchCase::IgnoreCase);
}

UObject* UCloudscapeFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) {
	bOutOperationCanceled = false;
	if (!FactoryCanImport(Filename)) return nullptr;

	return CreateVolumeTextureFromVDB(Filename, InParent, InName, Flags);
}

/* Volume texture size */
constexpr uint32 VTEX_X = 128;
constexpr uint32 VTEX_Y = 128;
constexpr uint32 VTEX_Z = 128;

UVolumeTexture* UCloudscapeFactory::CreateVolumeTextureFromVDB(const FString& Filename, UObject* InParent, FName InName, EObjectFlags Flags) {
	/* Init OpenVDB */
	openvdb::initialize();

	/* Create the OpenVDB file loader */
	openvdb::io::File File(TCHAR_TO_UTF8(*Filename));

	/* Try to open the VDB file */
	try {
		File.open();
	} catch (const std::exception& e) {
		UE_LOG(LogTemp, Error, TEXT("Error opening VDB file: %s"), UTF8_TO_TCHAR(e.what()));
		return nullptr;
	}

	/* Make sure the VDB file has at least 1 grid */
	if (File.getGrids()->size() == 0ull) {
		UE_LOG(LogTemp, Error, TEXT("No grids found in VDB file"));
		return nullptr;
	}

	/* Just grab the first grid in the file for now */
	const openvdb::GridBase::Ptr BaseGrid = File.getGrids()->front();
	const openvdb::FloatGrid::Ptr DensityGrid = openvdb::gridPtrCast<openvdb::FloatGrid>(BaseGrid);

	/* Make sure our grid is a float grid */
	if (!DensityGrid) {
		UE_LOG(LogTemp, Error, TEXT("Grid in VDB file is not a float grid"));
		return nullptr;
	}

	/* Create a new volume texture */
	UVolumeTexture* VolumeTexture = NewObject<UVolumeTexture>(InParent, InName, Flags);
	VolumeTexture->Source.Init(VTEX_X, VTEX_Y, VTEX_Z, 1, TSF_G8, nullptr);
	
	/* Get the extent of the grid in world space */
	const openvdb::math::Transform GridTransform = DensityGrid->transform();
	const openvdb::CoordBBox AABB = DensityGrid->evalActiveVoxelBoundingBox();
	const openvdb::Vec3d GridMin = GridTransform.indexToWorld(AABB.min());
	const openvdb::Vec3d GridExtent = GridTransform.indexToWorld(AABB.max()) - GridMin;

	/* Calculate the step size along each axis */
	const double StepX = GridExtent.x() / VTEX_X;
	const double StepY = GridExtent.y() / VTEX_Y;
	const double StepZ = GridExtent.z() / VTEX_Z;
	const double StepSize = std::max(std::max(StepX, StepY), StepZ);

	/* Find the min and max density in the grid */
	float MinDensity = 0.0f;
	float MaxDensity = 0.0f;
	DensityGrid->evalMinMax(MinDensity, MaxDensity);
	const float DensityRange = MaxDensity - MinDensity;

	/* Create a grid sampler and lock the volume texture data */
	openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler> sampler(*DensityGrid);
	uint8* TextureData = VolumeTexture->Source.LockMip(0);

	/* Re-sample the density grid */
	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				const openvdb::Vec3d worldPos(
					GridMin.x() + x * StepSize,
					GridMin.y() + y * StepSize,
					GridMin.z() + z * StepSize
				);

				/* Sample the density grid */
				const float Density = sampler.wsSample(worldPos) / DensityRange;
				const uint8 DensityData = (uint8)(Density * 255.0f);

				/* Calculate the texture index and write our density data */
				const int32 index = x + ((VTEX_Y - y - 1) * VTEX_X) + (z * VTEX_X * VTEX_Y);
				TextureData[index] = DensityData;
			}
		}
	}

	/* Unlock the volume texture data */
	VolumeTexture->Source.UnlockMip(0);

	/* Set all the volume texture settings */
	VolumeTexture->MipGenSettings = TMGS_NoMipmaps;
	VolumeTexture->CompressionSettings = TC_Grayscale;
	VolumeTexture->SRGB = false;
	VolumeTexture->Filter = TF_Bilinear;
	VolumeTexture->AddressMode = TA_Clamp;

	/* Update the volume texture resource */
	VolumeTexture->UpdateResource();

	File.close(); /* Finally close the VDB file, and return */
	return VolumeTexture;
}

#endif

#undef LOCTEXT_NAMESPACE
