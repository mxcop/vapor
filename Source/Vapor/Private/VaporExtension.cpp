#include "VaporExtension.h"

#include "Engine/DirectionalLight.h"
#include "Engine/VolumeTexture.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "EngineUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "VaporComponent.h"
#include "Misc/Optional.h"
#include "VaporCloud.h"
#include "VDBLoader.h"

IMPLEMENT_GLOBAL_SHADER(FCloudShader, "/Plugins/Vapor/CloudMarchCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FNoiseShader, "/Plugins/Vapor/NoiseGenCS.usf", "MainCS", SF_Compute);

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FCloudscapeRenderData, "Cloud");

namespace {
	TAutoConsoleVariable<int32> CVarShaderOn(
		TEXT("r.Vapor"),
		0,
		TEXT("Enable Vapor Cloud Rendering \n")
		TEXT(" 0: OFF;")
		TEXT(" 1: ON."),
		ECVF_RenderThreadSafe);
}

enum ERenderTarget {
	ESceneColor  = 0, /* [0] "SceneColor" */
	EWorldNormal = 1, /* [1]  "GBufferA"  */
	EMaterial    = 2, /* [2]  "GBufferB"  */
	EAlbedo      = 3, /* [3]  "GBufferC"  */
	ECustom      = 4, /* [4]  "GBufferD"  */
};

FVaporExtension::FVaporExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {
	UE_LOG(LogTemp, Log, TEXT("Vapor: Custom SceneViewExtension registered"));
}

void FVaporExtension::BeginRenderViewFamily(FSceneViewFamily& ViewFamily) {
	/* Get the world from the scene */
	UWorld* World = ViewFamily.Scene->GetWorld();
	if (World == nullptr) return;

	/* Fetch actors from the scene */
	TActorIterator<AVapor> VaporInstance(World);
	TActorIterator<ADirectionalLight> SunInstance(World);
	TActorIterator<ASkyAtmosphere> SkyInstance(World);
	if (!VaporInstance || !SunInstance) return;

	/* Fill in the render data struct */
	FCloudscapeRenderData Data {};
	Data.Position = (FVector3f)VaporInstance->GetActorLocation();
	Data.SunDir = -(FVector3f)SunInstance->GetComponent()->GetDirection();
	Data.SunLuminance = (FVector3f)SunInstance->GetComponent()->GetColoredLightBrightness();
	if (SkyInstance) Data.SunLuminance *= (FVector3f)SkyInstance->GetComponent()->GetAtmosphereTransmitanceOnGroundAtPlanetTop(SunInstance->GetComponent());
	VaporInstance->GetComponent()->IntoRenderData(Data);

	{ /* Lock and update the render data */
		FScopeLock Lock(&RenderDataLock);
		DebugMode = VaporInstance->GetComponent()->Debug;
		RenderData = MoveTemp(Data);
	}

	/* Get the different textures from the cloud asset */
	if (VaporInstance->GetComponent()->CloudAsset) {
		DensityTexture = VaporInstance->GetComponent()->CloudAsset->DensityField->GetResource();
		if (DensityTexture == nullptr) DensityTexture = VaporInstance->GetComponent()->CloudAsset->DensityField->CreateResource();
		SDFTexture = VaporInstance->GetComponent()->CloudAsset->SignedDistanceField->GetResource();
		if (SDFTexture == nullptr) SDFTexture = VaporInstance->GetComponent()->CloudAsset->SignedDistanceField->CreateResource();
	}

	/* Initialize the noise texture if it's not initialized yet */
	if (NoiseTexture == nullptr) {
		NoiseTexture = LoadAlligatorNoise();
	}
}

void FVaporExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) {
	/* Check if our extension is toggled ON */
	if (CVarShaderOn.GetValueOnRenderThread() == 0) return;

	/* Get the global shader map from our scene view */
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InView.Family->GetFeatureLevel());

	/* Start the render graph event scope */
	RDG_EVENT_SCOPE(GraphBuilder, "Vapor Render Pass");

	/* Make sure the density texture is set */
	if (DensityTexture == nullptr || SDFTexture == nullptr) return;

	/* Convert the scene color texture to a screen pass texture */
	FRDGTexture* SceneColor = Inputs.SceneTextures->GetContents()->SceneColorTexture;
	FRDGTexture* SceneDepth = Inputs.SceneTextures->GetContents()->SceneDepthTexture;
	const FIntPoint ViewSize = SceneColor->Desc.Extent;
	
	/* Target texture creation info */
	FRDGTextureDesc OutputDesc {};
	OutputDesc = SceneColor->Desc;
	OutputDesc.Reset();
	OutputDesc.Flags |= TexCreate_UAV;
	OutputDesc.Flags &= ~(TexCreate_RenderTargetable | TexCreate_FastVRAM);
	const FLinearColor ClearColor(0., 0., 0., 0.);
	OutputDesc.ClearValue = FClearValueBinding(ClearColor);

	/* Create a target texture we can write into */
	const FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("Vapor Output"));

	/* Allocate and fill-in the shader pass parameters */
	FCloudShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FCloudShader::FParameters>();
	{
		FScopeLock Lock(&RenderDataLock);
		RenderData.DensityTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DensityTexture->GetTextureRHI(), TEXT("Density Texture")));
		RenderData.SDFTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SDFTexture->GetTextureRHI(), TEXT("SDF Texture")));
		PassParameters->Cloud = TUniformBufferRef<FCloudscapeRenderData>::CreateUniformBufferImmediate(RenderData, EUniformBufferUsage::UniformBuffer_SingleFrame);
	}
	PassParameters->View = InView.ViewUniformBuffer;
	PassParameters->Noise = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(NoiseTexture->GetResource()->GetTextureRHI(), TEXT("Noise Texture")));
	PassParameters->SceneColor = SceneColor;
	PassParameters->SceneDepth = SceneDepth;
	PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

	/* Calculate the group count based on the viewport size */
	const FIntVector DispatchCount = FComputeShaderUtils::GetGroupCount(ViewSize, FComputeShaderUtils::kGolden2DGroupSize);

	/* Set the permutation vector for the shader */
	FCloudShader::FPermutationDomain PermutationVector;
	PermutationVector.Set<FCloudShader::FDebugDim>(DebugMode);

	/* Load our custom shader from the global shader map */
	TShaderMapRef<FCloudShader> ComputeShader(GlobalShaderMap, PermutationVector);

	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("Vapor Cloud Rendering %dx%d", ViewSize.X, ViewSize.Y),
		ComputeShader, PassParameters, DispatchCount);

	/* Finally copy our output texture back onto the scene color texture */
	AddCopyTexturePass(GraphBuilder, OutputTexture, SceneColor);
}
