// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RendererPass.h"
#include "Engine/Graphics/PostProcessSettings.h"
#include "Engine/Graphics/RenderGraph/RenderGraphPass.h"

/// <summary>
/// Contrast Adaptive Sharpening (CAS) provides a mixed ability to sharpen and optionally scale an image. Based on AMD FidelityFX implementation.
/// </summary>
class ContrastAdaptiveSharpeningPass : public RendererPass<ContrastAdaptiveSharpeningPass>, public RenderGraphRasterPass
{
private:
    AssetReference<Shader> _shader;
    GPUPipelineState* _psCAS = nullptr;
    bool _lazyInit = true;

    // RenderGraph resources
    RenderGraphTextureRef _inputRef;
    RenderGraphTextureRef _outputRef;
    RenderContext* _renderContext = nullptr;

public:
    bool CanRender(const RenderContext& renderContext);
    void Render(const RenderContext& renderContext, GPUTexture* input, GPUTextureView* output);

    // [RenderGraphPass]
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(GPUContext* context) override;

private:
#if COMPILE_WITH_DEV_ENV
    void OnShaderReloading(Asset* obj)
    {
        _psCAS->ReleaseGPU();
        invalidateResources();
    }
#endif

public:
    // [RendererPass]
    String ToString() const override;
    void Dispose() override;

protected:
    // [RendererPass]
    bool setupResources() override;
};
