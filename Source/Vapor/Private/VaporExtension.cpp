#include "VaporExtension.h"

#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "EngineUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "VaporComponent.h"
#include "Misc/Optional.h"

IMPLEMENT_GLOBAL_SHADER(FCloudShader, "/Plugins/Vapor/SphereVolumeCS.usf", "MainCS", SF_Compute);
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
	Data.Absorption = VaporInstance->GetComponent()->Absorption;
	Data.Density = VaporInstance->GetComponent()->Density;
	Data.MinStepSize = VaporInstance->GetComponent()->MinStepSize;
	Data.InnerStepSize = VaporInstance->GetComponent()->InnerStepSize;
	Data.StepSizeMult = VaporInstance->GetComponent()->StepSizeMult;
	Data.ExtinctionThreshold = VaporInstance->GetComponent()->ExtinctionThreshold;

	FScopeLock Lock(&RenderDataLock);
	RenderData = MoveTemp(Data);
}

void FVaporExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) {
	/* Check if our extension is toggled ON */
	if (CVarShaderOn.GetValueOnRenderThread() == 0) return;

	/* Get the global shader map from our scene view */
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InView.Family->GetFeatureLevel());

	/* Create the worley noise texture if it hasn't been created yet */
	const bool FirstTime = WorleyTexture.IsValid() == false;
	if (FirstTime) {
		// Create 3D texture description
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create3D(TEXT("Worley Texture"))
			.SetExtent(512, 512)
			.SetDepth(512)
			.SetFormat(PF_R8)
			.SetFlags(ETextureCreateFlags::UAV | ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::UAVCompute);

		// Create the texture
		WorleyTexture = RHICreateTexture(Desc);
	}

	FRDGTextureRef NoiseTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(WorleyTexture, TEXT("Worley Texture")));

	/* Fill in the worley noise texture if it hasn't been filled yet */
	if (FirstTime) {
		RDG_EVENT_SCOPE(GraphBuilder, "Vapor Noise Generation");

		/* Allocate and fill-in the shader pass parameters */
		FNoiseShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FNoiseShader::FParameters>();
		PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NoiseTexture));

		/* Calculate the group count based on the viewport size */
		const FIntVector DispatchCount = FIntVector(128, 128, 128); // FComputeShaderUtils::GetGroupCount(FIntVector(512, 512, 512), FComputeShaderUtils::kGolden2DGroupSize);

		/* Load our custom shader from the global shader map */
		TShaderMapRef<FNoiseShader> ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(GraphBuilder,
			RDG_EVENT_NAME("Worley Noise Generation Pass"),
			ComputeShader, PassParameters, DispatchCount);
	}

	/* Start the render graph event scope */
	RDG_EVENT_SCOPE(GraphBuilder, "Vapor Render Pass");

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
		PassParameters->Cloud = TUniformBufferRef<FCloudscapeRenderData>::CreateUniformBufferImmediate(RenderData, EUniformBufferUsage::UniformBuffer_SingleFrame);
	}
	PassParameters->View = InView.ViewUniformBuffer;
	PassParameters->WorleyNoise = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(NoiseTexture));
	PassParameters->SceneColor = SceneColor;
	PassParameters->SceneDepth = SceneDepth;
	PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

	/* Calculate the group count based on the viewport size */
	const FIntVector DispatchCount = FComputeShaderUtils::GetGroupCount(ViewSize, FComputeShaderUtils::kGolden2DGroupSize);

	/* Load our custom shader from the global shader map */
	TShaderMapRef<FCloudShader> ComputeShader(GlobalShaderMap);

	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("Vapor Cloud Rendering %dx%d", ViewSize.X, ViewSize.Y),
		ComputeShader, PassParameters, DispatchCount);

	/* Finally copy our output texture back onto the scene color texture */
	AddCopyTexturePass(GraphBuilder, OutputTexture, SceneColor);
}
