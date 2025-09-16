#pragma once

#include "CoreMinimal.h"
#include "RenderGraphUtils.h"
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterial.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "SceneRendererInterface.h"

/* Cloudscape render data. */
BEGIN_UNIFORM_BUFFER_STRUCT(FCloudscapeRenderData, )
	SHADER_PARAMETER(FVector3f, Position)
	SHADER_PARAMETER(FVector3f, Absorption)
	SHADER_PARAMETER(float, Density)
	SHADER_PARAMETER(FVector3f, SunDir)
	SHADER_PARAMETER(FVector3f, SunLuminance)
	SHADER_PARAMETER(float, MinStepSize)
	SHADER_PARAMETER(int, Method)
END_UNIFORM_BUFFER_STRUCT()

class FVaporExtension : public FSceneViewExtensionBase {
	FCloudscapeRenderData RenderData;
	FCriticalSection RenderDataLock;

public:
	FVaporExtension(const FAutoRegister& AutoRegister);

	virtual int32 GetPriority() const { return 1 << 16; };
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};

	/* Setup before rendering happens in here. */
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

	/* All the rendering happens in here. */
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;
};

// Custom Post Process Shader
class FCustomShader : public FGlobalShader {
public:
	DECLARE_GLOBAL_SHADER(FCustomShader)

	SHADER_USE_PARAMETER_STRUCT(FCustomShader, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FCloudscapeRenderData, Cloud)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
	END_SHADER_PARAMETER_STRUCT()

	// Basic shader initialization
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// Define environment variables used by compute shader
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) {
		OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
};


