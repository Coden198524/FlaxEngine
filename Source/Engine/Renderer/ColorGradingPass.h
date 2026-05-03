// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RendererPass.h"
#include "Engine/Graphics/GPUPipelineStatePermutations.h"
#include "Engine/Graphics/RenderGraph/RenderGraphPass.h"

/// <summary>
/// Color Grading and Tone Mapping rendering service. Generates HDR LUT for PostFx pass.
/// </summary>
class ColorGradingPass : public RendererPass<ColorGradingPass>, public RenderGraphRasterPass
{
private:
    int32 _use3D = -1;
    AssetReference<Shader> _shader;
    GPUPipelineStatePermutationsPs<4> _psLut;

    // RenderGraph resources
    RenderGraphTextureRef _lutOutputRef;
    RenderContext* _renderContext = nullptr;

public:
    /// <summary>
    /// Renders Look Up table with color grading parameters mixed in.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <returns>Allocated temp render target with a rendered LUT - cached within Render Buffers, released automatically.</returns>
    GPUTexture* RenderLUT(RenderContext& renderContext);

    // [RenderGraphPass]
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(GPUContext* context) override;

private:
#if COMPILE_WITH_DEV_ENV
    uint64 _reloadedFrame = 0;
    void OnShaderReloading(Asset* obj);
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
