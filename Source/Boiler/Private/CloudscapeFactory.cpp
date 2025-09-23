#include "CloudscapeFactory.h"

#if WITH_EDITOR

#include "Engine/VolumeTexture.h"

/* OpenVDB Includes */
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
constexpr uint32 VTEX_X = 64;
constexpr uint32 VTEX_Y = 64;
constexpr uint32 VTEX_Z = 64;

/** @brief Remap an input value in an input range to an output range. */
float Remap(float Value, float InMin, float InMax, float OutMin, float OutMax) {
	const float Range = InMax - InMin;
	const float Norm = (Value - InMin) / Range;
	const float Clamped = (Norm < 0.0f) ? 0.0f : (Norm > 1.0f) ? 1.0f : Norm;
	return OutMin + (Clamped * (OutMax - OutMin));
}

struct StepInfo {
	openvdb::Vec3d Min;
	double StepSize;
};

/** @brief Calculate the grid step size for re-sampling. */
StepInfo GridStepInfo(const openvdb::FloatGrid& Grid) {
	/* Get the extent of the grid in world space */
	const openvdb::math::Transform GridTransform = Grid.transform();
	const openvdb::CoordBBox AABB = Grid.evalActiveVoxelBoundingBox();
	const openvdb::Vec3d GridMin = GridTransform.indexToWorld(AABB.min());
	const openvdb::Vec3d GridExtent = GridTransform.indexToWorld(AABB.max()) - GridMin;

	/* Calculate the step size along each axis */
	const double StepX = GridExtent.x() / VTEX_X;
	const double StepY = GridExtent.y() / VTEX_Y;
	const double StepZ = GridExtent.z() / VTEX_Z;
	const double StepSize = std::max(std::max(StepX, StepY), StepZ);
	return StepInfo(GridMin, StepSize);
}

/** @brief Re-sample a VDB float grid as density field. */
void ResampleDensityField(UVolumeTexture& Output, openvdb::FloatGrid& Input) {
	/* Calculate the step size */
	const StepInfo StepInfo = GridStepInfo(Input);

	/* Pre-filter the input grid for down-sampling */
	openvdb::tools::Filter<openvdb::FloatGrid> Filter(Input);
	Filter.gaussian(StepInfo.StepSize, 1);

	/* Create a grid sampler and lock the volume texture data */
	openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::BoxSampler> sampler(Input);
	uint8* TextureData = Output.Source.LockMip(0);

	/* Re-sample the density grid */
	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				const openvdb::Vec3d WorldPos(
					StepInfo.Min.x() + x * StepInfo.StepSize,
					StepInfo.Min.y() + y * StepInfo.StepSize,
					StepInfo.Min.z() + z * StepInfo.StepSize
				);

				/* Sample the density grid */
				const float Density = sampler.wsSample(WorldPos);

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

/** @brief Re-sample a VDB float grid as signed distance field. */
void ResampleSignedDistanceField(UVolumeTexture& Output, openvdb::FloatGrid& Input) {
	/* Calculate the step size */
	const StepInfo StepInfo = GridStepInfo(Input);

	const openvdb::Vec3d EndPos(
		StepInfo.Min.x() + VTEX_X * StepInfo.StepSize,
		StepInfo.Min.y() + VTEX_Y * StepInfo.StepSize,
		StepInfo.Min.z() + VTEX_Z * StepInfo.StepSize
	);

	/* Convert the density grid data to a signed distance field */
	openvdb::Vec3d EndIdx = Input.worldToIndex(EndPos);
	auto accessor = Input.getAccessor();

	openvdb::Coord coord;
	for (coord.x() = StepInfo.Min.x(); coord.x() <= EndIdx.x(); ++coord.x()) {
		for (coord.y() = StepInfo.Min.y(); coord.y() <= EndIdx.y(); ++coord.y()) {
			for (coord.z() = StepInfo.Min.z(); coord.z() <= EndIdx.z(); ++coord.z()) {
				if (!accessor.isValueOn(coord)) {
					accessor.setValue(coord, 0.0f);
				}
			}
		}
	}

	const openvdb::FloatGrid::Ptr SdfGrid = openvdb::tools::fogToSdf(Input, 0.0001f);
	
	/* Create a grid sampler and lock the volume texture data */
	openvdb::tools::GridSampler<openvdb::FloatGrid, openvdb::tools::QuadraticSampler> sampler(*SdfGrid);
	uint16* TextureData = (uint16*)Output.Source.LockMip(0);

	/* Re-sample the SDF grid */
	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				const openvdb::Vec3d WorldPos(
					StepInfo.Min.x() + x * StepInfo.StepSize,
					StepInfo.Min.y() + y * StepInfo.StepSize,
					StepInfo.Min.z() + z * StepInfo.StepSize
				);

				/* Sample the SDF grid */
				const float SDF = sampler.wsSample(WorldPos);

				/* Calculate the texture index and write our distance data */
				const int32 index = x + (y * VTEX_X) + (z * VTEX_X * VTEX_Y);
				TextureData[index] = (uint16)Remap(SDF, -256.0f, 4096.0f, 0.0f, 65535.0f);
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
	BaseGrid->setTransform(openvdb::math::Transform::createLinearTransform());
	const openvdb::FloatGrid::Ptr DensityGrid = openvdb::gridPtrCast<openvdb::FloatGrid>(BaseGrid);

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
	UVolumeTexture& DensityField = *CloudData->DensityField;
	UVolumeTexture& SignedDistanceField = *CloudData->SignedDistanceField;

	/* Initialize the volume textures */
	DensityField.Source.Init(VTEX_X, VTEX_Y, VTEX_Z, 1, TSF_G8, nullptr);
	SignedDistanceField.Source.Init(VTEX_X, VTEX_Y, VTEX_Z, 1, TSF_G16, nullptr);

	/* Re-sample the density field */
	ResampleDensityField(DensityField, *DensityGrid);
	ResampleSignedDistanceField(SignedDistanceField, *DensityGrid);

	File.close(); /* Finally close the VDB file, and return */
	return CloudData;
}

#endif

#undef LOCTEXT_NAMESPACE
