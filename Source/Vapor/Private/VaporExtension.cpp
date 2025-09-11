#include "VaporExtension.h"

#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "VaporComponent.h"
#include "Misc/Optional.h"

IMPLEMENT_GLOBAL_SHADER(FCustomShader, "/Plugins/Vapor/PostProcessCS.usf", "MainCS", SF_Compute);

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
	if (!VaporInstance || !SunInstance) return;

	/* Fill in the render data struct */
	FCloudscapeRenderData Data {};
	Data.Position = (FVector3f)VaporInstance->GetActorLocation();
	Data.SunDir = -(FVector3f)SunInstance->GetComponent()->GetDirection();

	FScopeLock Lock(&RenderDataLock);
	RenderData = MoveTemp(Data);
}

void FVaporExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) {
	/* Check if our extension is toggled ON */
	if (CVarShaderOn.GetValueOnRenderThread() == 0) return;
	
	/* Start the render graph event scope */
	RDG_EVENT_SCOPE(GraphBuilder, "Vapor Render Pass");

	/* Get the global shader map from our scene view */
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InView.Family->GetFeatureLevel());

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
	FCustomShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FCustomShader::FParameters>();
	{
		FScopeLock Lock(&RenderDataLock);
		PassParameters->Cloud = RenderData;
	}
	PassParameters->View = InView.ViewUniformBuffer;
	PassParameters->SceneColor = SceneColor;
	PassParameters->SceneDepth = SceneDepth;
	PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

	/* Calculate the group count based on the viewport size */
	const FIntVector DispatchCount = FComputeShaderUtils::GetGroupCount(ViewSize, FComputeShaderUtils::kGolden2DGroupSize);

	/* Load our custom shader from the global shader map */
	TShaderMapRef<FCustomShader> ComputeShader(GlobalShaderMap);

	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("Custom SceneViewExtension Post Processing CS Shader %dx%d", ViewSize.X, ViewSize.Y),
		ComputeShader, PassParameters, DispatchCount);

	/* Finally copy our output texture back onto the scene color texture */
	AddCopyTexturePass(GraphBuilder, OutputTexture, SceneColor);
}
