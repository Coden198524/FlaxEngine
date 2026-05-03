// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "../RendererPass.h"
#include "Engine/Graphics/RenderGraph/RenderGraphPass.h"
#include "Engine/Graphics/GPUPipelineStatePermutations.h"

/// <summary>
/// Temporal Anti-Aliasing effect.
/// </summary>
class TAA : public RendererPass<TAA>, public RenderGraphRasterPass
{
private:

    AssetReference<Shader> _shader;
    GPUPipelineState* _psTAA;

    // RenderGraph resources
    RenderGraphTextureRef _inputRef;
    RenderGraphTextureRef _outputRef;
    RenderGraphTextureRef _historyRef;
    RenderGraphTextureRef _motionVectorsRef;
    RenderGraphTextureRef _depthBufferRef;
    RenderContext* _renderContext = nullptr;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="TAA"/> class.
    /// </summary>
    TAA();

    /// <summary>
    /// Performs AA pass rendering for the input task.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="input">The input render target.</param>
    /// <param name="output">The output render target.</param>
    void Render(const RenderContext& renderContext, GPUTexture* input, GPUTextureView* output);

    // [RenderGraphPass]
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(GPUContext* context) override;

private:

#if COMPILE_WITH_DEV_ENV
    void OnShaderReloading(Asset* obj)
    {
        if (_psTAA)
            _psTAA->ReleaseGPU();
        invalidateResources();
    }
#endif

public:

    // [RendererPass]
    String ToString() const override
    {
        return TEXT("TAA");
    }
    bool Init() override;
    void Dispose() override;

protected:

    // [RendererPass]
    bool setupResources() override;
};
