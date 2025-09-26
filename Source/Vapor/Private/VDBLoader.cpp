#include "VDBLoader.h"

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "Interfaces/IPluginManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

THIRD_PARTY_INCLUDES_START
#include "openvdb/openvdb.h"
#include "openvdb/Grid.h"
THIRD_PARTY_INCLUDES_END

constexpr uint32 NOISE_RESOLUTION_X = 128;
constexpr uint32 NOISE_RESOLUTION_Y = 128;
constexpr uint32 NOISE_RESOLUTION_Z = 128;

FTextureRHIRef LoadAlligatorNoise() {
	/* Init OpenVDB */
	openvdb::initialize();

	/* Get the VDB file path */
	const FString VDBFilePath = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("Vapor"))->GetBaseDir(),
		TEXT("Resources/AlligatorNoise.vdb")
	);

	/* Create the OpenVDB file loader */
	openvdb::io::File File(TCHAR_TO_UTF8(*VDBFilePath));

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

	/* Get the noise grid pointers */
	const openvdb::FloatGrid::Ptr HFAlligator = openvdb::gridPtrCast<openvdb::FloatGrid>(File.getGrids()->at(0)); // hf_alligator
	const openvdb::FloatGrid::Ptr HFCurlyWorley = openvdb::gridPtrCast<openvdb::FloatGrid>(File.getGrids()->at(1)); // hf_curly_worley
	const openvdb::FloatGrid::Ptr LFAlligator = openvdb::gridPtrCast<openvdb::FloatGrid>(File.getGrids()->at(2)); // lf_alligator
	const openvdb::FloatGrid::Ptr LFCurlyWorley = openvdb::gridPtrCast<openvdb::FloatGrid>(File.getGrids()->at(3)); // lf_curly_worley

	/* Get the noise accessors */
	const openvdb::FloatGrid::ConstAccessor AccessorR = HFAlligator->getConstAccessor();
	const openvdb::FloatGrid::ConstAccessor AccessorG = HFCurlyWorley->getConstAccessor();
	const openvdb::FloatGrid::ConstAccessor AccessorB = LFAlligator->getConstAccessor();
	const openvdb::FloatGrid::ConstAccessor AccessorA = LFCurlyWorley->getConstAccessor();

	/* Create new volume texture asset */
	// UVolumeTexture* Texture = NewObject<UVolumeTexture>(GetTransientPackage(), TEXT("Vapor_AlligatorNoise"));

	/* Initialize the volume texture */
	// Texture->Source.Init(NOISE_RESOLUTION_X, NOISE_RESOLUTION_Y, NOISE_RESOLUTION_Z, 1, TSF_BGRA8, nullptr);
	// uint8* TextureData = Texture->Source.LockMip(0);
	uint8* SourceData = new uint8[NOISE_RESOLUTION_X * NOISE_RESOLUTION_Y * NOISE_RESOLUTION_Z * 4];

	for (uint32 z = 0; z < NOISE_RESOLUTION_Z; ++z) {
		for (uint32 y = 0; y < NOISE_RESOLUTION_Y; ++y) {
			for (uint32 x = 0; x < NOISE_RESOLUTION_X; ++x) {
				/* Sample the noise grid */
				const float ValueR = AccessorR.getValue(openvdb::Coord(x, y, z));
				const float ValueG = AccessorG.getValue(openvdb::Coord(x, y, z));
				const float ValueB = AccessorB.getValue(openvdb::Coord(x, y, z));
				const float ValueA = AccessorA.getValue(openvdb::Coord(x, y, z));

				/* Set the value inside our resampled grid */
				const int32 Index = x + (y * NOISE_RESOLUTION_X) + (z * NOISE_RESOLUTION_X * NOISE_RESOLUTION_Y);
				SourceData[Index * 4 + 0] = (uint8)(ValueB * 255.0f);
				SourceData[Index * 4 + 1] = (uint8)(ValueG * 255.0f);
				SourceData[Index * 4 + 2] = (uint8)(ValueR * 255.0f);
				SourceData[Index * 4 + 3] = (uint8)(ValueA * 255.0f);
			}
		}
	}

	FTextureRHIRef Texture;

	ENQUEUE_RENDER_COMMAND(CreateNoiseTexture)(
	[&Texture, SourceData](FRHICommandListImmediate& RHICmdList) {
		/* Create the noise texture */
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create3D(TEXT("Alligator Texture"))
			.SetExtent(NOISE_RESOLUTION_X, NOISE_RESOLUTION_Y)
			.SetDepth(NOISE_RESOLUTION_Z)
			.SetFormat(PF_B8G8R8A8)
			.SetFlags(ETextureCreateFlags::ShaderResource);
		Texture = RHICreateTexture(Desc);

		/* Upload the noise data */
		FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, NOISE_RESOLUTION_X, NOISE_RESOLUTION_Y, NOISE_RESOLUTION_Z);
		RHIUpdateTexture3D(Texture, 0, UpdateRegion, NOISE_RESOLUTION_X * 4, NOISE_RESOLUTION_X * NOISE_RESOLUTION_Y * 4, SourceData);
		delete[] SourceData;
	});

	File.close(); /* Finally close the VDB file, and return */
	return Texture;
}