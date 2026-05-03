// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RendererPass.h"
#include "Engine/Graphics/RenderGraph/RenderGraphPass.h"

/// <summary>
/// Eye adaptation effect based on color buffer luminance.
/// </summary>
class EyeAdaptationPass : public RendererPass<EyeAdaptationPass>, public RenderGraphComputePass
{
private:

    AssetReference<Shader> _shader;
    GPUPipelineState* _psManual = nullptr;
    GPUPipelineState* _psLuminanceMap = nullptr;
    GPUPipelineState* _psBlendLuminance = nullptr;
    GPUPipelineState* _psApplyLuminance = nullptr;
    GPUPipelineState* _psHistogram = nullptr;
    bool _canUseHistogram;

    // RenderGraph resources
    RenderGraphTextureRef _colorBufferRef;
    RenderContext* _renderContext = nullptr;

public:

    EyeAdaptationPass();

    /// <summary>
    /// Performs the eye adaptation effect.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="colorBuffer">The input and output color buffer to apply eye adaptation effect to it.</param>
    void Render(RenderContext& renderContext, GPUTexture* colorBuffer);

    // [RenderGraphPass]
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(GPUContext* context) override;

private:

#if COMPILE_WITH_DEV_ENV
    void OnShaderReloading(Asset* obj)
    {
        _psManual->ReleaseGPU();
        _psLuminanceMap->ReleaseGPU();
        _psBlendLuminance->ReleaseGPU();
        _psApplyLuminance->ReleaseGPU();
        _psHistogram->ReleaseGPU();
        invalidateResources();
    }
#endif

public:

    // [RendererPass]
    String ToString() const override;
    bool Init() override;
    void Dispose() override;

protected:

    // [RendererPass]
    bool setupResources() override;
};
