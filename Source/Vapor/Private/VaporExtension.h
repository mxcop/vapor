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
	SHADER_PARAMETER(float, InnerStepSize)
	SHADER_PARAMETER(float, StepSizeMult)
	SHADER_PARAMETER(float, ExtinctionThreshold)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, DensityTexture)
END_UNIFORM_BUFFER_STRUCT()

class FVaporExtension : public FSceneViewExtensionBase {
	// Frame Render Data
	FCloudscapeRenderData RenderData;
	FTextureResource* DensityTexture = nullptr;
	FCriticalSection RenderDataLock;

	// Worley Noise Texture
	FTextureRHIRef WorleyTexture;

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

// Cloud ray marching shader.
class FCloudShader : public FGlobalShader {
public:
	DECLARE_GLOBAL_SHADER(FCloudShader)

	SHADER_USE_PARAMETER_STRUCT(FCloudShader, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FCloudscapeRenderData, Cloud)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D, WorleyNoise)
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
		OutEnvironment.SetDefine(TEXT("THREADS_X"), 16);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 16);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
};

// Noise generation shader.
class FNoiseShader : public FGlobalShader {
public:
	DECLARE_GLOBAL_SHADER(FNoiseShader)

	SHADER_USE_PARAMETER_STRUCT(FNoiseShader, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, Output)
	END_SHADER_PARAMETER_STRUCT()

	// Basic shader initialization
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// Define environment variables used by compute shader
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) {
		OutEnvironment.SetDefine(TEXT("THREADS_X"), 4);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 4);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 4);
	}
};
