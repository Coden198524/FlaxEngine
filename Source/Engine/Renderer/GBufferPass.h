// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RendererPass.h"
#include "Engine/Graphics/RenderGraph/RenderGraphPass.h"
#if USE_EDITOR
#include "Engine/Core/Collections/Dictionary.h"
#endif

/// <summary>
/// Rendering scene to the GBuffer
/// </summary>
class GBufferPass : public RendererPass<GBufferPass>, public RenderGraphRasterPass
{
private:

    AssetReference<Shader> _gBufferShader;
    GPUPipelineState* _psDebug = nullptr;
    GPUPipelineState* _psLinearToSrgb = nullptr;
    AssetReference<Model> _skyModel;
    AssetReference<Model> _boxModel;
#if USE_EDITOR
    class LightmapUVsDensityMaterialShader* _lightmapUVsDensity = nullptr;
    class VertexColorsMaterialShader* _vertexColors = nullptr;
    class LODPreviewMaterialShader* _lodPreview = nullptr;
    class MaterialComplexityMaterialShader* _materialComplexity = nullptr;
#endif

    // RenderGraph resources
    RenderGraphTextureRef _lightBufferRef;
    RenderGraphTextureRef _gbuffer0Ref;
    RenderGraphTextureRef _gbuffer1Ref;
    RenderGraphTextureRef _gbuffer2Ref;
    RenderGraphTextureRef _gbuffer3Ref;
    RenderGraphTextureRef _depthBufferRef;
    RenderContext* _renderContext = nullptr;

public:

    /// <summary>
    /// Initializes a new instance of the <see cref="GBufferPass"/> class.
    /// </summary>
    GBufferPass();

    /// <summary>
    /// Fill GBuffer
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="lightBuffer">Light buffer to output material emissive light and precomputed indirect lighting</param>
    void Fill(RenderContext& renderContext, GPUTexture* lightBuffer);

    // [RenderGraphPass]
    void Setup(RenderGraphBuilder& builder) override;
    void Execute(GPUContext* context) override;

    /// <summary>
    /// Render debug view
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    void RenderDebug(RenderContext& renderContext);

    /// <summary>
    /// Draws the shader that converts texture from Linear to sRGB color space. Can be used to display internal lighting buffer that is not matching gamma of the output display.
    /// </summary>
    /// <remarks>Assumes the output render target and viewport has been set.</remarks>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="input">The input texture to blit.</param>
    void DrawLinearToSrgb(RenderContext& renderContext, GPUTexture* input);

    /// <summary>
    /// Renders the sky or skybox into low-resolution cubemap. Can be used to sample realtime sky lighting in GI passes.
    /// </summary>
    /// <param name="renderContext">The rendering context.</param>
    /// <param name="context">The GPU context.</param>
    /// <returns>Rendered cubemap or null if not ready or failed.</returns>
    GPUTextureView* RenderSkybox(RenderContext& renderContext, GPUContext* context);

#if USE_EDITOR
    // Temporary cache for faster debug previews drawing (used only during frame rendering).
    static Dictionary<GPUBuffer*, const ModelLOD*> IndexBufferToModelLOD;
    static CriticalSection Locker;

    FORCE_INLINE static void AddIndexBufferToModelLOD(GPUBuffer* indexBuffer, const ModelLOD* modelLod)
    {
        Locker.Lock();
        IndexBufferToModelLOD[indexBuffer] = modelLod;
        Locker.Unlock();
    }
    void PreOverrideDrawCalls(RenderContext& renderContext);
    void OverrideDrawCalls(RenderContext& renderContext);
    void DrawMaterialComplexity(RenderContext& renderContext, GPUContext* context, GPUTextureView* lightBuffer);
#endif

public:

    static bool IsDebugView(ViewMode mode);

    /// <summary>
    /// Set GBuffer inputs structure for given render task
    /// </summary>
    /// <param name="view">The rendering view.</param>
    /// <param name="gBuffer">GBuffer input to setup</param>
    static void SetInputs(const RenderView& view, ShaderGBufferData& gBuffer);

private:

    void DrawSky(RenderContext& renderContext, GPUContext* context);
    void DrawDecals(RenderContext& renderContext, GPUTextureView* lightBuffer);

#if COMPILE_WITH_DEV_ENV
    void OnShaderReloading(Asset* obj)
    {
        _psDebug->ReleaseGPU();
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
