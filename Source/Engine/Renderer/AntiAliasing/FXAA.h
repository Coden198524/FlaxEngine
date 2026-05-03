// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "../RendererPass.h"
#include "Engine/Graphics/RenderGraph/RenderGraphPass.h"
#include "Engine/Graphics/GPUPipelineStatePermutations.h"

/// <summary>
/// Fast-Approximate Anti-Aliasing effect.
/// </summary>
class FXAA : public RendererPass<FXAA>, public RenderGraphComputePass
{
private:
    AssetReference<Shader> _shader;
    GPUPipelineStatePermutationsPs<static_cast<int32>(Quality::MAX)> _psFXAA;

    // RenderGraph resources
    RenderGraphTextureRef _inputRef;
    RenderGraphTextureRef _outputRef;
    RenderContext* _renderContext = nullptr;
    GPUTexture* _input = nullptr;
    GPUTextureView* _output = nullptr;

public:
    /// <summary>
    /// Performs AA pass rendering for the input task.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="input">The source render target.</param>
    /// <param name="output">The result render target.</param>
    void Render(RenderContext& renderContext, GPUTexture* input, GPUTextureView* output);

    // [RenderGraphPass]
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(GPUContext* context) override;

private:
#if COMPILE_WITH_DEV_ENV
    void OnShaderReloading(Asset* obj)
    {
        _psFXAA.Release();
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
