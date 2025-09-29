#include "CloudscapeFactory.h"

#if WITH_EDITOR

#include "Engine/VolumeTexture.h"

THIRD_PARTY_INCLUDES_START
__pragma(warning(disable: 4706))
#undef check /* <- Otherwise we cannot compile... */
#include "openvdb/openvdb.h"
#include "openvdb/Grid.h"
#include "openvdb/tools/Interpolation.h"
#include "openvdb/tools/FastSweeping.h"
#include <openvdb/tools/GridTransformer.h>
#include <openvdb/tools/Filter.h>
#undef UpdateResource /* <- Windows header included somewhere... */
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
constexpr uint32 VTEX_X = 512;
constexpr uint32 VTEX_Y = 512;
constexpr uint32 VTEX_Z = 64;

/** @brief Remap an input value in an input range to an output range. */
float Remap(float Value, float InMin, float InMax, float OutMin, float OutMax) {
	const float Range = InMax - InMin;
	const float Norm = (Value - InMin) / Range;
	const float Clamped = (Norm < 0.0f) ? 0.0f : (Norm > 1.0f) ? 1.0f : Norm;
	return OutMin + (Clamped * (OutMax - OutMin));
}

openvdb::FloatGrid::Ptr ResampleGrid(const openvdb::BBoxd& WorldAABB, const openvdb::FloatGrid& Grid, const uint32 X, const uint32 Y, const uint32 Z) {
	/* Find the step size to use for resampling the grid */
	const openvdb::Vec3d StepSizes = WorldAABB.extents() / openvdb::Vec3d(X, Z, Y);
	const double StepSize = std::max(std::max(StepSizes.x(), StepSizes.y()), StepSizes.z());

	/* Calculate the down-scale factor of the resampling process */
	const openvdb::Coord GridSize = Grid.evalActiveVoxelDim();
	const openvdb::Vec3f ScaleFactor = openvdb::Vec3f(GridSize.x(), GridSize.y(), GridSize.z()) / openvdb::Vec3f(X, Z, Y);
	const float LargestScaleFactor = std::max(std::max(ScaleFactor.x(), ScaleFactor.y()), ScaleFactor.z());

	/* Create the new resampled grid */
	openvdb::FloatGrid::Ptr Resampled = openvdb::FloatGrid::create(0.0f);
	openvdb::FloatGrid::Accessor Accessor = Resampled->getAccessor();

	/* Pre-filter the input grid for down-sampling */
	openvdb::FloatGrid::Ptr Filtered = Grid.deepCopy();
	if (LargestScaleFactor >= 2.0f) {
		openvdb::tools::Filter<openvdb::FloatGrid> Filter(*Filtered);
		Filter.gaussian((int)LargestScaleFactor / 2, 1);
	}

	/* Create a box grid sampler for the input grid */
	const openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler> sampler(*Filtered);

	/* Resample the pre-filtered grid */
	for (uint32 z = 0; z < Z; ++z) {
		for (uint32 y = 0; y < Y; ++y) {
			for (uint32 x = 0; x < X; ++x) {
				/* Sample the pre-filtered grid */
				const float Value = sampler.wsSample(openvdb::Vec3d(
					WorldAABB.min().x() + x * StepSizes.x(),
					WorldAABB.min().y() + z * StepSizes.y(),
					WorldAABB.min().z() + y * StepSizes.z()
				));

				/* Set the value inside our resampled grid */
				Accessor.setValue(openvdb::Coord(x, y, z), Value);
			}
		}
	}

	return Resampled;
}

void CreateDensityTexture(UVolumeTexture& Output, const openvdb::FloatGrid& DensityGrid) {
	/* Initialize the volume texture */
	Output.Source.Init(VTEX_X, VTEX_Y, VTEX_Z, 1, TSF_G8, nullptr);

	/* Create a grid accessor and lock the volume texture data */
	const openvdb::FloatGrid::ConstAccessor Accessor = DensityGrid.getConstAccessor();
	uint8* TextureData = Output.Source.LockMip(0);

	/* Move the density data into a texture */
	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				/* Sample the density grid */
				const float Density = Accessor.getValue(openvdb::Coord(x, y, z));

				/* Calculate the texture index and write our density data */
				const int32 index = x + (y * VTEX_X) + (z * VTEX_X * VTEX_Y);
				TextureData[index] = (uint8)Remap(Density, 0.0f, 1.0f, 0.0f, 255.0f);
			}
		}
	}

	/* Unlock the volume texture data */
	Output.Source.UnlockMip(0);

	/* Set all the volume texture settings */
	Output.MipGenSettings = TMGS_NoMipmaps;
	Output.CompressionSettings = TC_Grayscale;
	Output.SRGB = false;
	Output.Filter = TF_Bilinear;
	Output.AddressMode = TA_Wrap;

	/* Update the volume texture resource */
	Output.UpdateResource();
}

void CreateSDFTexture(UVolumeTexture& Output, const openvdb::FloatGrid& DensityGrid) {
	/* Initialize the volume texture */
	Output.Source.Init(VTEX_X, VTEX_Y, VTEX_Z, 1, TSF_G16, nullptr);

	/* Convert the density grid data to a signed distance field */
	const openvdb::FloatGrid::Ptr SdfGrid = openvdb::tools::fogToSdf(DensityGrid, 0.0001f);

	/* Create a grid accessor and lock the volume texture data */
	const openvdb::FloatGrid::ConstAccessor Accessor = SdfGrid->getConstAccessor();
	uint16* TextureData = (uint16*)Output.Source.LockMip(0);

	/* Move the SDF data into a texture */
	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				/* Sample the SDF grid */
				const float SDF = Accessor.getValue(openvdb::Coord(x, y, z));

				/* Calculate the texture index and write our SDF data */
				const int32 index = x + (y * VTEX_X) + (z * VTEX_X * VTEX_Y);
				TextureData[index] = (uint16)Remap(SDF, -32.0f, 512.0f, 0.0f, 65535.0f);
			}
		}
	}

	/* Unlock the volume texture data */
	Output.Source.UnlockMip(0);

	/* Set all the volume texture settings */
	Output.MipGenSettings = TMGS_NoMipmaps;
	Output.CompressionSettings = TC_Alpha;
	Output.SRGB = false;
	Output.Filter = TF_Bilinear;
	Output.AddressMode = TA_Wrap;

	/* Update the volume texture resource */
	Output.UpdateResource();
}

UVaporCloud* UCloudscapeFactory::CreateVolumeTextureFromVDB(const FString& Filename, UObject* InParent, FName InName, EObjectFlags Flags) {
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
	openvdb::GridBase::Ptr BaseGrid = File.getGrids()->front();
	for (uint32 i = 0; i < File.getGrids()->size(); ++i) {
		const openvdb::GridBase::Ptr Grid = File.getGrids()->at(i);
		if (Grid->getName() == "dimensional_profile") {
			BaseGrid = Grid;
		}
		UE_LOG(LogTemp, Warning, TEXT("VDB Grid: %s"), UTF8_TO_TCHAR(File.getGrids()->at(i)->getName().c_str()));
	}
	const openvdb::FloatGrid::Ptr DensityGrid = openvdb::gridPtrCast<openvdb::FloatGrid>(BaseGrid);

	/* Find the AABB of all the grids */
	openvdb::BBoxd WorldAABB = openvdb::BBoxd();
	for (uint32 i = 0; i < File.getGrids()->size(); ++i) {
		const openvdb::GridBase::Ptr Grid = File.getGrids()->at(i);
		const openvdb::CoordBBox AABB = Grid->evalActiveVoxelBoundingBox();
		WorldAABB.expand(openvdb::BBoxd(Grid->indexToWorld(AABB.min()), Grid->indexToWorld(AABB.max())));
	}

	/* Make sure our grid is a float grid */
	if (!DensityGrid) {
		UE_LOG(LogTemp, Error, TEXT("Grid in VDB file is not a float grid"));
		return nullptr;
	}

	/* Create new volume texture assets */
	UVaporCloud* CloudData = NewObject<UVaporCloud>(InParent, InName, Flags);
	CloudData->DensityField = NewObject<UVolumeTexture>(CloudData,
		*FString::Printf(TEXT("%s_DensityField"), *InName.ToString()));
	CloudData->SignedDistanceField = NewObject<UVolumeTexture>(CloudData,
		*FString::Printf(TEXT("%s_SignedDistanceField"), *InName.ToString()));
		
	/* Get the source volume textures */
	UVolumeTexture& DensityTexture = *CloudData->DensityField;
	UVolumeTexture& SDFTexture = *CloudData->SignedDistanceField;

	/* Resample the density field */
	const openvdb::FloatGrid::Ptr ResampledGrid = ResampleGrid(WorldAABB, *DensityGrid, VTEX_X, VTEX_Y, VTEX_Z);

	/* Create the volume textures from the resampled density field */
	CreateDensityTexture(DensityTexture, *ResampledGrid);
	CreateSDFTexture(SDFTexture, *ResampledGrid);

	File.close(); /* Finally close the VDB file, and return */
	return CloudData;
}

#endif

#undef LOCTEXT_NAMESPACE
