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
					WorldAABB.min().x() + x * StepSize,
					WorldAABB.min().y() + z * StepSize,
					WorldAABB.min().z() + y * StepSize
				));

				/* Set the value inside our resampled grid */
				Accessor.setValue(openvdb::Coord(x, y, z), Value);
			}
		}
	}

	return Resampled;
}

void CreateDensityTexture(UVolumeTexture& Output, const openvdb::FloatGrid& DensityGrid) {
	/* Initialize the volume source texture data */
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

/* Expand a 5 bit number to the range of 8 bits. (using flooring to avoid a larger fraction) */
int SafeExpand5b(const int bits) { return (bits << 3); }
/* Expand a 6 bit number to the range of 8 bits. (using flooring to avoid a larger fraction) */
int SafeExpand6b(const int bits) { return (bits << 2); }

struct FEndpoint {
	int r = 0, g = 0, b = 0;
	FEndpoint() = default;
	FEndpoint(int r, int g, int b) : r(r), g(g), b(b) {}
	void Min(const FEndpoint& Endpoint) { 
		r = std::min(r, Endpoint.r); 
		g = std::min(g, Endpoint.g);
		b = std::min(b, Endpoint.b);
	}
	void Max(const FEndpoint& Endpoint) {
		r = std::max(r, Endpoint.r);
		g = std::max(g, Endpoint.g);
		b = std::max(b, Endpoint.b);
	}
	FEndpoint Expanded() const {
		return FEndpoint(SafeExpand5b(r), SafeExpand6b(g), SafeExpand5b(b));
	}
	float ToScalar(const float RedMult, const float GreenMult, const float BlueMult) const {
		return ((float)r / 255.0f) * RedMult + ((float)g / 255.0f) * GreenMult + ((float)b / 255.0f) * BlueMult;
	}
};

FEndpoint ScalarToEndpoint(const float Scalar, const float RedMult, const float GreenMult, const float BlueMult) {
	/* Red channel endpoint */
	const int RedIndex = (int)(Scalar / RedMult * 31.0f);
	const float Red = RedIndex * (1.0f / 31.0f) * RedMult;
	const float RedResidual = Scalar - Red;

	/* Green channel endpoint */
	const int GreenIndex = (int)(RedResidual / GreenMult * 63.0f);
	const float Green = GreenIndex * (1.0f / 63.0f) * GreenMult;
	const float GreenResidual = RedResidual - Green;

	/* Blue channel endpoint */
	const int BlueIndex = (int)(GreenResidual / BlueMult * 31.0f);

	return FEndpoint(RedIndex, GreenIndex, BlueIndex);
}

struct FMidpoints {
	FEndpoint a{}, b{};
	FMidpoints(FEndpoint a, FEndpoint b) : a(a), b(b) {}
};

/* Find the maximum mid points for 4 endpoint BC1 */
FMidpoints MaxMidpoints(FEndpoint min, FEndpoint max) {
	const int min_r = SafeExpand5b(min.r), max_r = SafeExpand5b(max.r);
	const int min_g = SafeExpand6b(min.g), max_g = SafeExpand6b(max.g);
	const int min_b = SafeExpand5b(min.b), max_b = SafeExpand5b(max.b);

	int max_r2 = 0, max_r3 = 0;
	int max_g2 = 0, max_g3 = 0;
	int max_b2 = 0, max_b3 = 0;

	{ // Intel 4-point BC1
		const int r2 = ((256 - 85) * min_r + 85 * max_r + 128) >> 8;
		const int r3 = ((256 - 171) * min_r + 171 * max_r + 128) >> 8;
		const int g2 = ((256 - 85) * min_g + 85 * max_g + 128) >> 8;
		const int g3 = ((256 - 171) * min_g + 171 * max_g + 128) >> 8;
		const int b2 = ((256 - 85) * min_b + 85 * max_b + 128) >> 8;
		const int b3 = ((256 - 171) * min_b + 171 * max_b + 128) >> 8;
		max_r2 = std::max(max_r2, r2);
		max_r3 = std::max(max_r3, r3);
		max_g2 = std::max(max_g2, g2);
		max_g3 = std::max(max_g3, g3);
		max_b2 = std::max(max_b2, b2);
		max_b3 = std::max(max_b3, b3);
	}

	{ // AMD 4-point BC1
		const int r2 = ((64 - 21) * min_r + 21 * max_r + 32) >> 6;
		const int r3 = ((64 - 43) * min_r + 43 * max_r + 32) >> 6;
		const int g2 = ((64 - 21) * min_g + 21 * max_g + 32) >> 6;
		const int g3 = ((64 - 43) * min_g + 43 * max_g + 32) >> 6;
		const int b2 = ((64 - 21) * min_b + 21 * max_b + 32) >> 6;
		const int b3 = ((64 - 43) * min_b + 43 * max_b + 32) >> 6;
		max_r2 = std::max(max_r2, r2);
		max_r3 = std::max(max_r3, r3);
		max_g2 = std::max(max_g2, g2);
		max_g3 = std::max(max_g3, g3);
		max_b2 = std::max(max_b2, b2);
		max_b3 = std::max(max_b3, b3);
	}

	{ // NVIDIA 4-point BC1
		const int r2 = ((2 * min.r + max.r) * 22) >> 3;
		const int r3 = ((2 * max.r + min.r) * 22) >> 3;
		const int ae = (min.g << 2) | (min.g >> 4);
		const int be = (max.g << 2) | (max.g >> 4);
		const int diff = be - ae;
		const int scaled_diff = 80 * diff + (diff >> 2);
		const int g2 = ae + ((128 + scaled_diff) >> 8);
		const int g3 = be + ((128 - scaled_diff) >> 8);
		const int b2 = ((2 * min.b + max.b) * 22) >> 3;
		const int b3 = ((2 * max.b + min.b) * 22) >> 3;
		max_r2 = std::max(max_r2, r2);
		max_r3 = std::max(max_r3, r3);
		max_g2 = std::max(max_g2, g2);
		max_g3 = std::max(max_g3, g3);
		max_b2 = std::max(max_b2, b2);
		max_b3 = std::max(max_b3, b3);
	}

	return FMidpoints(FEndpoint(max_r2, max_g2, max_b2), FEndpoint(max_r3, max_g3, max_b3));
}

void CreateSDFTexture(UVolumeTexture& Output, const openvdb::FloatGrid& DensityGrid) {
#if 0
	/* Initialize the volume source texture data */
	Output.Source.Init(VTEX_X, VTEX_Y, VTEX_Z, 1, TSF_BGRA8, nullptr);

	/* Convert the density grid data to a signed distance field */
	const openvdb::FloatGrid::Ptr SdfGrid = openvdb::tools::fogToSdf(DensityGrid, 0.0001f);

	/* Create a grid accessor and lock the volume texture data */
	const openvdb::FloatGrid::ConstAccessor Accessor = SdfGrid->getConstAccessor();
	uint8* TextureData = (uint8*)Output.Source.LockMip(0);

	/* BC1 encoding/decoding coefficients */
	const float BC1R = 1.0f, RCP_BC1R = 1.0f / BC1R;
	const float BC1G = 0.03529415f, RCP_BC1G = 1.0f / BC1G;
	const float BC1B = 0.00069204f, RCP_BC1B = 1.0f / BC1B;

	struct TexelBlock {
		FEndpoint MinEndpoint = FEndpoint(255, 255, 255), MaxEndpoint = FEndpoint(0, 0, 0);
	};

	const uint32 BlockCount = VTEX_X * VTEX_Y * VTEX_Z / 16;
	TexelBlock* Blocks = new TexelBlock[BlockCount] {};

	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				/* Sample the SDF grid */
				const float SDF = Accessor.getValue(openvdb::Coord(x, y, z));

				/* Convert the floating point SDF to UNorm R5G6B5 */
				const float SDF01 = Remap(SDF, -32.0f, 512.0f, 0.0f, 1.0f);

				const FEndpoint Endpoint = ScalarToEndpoint(SDF01, BC1R, BC1G, BC1B);

				if (Endpoint.Expanded().ToScalar(BC1R, BC1G, BC1B) > SDF01) {
					UE_LOG(LogTemp, Warning, TEXT("[1] Value was too large! (%f -> %f)"), SDF01, Endpoint.Expanded().ToScalar(BC1R, BC1G, BC1B));
				}

				/* Update the block value range */
				const int32 BlockIndex = (x >> 2) + ((y >> 2) * (VTEX_X >> 2)) + (z * (VTEX_X >> 2) * (VTEX_Y >> 2));
				TexelBlock& Block = Blocks[BlockIndex];
				Block.MinEndpoint.Min(Endpoint);
				Block.MaxEndpoint.Max(Endpoint);
			}
		}
	}

	/* Move the SDF data into an RGB texture for BC1 compression */
	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				/* Sample the SDF grid */
				const float SDF = Accessor.getValue(openvdb::Coord(x, y, z));
				const float SDF01 = Remap(SDF, -32.0f, 512.0f, 0.0f, 1.0f);
				// const FEndpoint Encoded = ScalarToEndpoint(SDF01, BC1R, BC1G, BC1B).Expanded();

				/* Get the current block */
				const int32 BlockIndex = (x >> 2) + ((y >> 2) * (VTEX_X >> 2)) + (z * (VTEX_X >> 2) * (VTEX_Y >> 2));
				const TexelBlock& Block = Blocks[BlockIndex];

				const FMidpoints Midpoints = MaxMidpoints(Block.MinEndpoint, Block.MaxEndpoint);
				const FEndpoint Endpoints[4] = { Block.MinEndpoint.Expanded(), Midpoints.a, Midpoints.b, Block.MaxEndpoint.Expanded() };
				
				FEndpoint Encoded = Endpoints[0];
				for (int i = 1; i < 4; ++i) {
					const float EncodedScalar = Encoded.ToScalar(BC1R, BC1G, BC1B);
					const float NewScalar = Endpoints[i].ToScalar(BC1R, BC1G, BC1B);
					if (NewScalar < SDF01 && NewScalar > EncodedScalar) {
						Encoded = Endpoints[i];
					}
				}

				//if (Encoded.ToScalar(BC1R, BC1G, BC1B) > SDF01) {
				//	UE_LOG(LogTemp, Warning, TEXT("[2] Value was too large! (%f -> %f)"), SDF01, Encoded.ToScalar(BC1R, BC1G, BC1B));
				//}

				/* Calculate the texture index and write our SDF data */
				const int32 index = x + (y * VTEX_X) + (z * VTEX_X * VTEX_Y);
				TextureData[index * 4 + 0] = (uint8)Encoded.b;
				TextureData[index * 4 + 1] = (uint8)Encoded.g;
				TextureData[index * 4 + 2] = (uint8)Encoded.r;
				TextureData[index * 4 + 3] = 0xFFu;
			}
		}
	}

	delete[] Blocks;

	/* Unlock the volume texture data */
	Output.Source.UnlockMip(0);

	/* Set all the volume texture settings */
	Output.CompressionForceAlpha = false;
	Output.CompressionNoAlpha = true;
	Output.MipGenSettings = TMGS_NoMipmaps;
	Output.CompressionSettings = TC_Default; /* BC1 (R5G6B5) */
	Output.SRGB = false;
	Output.Filter = TF_Bilinear;
	Output.AddressMode = TA_Wrap;

	/* Update the volume texture resource */
	Output.UpdateResource();
#else
	/* Initialize the volume source texture data */
	Output.Source.Init(VTEX_X, VTEX_Y, VTEX_Z, 1, TSF_R32F, nullptr);

	/* Convert the density grid data to a signed distance field */
	const openvdb::FloatGrid::Ptr SdfGrid = openvdb::tools::fogToSdf(DensityGrid, 0.0001f);

	/* Create a grid accessor and lock the volume texture data */
	const openvdb::FloatGrid::ConstAccessor Accessor = SdfGrid->getConstAccessor();
	float* TextureData = (float*)Output.Source.LockMip(0);

	/* Move the SDF data into an RGB texture for BC1 compression */
	for (int32 z = 0; z < VTEX_Z; ++z) {
		for (int32 y = 0; y < VTEX_Y; ++y) {
			for (int32 x = 0; x < VTEX_X; ++x) {
				/* Sample the SDF grid */
				const float SDF = Accessor.getValue(openvdb::Coord(x, y, z));
				const float SDF01 = Remap(SDF, -32.0f, 512.0f, 0.0f, 1.0f);
				///const FEndpoint Encoded = ScalarToEndpoint(SDF01, BC1R, BC1G, BC1B).Expanded();

				/* Calculate the texture index and write our SDF data */
				const int32 index = x + (y * VTEX_X) + (z * VTEX_X * VTEX_Y);
				TextureData[index] = SDF01; // (uint16)(SDF01 * 65535.0f);
			}
		}
	}

	/* Unlock the volume texture data */
	Output.Source.UnlockMip(0);

	/* Set all the volume texture settings */
	Output.CompressionForceAlpha = false;
	Output.CompressionNoAlpha = true;
	Output.MipGenSettings = TMGS_NoMipmaps;
	Output.CompressionSettings = TC_Alpha;
	Output.SRGB = false;
	Output.Filter = TF_Bilinear;
	Output.AddressMode = TA_Wrap;

	/* Update the volume texture resource */
	Output.UpdateResource();
#endif
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
