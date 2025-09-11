#include "VaporExtension.h"

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

	/* Find the first Vapor actor in the scene, use it */
	for (TActorIterator<AVapor> It(World); It; ++It) {
		FCloudscapeRenderData Data {};
		Data.Position = (FVector3f)It->GetActorLocation();
		FScopeLock Lock(&RenderDataLock);
		RenderData = MoveTemp(Data);
		break;
	}
}

void FVaporExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& SceneView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) {
	/* Check if our extension is toggled ON */
	if (CVarShaderOn.GetValueOnRenderThread() == 0) return;
	
	/* Start the render graph event scope */
	RDG_EVENT_SCOPE(GraphBuilder, "Vapor Render Pass");

	/* Get the global shader map from our scene view */
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(SceneView.Family->GetFeatureLevel());

	/* Convert the scene color texture to a screen pass texture */
	const FScreenPassTexture SceneColor = FScreenPassTexture(RenderTargets.Output[EWorldNormal].GetTexture());
	const FScreenPassTextureViewport SceneColorViewport(SceneColor);
	
	/* Target texture creation info */
	FRDGTextureDesc OutputDesc {};
	OutputDesc = SceneColor.Texture->Desc;
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
		PassParameters->Position = RenderData.Position;
	}
	PassParameters->View = SceneView.ViewUniformBuffer;
	PassParameters->OriginalSceneColor = SceneColor.Texture;
	PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
	PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

	/* Calculate the group count based on the viewport size */
	const FIntPoint ViewSize = SceneColor.ViewRect.Size();
	const FIntVector DispatchCount = FComputeShaderUtils::GetGroupCount(ViewSize, FComputeShaderUtils::kGolden2DGroupSize);

	/* Load our custom shader from the global shader map */
	TShaderMapRef<FCustomShader> ComputeShader(GlobalShaderMap);

	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("Custom SceneViewExtension Post Processing CS Shader %dx%d", ViewSize.X, ViewSize.Y),
		ComputeShader, PassParameters, DispatchCount);

	/* Finally copy our output texture back onto the scene color texture */
	AddCopyTexturePass(GraphBuilder, OutputTexture, SceneColor.Texture);
}
