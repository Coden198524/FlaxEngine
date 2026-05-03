// Copyright (c) Wojciech Figat. All rights reserved.

#include "Renderer.h"
#include "Engine/Graphics/GPUContext.h"
#include "Engine/Graphics/GPUPass.h"
#include "Engine/Graphics/RenderTargetPool.h"
#include "Engine/Graphics/RenderBuffers.h"
#include "Engine/Graphics/RenderTask.h"
#include "Engine/Graphics/PostProcessEffect.h"
#include "Engine/Graphics/RenderGraph/RenderGraph.h"
#include "Engine/Engine/EngineService.h"
#include "GBufferPass.h"
#include "ForwardPass.h"
#include "ShadowsPass.h"
#include "LightPass.h"
#include "ReflectionsPass.h"
#include "ScreenSpaceReflectionsPass.h"
#include "AmbientOcclusionPass.h"
#include "DepthOfFieldPass.h"
#include "EyeAdaptationPass.h"
#include "PostProcessingPass.h"
#include "ColorGradingPass.h"
#include "MotionBlurPass.h"
#include "VolumetricFogPass.h"
#include "HistogramPass.h"
#include "AtmospherePreCompute.h"
#include "ContrastAdaptiveSharpeningPass.h"
#include "GlobalSignDistanceFieldPass.h"
#include "GI/GlobalSurfaceAtlasPass.h"
#include "GI/DynamicDiffuseGlobalIllumination.h"
#include "Utils/MultiScaler.h"
#include "Utils/BitonicSort.h"
#include "AntiAliasing/FXAA.h"
#include "AntiAliasing/TAA.h"
#include "AntiAliasing/SMAA.h"
#include "Engine/Level/Actor.h"
#include "Engine/Level/Level.h"
#include "Engine/Level/Scene/SceneRendering.h"
#include "Engine/Core/Config/GraphicsSettings.h"
#include "Engine/Engine/CommandLine.h"
#include "Engine/Graphics/Graphics.h"
#include "Engine/Threading/JobSystem.h"
#include "Engine/Profiler/ProfilerMemory.h"
#if USE_EDITOR
#include "Editor/Editor.h"
#include "Editor/QuadOverdrawPass.h"
#endif

#if USE_EDITOR
// Additional options used in editor for lightmaps baking
bool IsRunningRadiancePass = false;
bool IsBakingLightmaps = false;
bool EnableLightmapsUsage = true;
#endif

Array<RendererPassBase*> PassList;

class RendererService : public EngineService
{
public:
    RendererService()
        : EngineService(TEXT("Renderer"), 20)
    {
    }

    bool Init() override;
    void Dispose() override;
};

RendererService RendererServiceInstance;

void RenderInner(SceneRenderTask* task, RenderContext& renderContext, RenderContextBatch& renderContextBatch);

bool RendererService::Init()
{
    PROFILE_MEM(Graphics);

    // Register renderer passes. The legacy path calls them directly and the experimental RenderGraph path
    // references the same initialized singleton instances without taking ownership.
    PassList.EnsureCapacity(64);
    PassList.Add(GBufferPass::Instance());
    PassList.Add(ShadowsPass::Instance());
    PassList.Add(LightPass::Instance());
    PassList.Add(ForwardPass::Instance());
    PassList.Add(ReflectionsPass::Instance());
    PassList.Add(ScreenSpaceReflectionsPass::Instance());
    PassList.Add(AmbientOcclusionPass::Instance());
    PassList.Add(DepthOfFieldPass::Instance());
    PassList.Add(ColorGradingPass::Instance());
    PassList.Add(VolumetricFogPass::Instance());
    PassList.Add(EyeAdaptationPass::Instance());
    PassList.Add(PostProcessingPass::Instance());
    PassList.Add(MotionBlurPass::Instance());
    PassList.Add(MultiScaler::Instance());
    PassList.Add(BitonicSort::Instance());
    PassList.Add(FXAA::Instance());
    PassList.Add(TAA::Instance());
    PassList.Add(SMAA::Instance());
    PassList.Add(HistogramPass::Instance());
    PassList.Add(GlobalSignDistanceFieldPass::Instance());
    PassList.Add(GlobalSurfaceAtlasPass::Instance());
    PassList.Add(DynamicDiffuseGlobalIlluminationPass::Instance());
#if USE_EDITOR
    PassList.Add(QuadOverdrawPass::Instance());
#endif

    // Skip when using Null renderer
    if (GPUDevice::Instance->GetRendererType() == RendererType::Null)
    {
        return false;
    }

#if GPU_ENABLE_PRELOADING_RESOURCES
    // Init child services
    for (int32 i = 0; i < PassList.Count(); i++)
    {
        if (PassList[i]->Init())
        {
            LOG(Fatal, "Cannot init {0}. Please see a log file for more info.", PassList[i]->ToString());
            return true;
        }
    }
#endif

    return false;
}

void RendererService::Dispose()
{
    // Dispose child services
    for (int32 i = 0; i < PassList.Count(); i++)
    {
        PassList[i]->Dispose();
    }
    SAFE_DELETE_GPU_RESOURCE(IMaterial::BindParameters::PerViewConstants);
}

void RenderAntiAliasingPass(RenderContext& renderContext, GPUTexture* input, GPUTextureView* output, const Viewport& outputViewport)
{
    auto context = GPUDevice::Instance->GetMainContext();
    const auto aaMode = renderContext.List->Settings.AntiAliasing.Mode;
    if (ContrastAdaptiveSharpeningPass::Instance()->CanRender(renderContext))
    {
        if (aaMode == AntialiasingMode::FastApproximateAntialiasing ||
            aaMode == AntialiasingMode::SubpixelMorphologicalAntialiasing)
        {
            // AA -> CAS -> Output
            auto tmpImage = RenderTargetPool::Get(input->GetDescription());
            RENDER_TARGET_POOL_SET_NAME(tmpImage, "TmpImage");
            context->SetViewportAndScissors((float)tmpImage->Width(), (float)tmpImage->Height());
            if (aaMode == AntialiasingMode::FastApproximateAntialiasing)
                FXAA::Instance()->Render(renderContext, input, tmpImage->View());
            else
                SMAA::Instance()->Render(renderContext, input, tmpImage->View());
            context->ResetSR();
            context->ResetRenderTarget();
            context->SetViewportAndScissors(outputViewport);
            ContrastAdaptiveSharpeningPass::Instance()->Render(renderContext, tmpImage, output);
            RenderTargetPool::Release(tmpImage);
        }
        else
        {
            // CAS -> Output
            context->SetViewportAndScissors(outputViewport);
            ContrastAdaptiveSharpeningPass::Instance()->Render(renderContext, input, output);
        }
    }
    else
    {
        // AA -> Output
        context->SetViewportAndScissors(outputViewport);
        if (aaMode == AntialiasingMode::FastApproximateAntialiasing)
            FXAA::Instance()->Render(renderContext, input, output);
        else if (aaMode == AntialiasingMode::SubpixelMorphologicalAntialiasing)
            SMAA::Instance()->Render(renderContext, input, output);
        else
        {
            PROFILE_GPU("Copy frame");
            context->SetRenderTarget(output);
            context->Draw(input);
        }
    }
}

void RenderLightBuffer(const SceneRenderTask* task, GPUContext* context, RenderContext& renderContext, GPUTexture* lightBuffer, const GPUTextureDescription& tempDesc)
{
    context->ResetRenderTarget();
    auto colorGradingLUT = ColorGradingPass::Instance()->RenderLUT(renderContext);
    auto tempBuffer = RenderTargetPool::Get(tempDesc);
    RENDER_TARGET_POOL_SET_NAME(tempBuffer, "TempBuffer");
    EyeAdaptationPass::Instance()->Render(renderContext, lightBuffer);
    PostProcessingPass::Instance()->Render(renderContext, lightBuffer, tempBuffer, colorGradingLUT);
    context->ResetRenderTarget();
    if (renderContext.List->Settings.AntiAliasing.Mode == AntialiasingMode::TemporalAntialiasing)
    {
        TAA::Instance()->Render(renderContext, tempBuffer, lightBuffer->View());
        Swap(lightBuffer, tempBuffer);
    }
    RenderTargetPool::Release(lightBuffer);
    context->SetRenderTarget(task->GetOutputView());
    context->SetViewportAndScissors(task->GetOutputViewport());
    context->Draw(tempBuffer);
    RenderTargetPool::Release(tempBuffer);
}

class MotionVectorsRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _depthBufferRef;

public:
    MotionVectorsRenderGraphPass()
        : RenderGraphRasterPass(TEXT("MotionVectorsPass"))
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        // MotionVectors is lazily allocated by RenderMotionVectors(), so do not transition it here.
        _depthBufferRef = builder.ReadTexture("DepthBuffer", RenderGraphTextureAccess::SRV);
        ReadTexture(_depthBufferRef);
    }

    void Execute(GPUContext* context) override
    {
        if (_renderContext)
            MotionBlurPass::Instance()->RenderMotionVectors(*_renderContext);
    }
};

class FogRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _lightBufferRef;
    RenderGraphTextureRef _depthBufferRef;

public:
    FogRenderGraphPass()
        : RenderGraphRasterPass(TEXT("FogPass"))
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        _lightBufferRef = builder.ReadWriteTexture("LightBuffer", RenderGraphTextureAccess::RTV);
        _depthBufferRef = builder.ReadTexture("DepthBuffer", RenderGraphTextureAccess::SRV);

        ReadTexture(_depthBufferRef);
        SetRenderTarget(0, _lightBufferRef);
        SetDepthStencil(_depthBufferRef, true);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext || !_renderContext->List)
            return;

        GPUTexture* lightBuffer = _lightBufferRef.GetTexture();
        if (!lightBuffer)
            return;

        context->ResetSR();
        if (_renderContext->List->Fog.Renderer)
        {
            VolumetricFogPass::Instance()->Render(*_renderContext);

            PROFILE_GPU_CPU("Fog");
            _renderContext->List->Fog.Renderer->DrawFog(context, *_renderContext, lightBuffer->View());
            context->ResetSR();
        }
    }
};

enum class PostFxRenderGraphTarget
{
    LightBuffer,
    InputFrame,
};

class PostFxRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _targetRef;
    MaterialPostFxLocation _materialLocation;
    PostProcessEffectLocation _customLocation;
    PostFxRenderGraphTarget _target;

public:
    PostFxRenderGraphPass(const Char* name, MaterialPostFxLocation materialLocation, PostProcessEffectLocation customLocation, PostFxRenderGraphTarget target)
        : RenderGraphRasterPass(name)
        , _materialLocation(materialLocation)
        , _customLocation(customLocation)
        , _target(target)
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        _targetRef = builder.ReadWriteTexture(_target == PostFxRenderGraphTarget::LightBuffer ? TEXT("LightBuffer") : TEXT("InputFrame"), RenderGraphTextureAccess::RTV);
        SetRenderTarget(0, _targetRef);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext || !_renderContext->List)
            return;

        GPUTexture* target = _targetRef.GetTexture();
        if (!target)
            return;

        const GPUTextureDescription desc = target->GetDescription();
        GPUTexture* input = RenderTargetPool::Get(desc);
        GPUTexture* output = RenderTargetPool::Get(desc);
        RENDER_TARGET_POOL_SET_NAME(input, "RenderGraph.PostFx.Input");
        RENDER_TARGET_POOL_SET_NAME(output, "RenderGraph.PostFx.Output");

        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();
        context->Transition(target, GPUResourceAccess::ShaderReadGraphics);
        context->SetRenderTarget(*input);
        context->SetViewportAndScissors((float)input->Width(), (float)input->Height());
        context->Draw(target);

        _renderContext->List->RunMaterialPostFxPass(context, *_renderContext, _materialLocation, input, output);
        _renderContext->List->RunCustomPostFxPass(context, *_renderContext, _customLocation, input, output);

        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();
        context->Transition(target, GPUResourceAccess::RenderTarget);
        context->SetRenderTarget(*target);
        context->SetViewportAndScissors((float)target->Width(), (float)target->Height());
        context->Draw(input);
        context->ResetRenderTarget();
        context->ResetSR();

        RenderTargetPool::Release(output);
        RenderTargetPool::Release(input);
    }
};

class UpscaleRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _inputRef;
    RenderGraphTextureRef _outputRef;

public:
    UpscaleRenderGraphPass()
        : RenderGraphRasterPass(TEXT("UpscalePass"))
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers || !_renderContext->Task)
            return;

        _inputRef = builder.ReadTexture("InputFrame", RenderGraphTextureAccess::SRV);

        GPUTexture* input = _inputRef.GetTexture();
        const Viewport outputViewport = _renderContext->Task->GetOutputViewport();
        const int32 width = (int32)outputViewport.Width;
        const int32 height = (int32)outputViewport.Height;
        const PixelFormat format = input ? input->Format() : _renderContext->Buffers->GetOutputFormat();
        RenderGraphTextureDesc outputDesc = RenderGraphTextureDesc::Create2D(width, height, format, GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget, TEXT("InputFrame"));
        outputDesc.Flags |= RenderGraphResourceFlags::Exported;
        _outputRef = builder.CreateTexture(outputDesc);

        ReadTexture(_inputRef);
        SetRenderTarget(0, _outputRef);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext || !_renderContext->List || !_renderContext->Task)
            return;

        GPUTexture* input = _inputRef.GetTexture();
        GPUTexture* output = _outputRef.GetTexture();
        if (!input || !output)
            return;

        const Viewport outputViewport = _renderContext->Task->GetOutputViewport();
        if (_renderContext->List->HasAnyPostFx(*_renderContext, PostProcessEffectLocation::CustomUpscale))
        {
            GPUTexture* frame = input;
            GPUTexture* temp = output;
            _renderContext->List->RunCustomPostFxPass(context, *_renderContext, PostProcessEffectLocation::CustomUpscale, frame, temp);
            if (temp && temp->Width() == output->Width())
                Swap(frame, temp);
            if (frame != output)
            {
                context->ResetRenderTarget();
                context->ResetSR();
                context->ResetUA();
                context->SetRenderTarget(*output);
                context->SetViewportAndScissors(outputViewport);
                context->Draw(frame);
            }
        }
        else
        {
            context->ResetSR();
            MultiScaler::Instance()->Upscale(context, outputViewport, input, output->View());
        }

        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();
    }
};

class GBufferDebugRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _gbuffer0Ref;
    RenderGraphTextureRef _gbuffer1Ref;
    RenderGraphTextureRef _gbuffer2Ref;
    RenderGraphTextureRef _gbuffer3Ref;
    RenderGraphTextureRef _depthBufferRef;
    RenderGraphTextureRef _outputRef;

public:
    GBufferDebugRenderGraphPass()
        : RenderGraphRasterPass(TEXT("GBufferDebugPass"))
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        _gbuffer0Ref = builder.ReadTexture("GBuffer0", RenderGraphTextureAccess::SRV);
        _gbuffer1Ref = builder.ReadTexture("GBuffer1", RenderGraphTextureAccess::SRV);
        _gbuffer2Ref = builder.ReadTexture("GBuffer2", RenderGraphTextureAccess::SRV);
        _gbuffer3Ref = builder.ReadTexture("GBuffer3", RenderGraphTextureAccess::SRV);
        _depthBufferRef = builder.ReadTexture("DepthBuffer", RenderGraphTextureAccess::SRV);

        RenderGraphTextureDesc outputDesc = RenderGraphTextureDesc::Create2D(
            _renderContext->Buffers->GetWidth(),
            _renderContext->Buffers->GetHeight(),
            _renderContext->Buffers->GetOutputFormat(),
            GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
            TEXT("OutputFrame"));
        outputDesc.Flags |= RenderGraphResourceFlags::Exported;
        _outputRef = builder.CreateTexture(outputDesc);

        ReadTexture(_gbuffer0Ref);
        ReadTexture(_gbuffer1Ref);
        ReadTexture(_gbuffer2Ref);
        ReadTexture(_gbuffer3Ref);
        ReadTexture(_depthBufferRef);
        SetRenderTarget(0, _outputRef);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext)
            return;

        GPUTexture* output = _outputRef.GetTexture();
        if (!output)
            return;

        context->ResetRenderTarget();
        context->SetRenderTarget(output->View());
        context->SetViewportAndScissors((float)output->Width(), (float)output->Height());
        GBufferPass::Instance()->RenderDebug(*_renderContext);
        context->ResetRenderTarget();
        context->ResetSR();
    }
};

class CopyTextureRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    const Char* _name;
    RenderGraphTextureRef _inputRef;
    RenderGraphTextureRef _outputRef;

public:
    CopyTextureRenderGraphPass(const Char* name, const Char* inputName)
        : RenderGraphRasterPass(name)
        , _name(inputName)
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        _inputRef = builder.ReadTexture(_name, RenderGraphTextureAccess::SRV);
        GPUTexture* input = _inputRef.GetTexture();
        const PixelFormat format = input ? input->Format() : _renderContext->Buffers->GetOutputFormat();
        RenderGraphTextureDesc outputDesc = RenderGraphTextureDesc::Create2D(
            _renderContext->Buffers->GetWidth(),
            _renderContext->Buffers->GetHeight(),
            format,
            GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
            TEXT("OutputFrame"));
        outputDesc.Flags |= RenderGraphResourceFlags::Exported;
        _outputRef = builder.CreateTexture(outputDesc);

        ReadTexture(_inputRef);
        SetRenderTarget(0, _outputRef);
    }

    void Execute(GPUContext* context) override
    {
        GPUTexture* input = _inputRef.GetTexture();
        GPUTexture* output = _outputRef.GetTexture();
        if (!input || !output)
            return;

        context->ResetRenderTarget();
        context->ResetSR();
        context->SetRenderTarget(output->View());
        context->SetViewportAndScissors((float)output->Width(), (float)output->Height());
        context->Draw(input);
        context->ResetRenderTarget();
        context->ResetSR();
    }
};

enum class RenderGraphDebugOutput
{
    GlobalSDF,
    GlobalSurfaceAtlas,
    MotionVectors,
};

class DebugOutputRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphDebugOutput _mode;
    RenderGraphTextureRef _lightBufferRef;
    RenderGraphTextureRef _inputFrameRef;
    RenderGraphTextureRef _motionVectorsRef;
    RenderGraphTextureRef _depthBufferRef;
    RenderGraphTextureRef _outputRef;

public:
    DebugOutputRenderGraphPass(RenderGraphDebugOutput mode)
        : RenderGraphRasterPass(TEXT("DebugOutputPass"))
        , _mode(mode)
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        switch (_mode)
        {
        case RenderGraphDebugOutput::GlobalSDF:
        case RenderGraphDebugOutput::GlobalSurfaceAtlas:
            break;
        case RenderGraphDebugOutput::MotionVectors:
            _inputFrameRef = builder.ReadTexture("InputFrame", RenderGraphTextureAccess::SRV);
            _motionVectorsRef = builder.ReadTexture("MotionVectors", RenderGraphTextureAccess::SRV);
            _depthBufferRef = builder.ReadTexture("DepthBuffer", RenderGraphTextureAccess::SRV);
            ReadTexture(_inputFrameRef);
            ReadTexture(_motionVectorsRef);
            ReadTexture(_depthBufferRef);
            break;
        }

        RenderGraphTextureDesc outputDesc = RenderGraphTextureDesc::Create2D(
            _renderContext->Buffers->GetWidth(),
            _renderContext->Buffers->GetHeight(),
            _renderContext->Buffers->GetOutputFormat(),
            GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
            TEXT("OutputFrame"));
        outputDesc.Flags |= RenderGraphResourceFlags::Exported;
        _outputRef = builder.CreateTexture(outputDesc);
        SetRenderTarget(0, _outputRef);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext)
            return;

        GPUTexture* output = _outputRef.GetTexture();
        if (!output)
            return;

        context->ResetRenderTarget();
        context->ResetSR();
        context->SetViewportAndScissors((float)output->Width(), (float)output->Height());

        switch (_mode)
        {
        case RenderGraphDebugOutput::GlobalSDF:
            GlobalSignDistanceFieldPass::Instance()->RenderDebug(*_renderContext, context, output);
            break;
        case RenderGraphDebugOutput::GlobalSurfaceAtlas:
            GlobalSurfaceAtlasPass::Instance()->RenderDebug(*_renderContext, context, output);
            break;
        case RenderGraphDebugOutput::MotionVectors:
        {
            GPUTexture* input = _inputFrameRef.GetTexture();
            if (!input)
                return;
            context->SetRenderTarget(output->View());
            MotionBlurPass::Instance()->RenderDebug(*_renderContext, input->View());
            break;
        }
        }

        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();
    }
};

class LightBufferPostRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _lightBufferRef;
    RenderGraphTextureRef _outputRef;

public:
    LightBufferPostRenderGraphPass()
        : RenderGraphRasterPass(TEXT("LightBufferDebugPass"))
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        _lightBufferRef = builder.ReadWriteTexture("LightBuffer", RenderGraphTextureAccess::RTV);
        RenderGraphTextureDesc outputDesc = RenderGraphTextureDesc::Create2D(
            _renderContext->Buffers->GetWidth(),
            _renderContext->Buffers->GetHeight(),
            _renderContext->Buffers->GetOutputFormat(),
            GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
            TEXT("OutputFrame"));
        outputDesc.Flags |= RenderGraphResourceFlags::Exported;
        _outputRef = builder.CreateTexture(outputDesc);

        ReadTexture(_lightBufferRef);
        WriteTexture(_lightBufferRef);
        SetRenderTarget(0, _outputRef);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext)
            return;

        GPUTexture* lightBuffer = _lightBufferRef.GetTexture();
        GPUTexture* output = _outputRef.GetTexture();
        if (!lightBuffer || !output)
            return;

        context->ResetRenderTarget();
        context->ResetSR();

        if (_renderContext->View.Mode == ViewMode::Reflections)
        {
            _renderContext->List->Settings.ToneMapping.Mode = ToneMappingMode::Neutral;
            _renderContext->List->Settings.Bloom.Enabled = false;
            _renderContext->List->Settings.LensFlares.Intensity = 0.0f;
            _renderContext->List->Settings.CameraArtifacts.GrainAmount = 0.0f;
            _renderContext->List->Settings.CameraArtifacts.ChromaticDistortion = 0.0f;
            _renderContext->List->Settings.CameraArtifacts.VignetteIntensity = 0.0f;
        }

        auto colorGradingLUT = ColorGradingPass::Instance()->RenderLUT(*_renderContext);
        GPUTexture* tempBuffer = RenderTargetPool::Get(output->GetDescription());
        RENDER_TARGET_POOL_SET_NAME(tempBuffer, "RenderGraph.LightBufferDebug.Temp");
        EyeAdaptationPass::Instance()->Render(*_renderContext, lightBuffer);
        PostProcessingPass::Instance()->Render(*_renderContext, lightBuffer, tempBuffer, colorGradingLUT);
        if (_renderContext->List->Settings.AntiAliasing.Mode == AntialiasingMode::TemporalAntialiasing)
        {
            TAA::Instance()->Render(*_renderContext, tempBuffer, output->View());
        }
        else
        {
            context->SetRenderTarget(output->View());
            context->SetViewportAndScissors((float)output->Width(), (float)output->Height());
            context->Draw(tempBuffer);
        }
        RenderTargetPool::Release(tempBuffer);

        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();
    }
};

#if USE_EDITOR
class QuadOverdrawRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _depthBufferRef;
    RenderGraphTextureRef _outputRef;

public:
    QuadOverdrawRenderGraphPass()
        : RenderGraphRasterPass(TEXT("QuadOverdrawPass"))
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        _depthBufferRef = builder.ReadWriteTexture("DepthBuffer", RenderGraphTextureAccess::RTV);
        RenderGraphTextureDesc outputDesc = RenderGraphTextureDesc::Create2D(
            _renderContext->Buffers->GetWidth(),
            _renderContext->Buffers->GetHeight(),
            _renderContext->Buffers->GetOutputFormat(),
            GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
            TEXT("OutputFrame"));
        outputDesc.Flags |= RenderGraphResourceFlags::Exported;
        _outputRef = builder.CreateTexture(outputDesc);

        SetDepthStencil(_depthBufferRef, false);
        SetRenderTarget(0, _outputRef);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext)
            return;

        GPUTexture* output = _outputRef.GetTexture();
        if (!output)
            return;

        context->ResetRenderTarget();
        context->ResetSR();
        context->SetViewportAndScissors((float)output->Width(), (float)output->Height());
        QuadOverdrawPass::Instance()->Render(*_renderContext, context, output->View());
        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();
    }
};

class MaterialComplexityRenderGraphPass : public RenderGraphRasterPass
{
private:
    RenderContext* _renderContext = nullptr;
    RenderGraphTextureRef _lightBufferRef;
    RenderGraphTextureRef _depthBufferRef;
    RenderGraphTextureRef _outputRef;

public:
    MaterialComplexityRenderGraphPass()
        : RenderGraphRasterPass(TEXT("MaterialComplexityPass"))
    {
    }

    void Setup(RenderGraphBuilder& builder) override
    {
        _renderContext = builder.GetRenderContext();
        if (!_renderContext || !_renderContext->Buffers)
            return;

        _lightBufferRef = builder.ReadWriteTexture("LightBuffer", RenderGraphTextureAccess::RTV);
        _depthBufferRef = builder.ReadTexture("DepthBuffer", RenderGraphTextureAccess::SRV);
        RenderGraphTextureDesc outputDesc = RenderGraphTextureDesc::Create2D(
            _renderContext->Buffers->GetWidth(),
            _renderContext->Buffers->GetHeight(),
            _renderContext->Buffers->GetOutputFormat(),
            GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
            TEXT("OutputFrame"));
        outputDesc.Flags |= RenderGraphResourceFlags::Exported;
        _outputRef = builder.CreateTexture(outputDesc);

        ReadTexture(_lightBufferRef);
        WriteTexture(_lightBufferRef);
        ReadTexture(_depthBufferRef);
        SetRenderTarget(0, _outputRef);
    }

    void Execute(GPUContext* context) override
    {
        if (!_renderContext)
            return;

        GPUTexture* lightBuffer = _lightBufferRef.GetTexture();
        GPUTexture* output = _outputRef.GetTexture();
        if (!lightBuffer || !output)
            return;

        Viewport outputViewport(Float2((float)output->Width(), (float)output->Height()));
        GBufferPass::Instance()->DrawMaterialComplexity(*_renderContext, context, lightBuffer->View(), output->View(), &outputViewport);
        context->ResetRenderTarget();
        context->ResetSR();
    }
};
#endif

bool HasRenderGraphUnsupportedPostFx(const RenderContext& renderContext)
{
    return false;
}

const Char* GetRenderGraphUnsupportedReason(const SceneRenderTask* task, const RenderContext& renderContext)
{
    const RenderView& view = renderContext.View;
    const RenderList* list = renderContext.List;
    const PostProcessSettings& settings = list->Settings;
    const RenderSetup& setup = list->Setup;

    switch (view.Mode)
    {
    case ViewMode::Default:
    case ViewMode::NoPostFx:
    case ViewMode::Diffuse:
    case ViewMode::Normals:
    case ViewMode::Emissive:
    case ViewMode::Depth:
    case ViewMode::AmbientOcclusion:
    case ViewMode::Metalness:
    case ViewMode::Roughness:
    case ViewMode::Specular:
    case ViewMode::SpecularColor:
    case ViewMode::ShadingModel:
    case ViewMode::LightBuffer:
    case ViewMode::Reflections:
    case ViewMode::Wireframe:
    case ViewMode::MotionVectors:
    case ViewMode::SubsurfaceColor:
    case ViewMode::Unlit:
    case ViewMode::LightmapUVsDensity:
    case ViewMode::VertexColors:
    case ViewMode::PhysicsColliders:
    case ViewMode::LODPreview:
    case ViewMode::MaterialComplexity:
    case ViewMode::QuadOverdraw:
    case ViewMode::GlobalSDF:
    case ViewMode::GlobalSurfaceAtlas:
    case ViewMode::GlobalIllumination:
        break;
    default:
        return TEXT("view mode");
    }
    if (list->AtmosphericFog)
        return TEXT("atmospheric fog");
    if (HasRenderGraphUnsupportedPostFx(renderContext))
        return TEXT("custom post effects");

    return nullptr;
}

bool CanUseRenderGraphForFrame(const SceneRenderTask* task, const RenderContext& renderContext)
{
    return GetRenderGraphUnsupportedReason(task, renderContext) == nullptr;
}

bool PresentRenderGraphOutput(const SceneRenderTask* task, GPUContext* context, RenderContext& renderContext, RenderGraph& graph)
{
    GPUTexture* frame = graph.GetTexture(TEXT("OutputFrame"));
    const bool hasPostProcessingOutput = frame != nullptr;
    if (!frame)
        frame = graph.GetTexture(TEXT("InputFrame"));
    if (!frame)
        frame = graph.GetTexture(TEXT("LightBuffer"));
    if (!frame)
        return false;

    context->ResetRenderTarget();
    context->ResetSR();
    context->FlushState();

    const Viewport outputViewport = task->GetOutputViewport();
    GPUTextureView* outputView = task->GetOutputView();
    const ViewMode viewMode = renderContext.View.Mode;
    const bool directOutput =
            hasPostProcessingOutput &&
            (GBufferPass::IsDebugView(viewMode) ||
             viewMode == ViewMode::Emissive ||
             viewMode == ViewMode::VertexColors ||
             viewMode == ViewMode::LightmapUVsDensity ||
             viewMode == ViewMode::PhysicsColliders ||
             viewMode == ViewMode::MaterialComplexity ||
             viewMode == ViewMode::QuadOverdraw ||
             viewMode == ViewMode::GlobalSDF ||
             viewMode == ViewMode::GlobalSurfaceAtlas ||
             viewMode == ViewMode::LightBuffer ||
             viewMode == ViewMode::Reflections ||
             viewMode == ViewMode::MotionVectors);
    if (directOutput)
    {
        context->SetRenderTarget(outputView);
        context->SetViewportAndScissors(outputViewport);
        context->Draw(frame);
        return true;
    }
    if (viewMode == ViewMode::NoPostFx || viewMode == ViewMode::Wireframe)
    {
        context->SetRenderTarget(outputView);
        context->SetViewportAndScissors(outputViewport);
        if (!Graphics::GammaColorSpace)
            GBufferPass::Instance()->DrawLinearToSrgb(renderContext, frame);
        else
            context->Draw(frame);
        return true;
    }

    GPUTexture* frameBuffer = frame;
    GPUTexture* tempBuffer = nullptr;
    GPUTexture* presentTempBuffer = nullptr;
    const GPUTextureDescription tempDesc = frame->GetDescription();
    const bool hasLatePostFx =
            renderContext.List->HasAnyPostFx(renderContext, MaterialPostFxLocation::AfterPostProcessingPass) ||
            renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::Default) ||
            renderContext.List->HasAnyPostFx(renderContext, MaterialPostFxLocation::AfterCustomPostEffects);
    const bool hasAfterAA =
            renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::AfterAntiAliasingPass, MaterialPostFxLocation::AfterAntiAliasingPass);
    const bool useUpscaling =
            task->RenderingPercentage < 1.0f &&
            renderContext.List->Setup.UpscaleLocation == RenderingUpscaleLocation::AfterAntiAliasingPass;

    if (hasLatePostFx || hasAfterAA || useUpscaling)
    {
        tempBuffer = RenderTargetPool::Get(tempDesc);
        presentTempBuffer = tempBuffer;
        RENDER_TARGET_POOL_SET_NAME(tempBuffer, "RenderGraph.PresentTemp");
    }

    if (hasLatePostFx)
    {
        renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterPostProcessingPass, frameBuffer, tempBuffer);
        renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::Default, frameBuffer, tempBuffer);
        renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterCustomPostEffects, frameBuffer, tempBuffer);

        context->ResetRenderTarget();
        context->ResetSR();
        context->FlushState();
    }

    if (!hasAfterAA)
    {
        if (!useUpscaling)
        {
            RenderAntiAliasingPass(renderContext, frameBuffer, outputView, outputViewport);
        }
        else
        {
            RenderAntiAliasingPass(renderContext, frameBuffer, *tempBuffer, Viewport(Float2(renderContext.View.ScreenSize)));
            context->ResetRenderTarget();
            Swap(frameBuffer, tempBuffer);
        }
    }
    else
    {
        RenderAntiAliasingPass(renderContext, frameBuffer, *tempBuffer, Viewport(Float2(renderContext.View.ScreenSize)));
        context->ResetRenderTarget();
        Swap(frameBuffer, tempBuffer);
        renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::AfterAntiAliasingPass, frameBuffer, tempBuffer);
        renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterAntiAliasingPass, frameBuffer, tempBuffer);
    }

    if (useUpscaling)
    {
        if (renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::CustomUpscale))
        {
            if (outputView->GetParent()->Is<GPUTexture>())
            {
                auto outputTexture = (GPUTexture*)outputView->GetParent();
                renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::CustomUpscale, frameBuffer, outputTexture);
            }
            else
            {
                GPUTextureDescription upscaleDesc = tempDesc;
                upscaleDesc.Width = (int32)outputViewport.Width;
                upscaleDesc.Height = (int32)outputViewport.Height;
                GPUTexture* upscaleTemp = RenderTargetPool::Get(upscaleDesc);
                RENDER_TARGET_POOL_SET_NAME(upscaleTemp, "RenderGraph.PresentUpscaleTemp");
                renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::CustomUpscale, frameBuffer, upscaleTemp);
                PROFILE_GPU("Copy frame");
                context->SetRenderTarget(outputView);
                context->SetViewportAndScissors(outputViewport);
                context->Draw(frameBuffer);
                RenderTargetPool::Release(upscaleTemp);
            }
        }
        else
        {
            MultiScaler::Instance()->Upscale(context, outputViewport, frameBuffer, outputView);
        }
    }
    else if (hasAfterAA)
    {
        PROFILE_GPU("Copy frame");
        context->SetRenderTarget(outputView);
        context->SetViewportAndScissors(outputViewport);
        context->Draw(frameBuffer);
    }

    RenderTargetPool::Release(presentTempBuffer);
    return hasPostProcessingOutput || frameBuffer != nullptr;
}

bool Renderer::IsReady()
{
    // Warm up first (state getters initialize content loading so do it for all first)
    AtmosphereCache atmosphereCache;
    AtmospherePreCompute::GetCache(&atmosphereCache);
    for (int32 i = 0; i < PassList.Count(); i++)
        PassList[i]->IsReady();

    // Now check state
    if (!AtmospherePreCompute::GetCache(&atmosphereCache))
        return false;
    for (int32 i = 0; i < PassList.Count(); i++)
    {
        if (!PassList[i]->IsReady())
            return false;
    }
    return true;
}

void Renderer::Render(SceneRenderTask* task)
{
    PROFILE_GPU_CPU_NAMED("Render Frame");

    // Prepare GPU context
    auto context = GPUDevice::Instance->GetMainContext();
    context->ResetState();
    context->FlushState();
    const Viewport viewport = task->GetViewport();
    context->SetViewportAndScissors(viewport);

    // Prepare render context
    RenderContext renderContext(task);
    renderContext.List = RenderList::GetFromPool();
    RenderContextBatch renderContextBatch(task);
    renderContextBatch.Contexts.Add(renderContext);

    // Pre-init render view cache early in case it's used in PreRender drawing
    Float4 jitter = renderContext.View.TemporalAAJitter; // Preserve temporal jitter value (PrepareCache modifies it)
    renderContext.View.PrepareCache(renderContext, viewport.Width, viewport.Height, Float2::Zero);
    renderContext.View.TemporalAAJitter = jitter;

#if USE_EDITOR
    // Turn on low quality rendering during baking lightmaps (leave more GPU power for baking)
    const auto flags = renderContext.View.Flags;
    if (!renderContext.View.IsOfflinePass && IsBakingLightmaps)
    {
        renderContext.View.Flags &= ~(ViewFlags::AO
            | ViewFlags::Shadows
            | ViewFlags::AntiAliasing
            | ViewFlags::CustomPostProcess
            | ViewFlags::Bloom
            | ViewFlags::ToneMapping
            | ViewFlags::EyeAdaptation
            | ViewFlags::CameraArtifacts
            | ViewFlags::Reflections
            | ViewFlags::SSR
            | ViewFlags::LensFlares
            | ViewFlags::MotionBlur
            | ViewFlags::Fog
            | ViewFlags::PhysicsDebug
            | ViewFlags::Decals
            | ViewFlags::GI
            | ViewFlags::DebugDraw
            | ViewFlags::ContactShadows
            | ViewFlags::DepthOfField);
    }

    // Force Debug Draw usage in some specific views that depend on it
    if (renderContext.View.Mode == ViewMode::PhysicsColliders)
    {
        renderContext.View.Flags |= ViewFlags::DebugDraw;
    }
#endif

    // Perform the actual rendering
    task->OnPreRender(context, renderContext);
    RenderInner(task, renderContext, renderContextBatch);
    task->OnPostRender(context, renderContext);

#if USE_EDITOR
    // Restore flags
    renderContext.View.Flags = flags;
#endif

    // Copy back the view (modified during rendering with rendering state like TAA frame index and jitter)
    task->View = renderContext.View;

    // Cleanup
    for (const auto& e : renderContextBatch.Contexts)
        RenderList::ReturnToPool(e.List);
}

void Renderer::DrawSceneDepth(GPUContext* context, SceneRenderTask* task, GPUTexture* output, const Array<Actor*>& customActors)
{
    CHECK(context && task && output && output->IsDepthStencil());

    // Prepare
    RenderContext renderContext(task);
    renderContext.List = RenderList::GetFromPool();
    renderContext.View.Pass = DrawPass::Depth;
    renderContext.View.Prepare(renderContext);

    // Call drawing (will collect draw calls)
    DrawActors(renderContext, customActors);

    // Sort draw calls
    renderContext.List->SortDrawCalls(renderContext, false, DrawCallsListType::Depth, DrawPass::Depth);

    // Execute draw calls
    const float width = (float)output->Width();
    const float height = (float)output->Height();
    context->SetViewport(width, height);
    context->SetRenderTarget(output->View(), static_cast<GPUTextureView*>(nullptr));
    renderContext.List->ExecuteDrawCalls(renderContext, DrawCallsListType::Depth);

    // Cleanup
    RenderList::ReturnToPool(renderContext.List);
}

void Renderer::DrawPostFxMaterial(GPUContext* context, const RenderContext& renderContext, MaterialBase* material, GPUTexture* output, GPUTextureView* input)
{
    CHECK(material && material->IsPostFx());
    CHECK(context && output);

    context->ResetSR();
    context->SetViewport((float)output->Width(), (float)output->Height());
    context->SetRenderTarget(output->View());
    context->FlushState();

    MaterialBase::BindParameters bindParams(context, renderContext);
    bindParams.Input = input;
    material->Bind(bindParams);

    context->DrawFullscreenTriangle();
    context->ResetRenderTarget();
}

void Renderer::DrawActors(RenderContext& renderContext, const Array<Actor*>& customActors)
{
    if (customActors.HasItems())
    {
        // Draw custom actors
        for (Actor* actor : customActors)
        {
            if (actor && actor->GetIsActive())
                actor->Draw(renderContext);
        }
    }
    else
    {
        // Draw scene actors
        RenderContextBatch renderContextBatch(renderContext);
        JobSystem::SetJobStartingOnDispatch(false);
        Level::DrawActors(renderContextBatch, SceneRendering::DrawCategory::SceneDraw);
        Level::DrawActors(renderContextBatch, SceneRendering::DrawCategory::SceneDrawAsync);
        JobSystem::SetJobStartingOnDispatch(true);
        for (const int64 label : renderContextBatch.WaitLabels)
            JobSystem::Wait(label);
        renderContextBatch.WaitLabels.Clear();
    }
}

void RenderInner(SceneRenderTask* task, RenderContext& renderContext, RenderContextBatch& renderContextBatch)
{
    auto context = GPUDevice::Instance->GetMainContext();
    auto* graphicsSettings = GraphicsSettings::Get();
    auto& view = renderContext.View;
    ASSERT(renderContext.Buffers && renderContext.Buffers->GetWidth() > 0);

    const bool useRenderGraph = CommandLine::Options.RenderGraph.GetValueOr(true) && !CommandLine::Options.NoRenderGraph.GetValueOr(false);

    // Perform postFx volumes blending and query before rendering
    task->CollectPostFxVolumes(renderContext);
    renderContext.List->BlendSettings();
    {
        auto aaMode = EnumHasAnyFlags(renderContext.View.Flags, ViewFlags::AntiAliasing) ? renderContext.List->Settings.AntiAliasing.Mode : AntialiasingMode::None;
        if (aaMode == AntialiasingMode::TemporalAntialiasing && view.IsOrthographicProjection())
            aaMode = AntialiasingMode::None; // TODO: support TAA in ortho projection (see RenderView::Prepare to jitter projection matrix better)
        renderContext.List->Settings.AntiAliasing.Mode = aaMode;
    }

    // Initialize setup
    RenderSetup& setup = renderContext.List->Setup;
    const bool isGBufferDebug = GBufferPass::IsDebugView(renderContext.View.Mode);
    {
        PROFILE_CPU_NAMED("Setup");
        const int32 screenWidth = renderContext.Buffers->GetWidth();
        const int32 screenHeight = renderContext.Buffers->GetHeight();
        setup.UpscaleLocation = renderContext.Task->UpscaleLocation;
        if (screenWidth < 16 || screenHeight < 16 || renderContext.Task->IsCameraCut || isGBufferDebug || renderContext.View.Mode == ViewMode::NoPostFx)
            setup.UseMotionVectors = false;
        else
        {
            const MotionBlurSettings& motionBlurSettings = renderContext.List->Settings.MotionBlur;
            const ScreenSpaceReflectionsSettings ssrSettings = renderContext.List->Settings.ScreenSpaceReflections;
            setup.UseMotionVectors =
                    (EnumHasAnyFlags(renderContext.View.Flags, ViewFlags::MotionBlur) && motionBlurSettings.Enabled && motionBlurSettings.Scale > ZeroTolerance) ||
                    renderContext.View.Mode == ViewMode::MotionVectors ||
                    (ssrSettings.Intensity > ZeroTolerance && ssrSettings.TemporalEffect && EnumHasAnyFlags(renderContext.View.Flags, ViewFlags::SSR)) ||
                    renderContext.List->Settings.AntiAliasing.Mode == AntialiasingMode::TemporalAntialiasing;
        }
        setup.UseTemporalAAJitter = renderContext.List->Settings.AntiAliasing.Mode == AntialiasingMode::TemporalAntialiasing;
        setup.UseGlobalSurfaceAtlas = renderContext.View.Mode == ViewMode::GlobalSurfaceAtlas ||
                (EnumHasAnyFlags(renderContext.View.Flags, ViewFlags::GI) && renderContext.List->Settings.GlobalIllumination.Mode == GlobalIlluminationMode::DDGI);
        setup.UseGlobalSDF = (graphicsSettings->EnableGlobalSDF && EnumHasAnyFlags(view.Flags, ViewFlags::GlobalSDF)) ||
                renderContext.View.Mode == ViewMode::GlobalSDF ||
                setup.UseGlobalSurfaceAtlas;
        setup.UseVolumetricFog = (view.Flags & ViewFlags::Fog) != ViewFlags::None;

        // Disable TAA jitter in debug modes
        switch (renderContext.View.Mode)
        {
        case ViewMode::Unlit:
        case ViewMode::Diffuse:
        case ViewMode::Normals:
        case ViewMode::Depth:
        case ViewMode::Emissive:
        case ViewMode::AmbientOcclusion:
        case ViewMode::Metalness:
        case ViewMode::Roughness:
        case ViewMode::Specular:
        case ViewMode::SpecularColor:
        case ViewMode::SubsurfaceColor:
        case ViewMode::ShadingModel:
        case ViewMode::Reflections:
        case ViewMode::GlobalSDF:
        case ViewMode::GlobalSurfaceAtlas:
        case ViewMode::LightmapUVsDensity:
        case ViewMode::MaterialComplexity:
        case ViewMode::Wireframe:
        case ViewMode::NoPostFx:
        case ViewMode::VertexColors:
        case ViewMode::QuadOverdraw:
            setup.UseTemporalAAJitter = false;
            break;
        }

        // Customize setup (by postfx or custom gameplay effects)
        renderContext.Task->SetupRender(renderContext);
        for (PostProcessEffect* e : renderContext.List->PostFx)
            e->PreRender(context, renderContext);
    }
    renderContext.View.Prepare(renderContext);

    // Build batch of render contexts (main view and shadow projections)
    {
        PROFILE_CPU_NAMED("Collect Draw Calls");

        view.Pass = DrawPass::GBuffer | DrawPass::Forward | DrawPass::Distortion;
        if (setup.UseMotionVectors)
            view.Pass |= DrawPass::MotionVectors;
        renderContextBatch.GetMainContext() = renderContext; // Sync render context in batch with the current value
        renderContext.List->PreDraw(context, renderContextBatch);

        bool drawShadows = !isGBufferDebug && EnumHasAnyFlags(view.Flags, ViewFlags::Shadows) && ShadowsPass::Instance()->IsReady();
        switch (renderContext.View.Mode)
        {
        case ViewMode::QuadOverdraw:
        case ViewMode::Emissive:
        case ViewMode::LightmapUVsDensity:
        case ViewMode::GlobalSurfaceAtlas:
        case ViewMode::GlobalSDF:
        case ViewMode::MaterialComplexity:
        case ViewMode::VertexColors:
            drawShadows = false;
            break;
        }
        LightPass::Instance()->SetupLights(renderContext, renderContextBatch);
        if (drawShadows)
            ShadowsPass::Instance()->SetupShadows(renderContext, renderContextBatch);
#if USE_EDITOR
        GBufferPass::Instance()->PreOverrideDrawCalls(renderContext);
#endif

        // Dispatch drawing (via JobSystem - multiple job batches for every scene)
        JobSystem::SetJobStartingOnDispatch(false);
        task->OnCollectDrawCalls(renderContextBatch, SceneRendering::DrawCategory::SceneDraw);
        task->OnCollectDrawCalls(renderContextBatch, SceneRendering::DrawCategory::SceneDrawAsync);
        if (setup.UseGlobalSDF)
            GlobalSignDistanceFieldPass::Instance()->OnCollectDrawCalls(renderContextBatch);
        if (setup.UseGlobalSurfaceAtlas)
            GlobalSurfaceAtlasPass::Instance()->OnCollectDrawCalls(renderContextBatch);

        // Wait for async jobs to finish
        JobSystem::SetJobStartingOnDispatch(true);
        for (const int64 label : renderContextBatch.WaitLabels)
            JobSystem::Wait(label);
        renderContextBatch.WaitLabels.Clear();

        // Perform custom post-scene drawing (eg. GPU dispatches used by VFX)
        for (int32 i = 0; i < renderContextBatch.Contexts.Count(); i++)
            renderContextBatch.Contexts[i].List->DrainDelayedDraws(context, renderContextBatch, i);
        renderContext.List->PostDraw(context, renderContextBatch);

#if USE_EDITOR
        GBufferPass::Instance()->OverrideDrawCalls(renderContext);
#endif
    }

    // Process draw calls (sorting, objects buffer building)
    {
        PROFILE_CPU_NAMED("Process Draw Calls");

        // Utility that handles async jobs for a specific rendering routines in async
        struct DrawCallsProcessor
        {
            RenderContextBatch& RenderContextBatch;
            Pair<DrawCallsListType, bool> MainContextSorting[5] =
            {
                // Draw List + Reverse Distance sorting
                ToPair(DrawCallsListType::GBuffer, false),
                ToPair(DrawCallsListType::GBufferNoDecals, false),
                ToPair(DrawCallsListType::Forward, true),
                ToPair(DrawCallsListType::Distortion, false),
                ToPair(DrawCallsListType::MotionVectors, false),
            };

            void BuildObjectsBufferJob(int32 index)
            {
                RenderContextBatch.Contexts[index].List->BuildObjectsBuffer();
            }

            void SortDrawCallsJob(int32 index)
            {
                RenderContext& renderContext = RenderContextBatch.GetMainContext();
                if (index < ARRAY_COUNT(MainContextSorting))
                {
                    // Main context sorting
                    RenderSetup& setup = renderContext.List->Setup;
                    auto sorting = MainContextSorting[index];
                    if (sorting.First == DrawCallsListType::MotionVectors && !setup.UseMotionVectors)
                        return;
                    renderContext.List->SortDrawCalls(renderContext, sorting.Second, sorting.First);
                }
                else
                {
                    // Shadow context sorting
                    auto& shadowContext = RenderContextBatch.Contexts[index - ARRAY_COUNT(MainContextSorting)];
                    shadowContext.List->SortDrawCalls(shadowContext, false, DrawCallsListType::Depth, DrawPass::Depth);
                    shadowContext.List->SortDrawCalls(shadowContext, false, shadowContext.List->ShadowDepthDrawCallsList, renderContext.List->DrawCalls, DrawCallsListType::Depth, DrawPass::Depth);
                }
            }
        } processor = { renderContextBatch };

        // Dispatch async jobs
        Function<void(int32)> func;
        func.Bind<DrawCallsProcessor, &DrawCallsProcessor::BuildObjectsBufferJob>(&processor);
        const int64 buildObjectsBufferJob = JobSystem::Dispatch(func, renderContextBatch.Contexts.Count());
        func.Bind<DrawCallsProcessor, &DrawCallsProcessor::SortDrawCallsJob>(&processor);
        const int64 sortDrawCallsJob = JobSystem::Dispatch(func, ARRAY_COUNT(DrawCallsProcessor::MainContextSorting) + renderContextBatch.Contexts.Count());

        // Upload objects buffers to the GPU
        JobSystem::Wait(buildObjectsBufferJob);
        {
            PROFILE_CPU_NAMED("FlushObjectsBuffer");
            GPUMemoryPass pass(context);
            for (auto& e : renderContextBatch.Contexts)
                e.List->ObjectBuffer.Flush(context);
        }

        // Wait for async jobs to finish
        // TODO: use per-pass wait labels (eg. don't wait for shadow pass draws sorting until ShadowPass needs it)       
        JobSystem::Wait(sortDrawCallsJob);
    }

    // RenderGraph execution path
    const Char* renderGraphUnsupportedReason = useRenderGraph ? GetRenderGraphUnsupportedReason(task, renderContext) : nullptr;
    const bool canUseRenderGraph = useRenderGraph && renderGraphUnsupportedReason == nullptr;
    if (useRenderGraph && !canUseRenderGraph)
    {
        static bool warnedUnsupportedRenderGraphFrame = false;
        if (!warnedUnsupportedRenderGraphFrame)
        {
            LOG(Info, "RenderGraph renderer enabled but current frame uses unsupported feature '{0}'. Falling back to legacy renderer.", renderGraphUnsupportedReason);
            warnedUnsupportedRenderGraphFrame = true;
        }
    }
    if (canUseRenderGraph)
    {
        PROFILE_GPU_CPU_NAMED("RenderGraph Execute");

        // Create and build the render graph
        RenderGraph graph;
        Renderer::BuildRenderGraph(graph, renderContext, renderContextBatch);

        bool graphSucceeded = true;
        if (!graph.Compile())
        {
            LOG(Warning, "Failed to compile render graph. Falling back to legacy renderer.");
            graphSucceeded = false;
        }
        else if (!graph.Execute(context))
        {
            LOG(Warning, "Failed to execute render graph. Falling back to legacy renderer.");
            graphSucceeded = false;
        }
        else if (!PresentRenderGraphOutput(task, context, renderContext, graph))
        {
            LOG(Warning, "Failed to present render graph output. Falling back to legacy renderer.");
            graphSucceeded = false;
        }

        if (graphSucceeded)
        {
            static bool loggedRenderGraphActive = false;
            if (!loggedRenderGraphActive)
            {
                LOG(Info, "RenderGraph renderer path active.");
                loggedRenderGraphActive = true;
            }
            return;
        }
    }

    // Legacy rendering path (original hardcoded pipeline)
    // Get the light accumulation buffer
    auto outputFormat = renderContext.Buffers->GetOutputFormat();
    auto tempFlags = GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget;
    if (GPUDevice::Instance->Limits.HasCompute && EnumHasAllFlags(GPUDevice::Instance->GetFormatFeatures(outputFormat).Support, FormatSupport::UnorderedAccessReadOnly | FormatSupport::UnorderedAccessWriteOnly))
        tempFlags |= GPUTextureFlags::UnorderedAccess;
    auto tempDesc = GPUTextureDescription::New2D(renderContext.Buffers->GetWidth(), renderContext.Buffers->GetHeight(), outputFormat, tempFlags);
    auto lightBuffer = RenderTargetPool::Get(tempDesc);
    RENDER_TARGET_POOL_SET_NAME(lightBuffer, "LightBuffer");

#if USE_EDITOR
    if (renderContext.View.Mode == ViewMode::QuadOverdraw)
    {
        QuadOverdrawPass::Instance()->Render(renderContext, context, lightBuffer->View());
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        context->Draw(lightBuffer);
        RenderTargetPool::Release(lightBuffer);
        return;
    }
#endif

    // Global SDF rendering (can be used by materials later on)
    if (setup.UseGlobalSDF)
    {
        GlobalSignDistanceFieldPass::BindingData bindingData;
        GlobalSignDistanceFieldPass::Instance()->Render(renderContext, context, bindingData);
    }

    // Fill GBuffer
    GBufferPass::Instance()->Fill(renderContext, lightBuffer);

    // Debug drawing
    if (renderContext.View.Mode == ViewMode::GlobalSDF)
        GlobalSignDistanceFieldPass::Instance()->RenderDebug(renderContext, context, lightBuffer);
    else if (renderContext.View.Mode == ViewMode::GlobalSurfaceAtlas)
        GlobalSurfaceAtlasPass::Instance()->RenderDebug(renderContext, context, lightBuffer);
    if (renderContext.View.Mode == ViewMode::Emissive ||
        renderContext.View.Mode == ViewMode::VertexColors ||
        renderContext.View.Mode == ViewMode::LightmapUVsDensity ||
        renderContext.View.Mode == ViewMode::GlobalSurfaceAtlas ||
        renderContext.View.Mode == ViewMode::GlobalSDF)
    {
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        context->Draw(lightBuffer->View());
        RenderTargetPool::Release(lightBuffer);
        return;
    }
#if USE_EDITOR
    if (renderContext.View.Mode == ViewMode::MaterialComplexity)
    {
        GBufferPass::Instance()->DrawMaterialComplexity(renderContext, context, lightBuffer->View());
        RenderTargetPool::Release(lightBuffer);
        return;
    }
#endif

    // Render motion vectors
    MotionBlurPass::Instance()->RenderMotionVectors(renderContext);

    // Render ambient occlusion
    AmbientOcclusionPass::Instance()->Render(renderContext);

    // Check if use custom view mode
    if (isGBufferDebug)
    {
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        GBufferPass::Instance()->RenderDebug(renderContext);
        RenderTargetPool::Release(lightBuffer);
        return;
    }

    // Render lighting
    renderContextBatch.GetMainContext() = renderContext; // Sync render context in batch with the current value
    ShadowsPass::Instance()->RenderShadowMaps(renderContextBatch);
    LightPass::Instance()->RenderLights(renderContextBatch, *lightBuffer);
    if (EnumHasAnyFlags(renderContext.View.Flags, ViewFlags::GI))
    {
        switch (renderContext.List->Settings.GlobalIllumination.Mode)
        {
        case GlobalIlluminationMode::DDGI:
            DynamicDiffuseGlobalIlluminationPass::Instance()->Render(renderContext, context, *lightBuffer);
            break;
        }
    }
    if (renderContext.View.Mode == ViewMode::LightBuffer)
    {
        RenderLightBuffer(task, context, renderContext, lightBuffer, tempDesc);
        return;
    }

    // Material and Custom PostFx
    renderContext.List->RunPostFxPass(context, renderContext, MaterialPostFxLocation::BeforeReflectionsPass, PostProcessEffectLocation::BeforeReflectionsPass, lightBuffer);

    // Render reflections
    ReflectionsPass::Instance()->Render(renderContext, *lightBuffer);
    if (renderContext.View.Mode == ViewMode::Reflections)
    {
        renderContext.List->Settings.ToneMapping.Mode = ToneMappingMode::Neutral;
        renderContext.List->Settings.Bloom.Enabled = false;
        renderContext.List->Settings.LensFlares.Intensity = 0.0f;
        renderContext.List->Settings.CameraArtifacts.GrainAmount = 0.0f;
        renderContext.List->Settings.CameraArtifacts.ChromaticDistortion = 0.0f;
        renderContext.List->Settings.CameraArtifacts.VignetteIntensity = 0.0f;
        RenderLightBuffer(task, context, renderContext, lightBuffer, tempDesc);
        return;
    }

    // Material and Custom PostFx
    renderContext.List->RunPostFxPass(context, renderContext, MaterialPostFxLocation::BeforeForwardPass, PostProcessEffectLocation::BeforeForwardPass, lightBuffer);

    // Render fog
    context->ResetSR();
    if (renderContext.List->AtmosphericFog)
    {
        PROFILE_GPU_CPU("Atmospheric Fog");
        renderContext.List->AtmosphericFog->DrawFog(context, renderContext, *lightBuffer);
        context->ResetSR();
    }
    if (renderContext.List->Fog.Renderer)
    {
        VolumetricFogPass::Instance()->Render(renderContext);

        PROFILE_GPU_CPU("Fog");
        renderContext.List->Fog.Renderer->DrawFog(context, renderContext, *lightBuffer);
        context->ResetSR();
    }

    // Run forward pass
    auto frameBuffer = RenderTargetPool::Get(tempDesc);
    RENDER_TARGET_POOL_SET_NAME(frameBuffer, "FrameBuffer");
    ForwardPass::Instance()->Render(renderContext, lightBuffer, frameBuffer);

    // Material and Custom PostFx
    renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterForwardPass, frameBuffer, lightBuffer);
    renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::AfterForwardPass, frameBuffer, lightBuffer);

    // Cleanup
    context->ResetRenderTarget();
    context->ResetSR();
    context->FlushState();
    RenderTargetPool::Release(lightBuffer);

    // Check if skip post-processing
    if (renderContext.View.Mode == ViewMode::NoPostFx || renderContext.View.Mode == ViewMode::Wireframe)
    {
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(task->GetOutputViewport());
        if (!Graphics::GammaColorSpace)
            GBufferPass::Instance()->DrawLinearToSrgb(renderContext, frameBuffer);
        else
            context->Draw(frameBuffer);
        RenderTargetPool::Release(frameBuffer);
        return;
    }

    // Material and Custom PostFx
    auto tempBuffer = RenderTargetPool::Get(tempDesc);
    RENDER_TARGET_POOL_SET_NAME(tempBuffer, "TempBuffer");
    renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::BeforePostProcessingPass, frameBuffer, tempBuffer);
    renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::BeforePostProcessingPass, frameBuffer, tempBuffer);

    // Temporal Anti-Aliasing (goes before post processing)
    if (renderContext.List->Settings.AntiAliasing.Mode == AntialiasingMode::TemporalAntialiasing)
    {
        TAA::Instance()->Render(renderContext, frameBuffer, tempBuffer->View());
        Swap(frameBuffer, tempBuffer);
    }

    // Upscaling after scene rendering but before post processing
    bool useUpscaling = task->RenderingPercentage < 1.0f;
    const Viewport outputViewport = task->GetOutputViewport();
    if (useUpscaling && setup.UpscaleLocation == RenderingUpscaleLocation::BeforePostProcessingPass)
    {
        useUpscaling = false;
        RenderTargetPool::Release(tempBuffer);
        tempDesc.Width = (int32)outputViewport.Width;
        tempDesc.Height = (int32)outputViewport.Height;
        tempBuffer = RenderTargetPool::Get(tempDesc);
        context->ResetSR();
        if (renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::CustomUpscale))
            renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::CustomUpscale, frameBuffer, tempBuffer);
        else
            MultiScaler::Instance()->Upscale(context, outputViewport, frameBuffer, tempBuffer->View());
        if (tempBuffer->Width() == tempDesc.Width)
            Swap(frameBuffer, tempBuffer);
        RenderTargetPool::Release(tempBuffer);
        tempBuffer = RenderTargetPool::Get(tempDesc);
    }

    // Depth of Field
    DepthOfFieldPass::Instance()->Render(renderContext, frameBuffer, tempBuffer);

    // Motion Blur
    MotionBlurPass::Instance()->Render(renderContext, frameBuffer, tempBuffer);

    // Color Grading LUT generation
    auto colorGradingLUT = ColorGradingPass::Instance()->RenderLUT(renderContext);

    // Post-processing
    EyeAdaptationPass::Instance()->Render(renderContext, frameBuffer);
    PostProcessingPass::Instance()->Render(renderContext, frameBuffer, tempBuffer, colorGradingLUT);
    Swap(frameBuffer, tempBuffer);

    // Cleanup
    context->ResetRenderTarget();
    context->ResetSR();
    context->FlushState();

    // Custom Post Processing
    renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterPostProcessingPass, frameBuffer, tempBuffer);
    renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::Default, frameBuffer, tempBuffer);
    renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterCustomPostEffects, frameBuffer, tempBuffer);

    // Cleanup
    context->ResetRenderTarget();
    context->ResetSR();
    context->FlushState();

    // Debug motion vectors
    if (renderContext.View.Mode == ViewMode::MotionVectors)
    {
        context->ResetRenderTarget();
        context->SetRenderTarget(task->GetOutputView());
        context->SetViewportAndScissors(outputViewport);
        MotionBlurPass::Instance()->RenderDebug(renderContext, frameBuffer->View());
        RenderTargetPool::Release(tempBuffer);
        RenderTargetPool::Release(frameBuffer);
        return;
    }

    // Anti Aliasing
    GPUTextureView* outputView = task->GetOutputView();
    if (!renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::AfterAntiAliasingPass, MaterialPostFxLocation::AfterAntiAliasingPass) && !useUpscaling)
    {
        // AA -> Back Buffer
        RenderAntiAliasingPass(renderContext, frameBuffer, outputView, outputViewport);
    }
    else
    {
        // AA -> PostFx
        RenderAntiAliasingPass(renderContext, frameBuffer, *tempBuffer, Viewport(Float2(renderContext.View.ScreenSize)));
        context->ResetRenderTarget();
        Swap(frameBuffer, tempBuffer);
        renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::AfterAntiAliasingPass, frameBuffer, tempBuffer);
        renderContext.List->RunMaterialPostFxPass(context, renderContext, MaterialPostFxLocation::AfterAntiAliasingPass, frameBuffer, tempBuffer);

        // PostFx -> (up-scaling) -> Back Buffer
        if (!useUpscaling)
        {
            PROFILE_GPU("Copy frame");
            context->SetRenderTarget(outputView);
            context->SetViewportAndScissors(outputViewport);
            context->Draw(frameBuffer);
        }
        else if (renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::CustomUpscale))
        {
            if (outputView->GetParent()->Is<GPUTexture>())
            {
                // Upscale directly to the output texture
                auto outputTexture = (GPUTexture*)outputView->GetParent();
                renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::CustomUpscale, frameBuffer, outputTexture);
                if (frameBuffer == (GPUTexture*)outputView->GetParent())
                    Swap(frameBuffer, outputTexture);
            }
            else
            {
                // Use temporary buffer for upscaled frame if GetOutputView is owned by GPUSwapChain
                RenderTargetPool::Release(tempBuffer);
                tempDesc.Width = (int32)outputViewport.Width;
                tempDesc.Height = (int32)outputViewport.Height;
                tempBuffer = RenderTargetPool::Get(tempDesc);
                renderContext.List->RunCustomPostFxPass(context, renderContext, PostProcessEffectLocation::CustomUpscale, frameBuffer, tempBuffer);
                {
                    PROFILE_GPU("Copy frame");
                    context->SetRenderTarget(outputView);
                    context->SetViewportAndScissors(outputViewport);
                    context->Draw(frameBuffer);
                }
            }
        }
        else
        {
            MultiScaler::Instance()->Upscale(context, outputViewport, frameBuffer, outputView);
        }
    }

    RenderTargetPool::Release(tempBuffer);
    RenderTargetPool::Release(frameBuffer);
}

void Renderer::BuildRenderGraph(RenderGraph& graph, RenderContext& renderContext, RenderContextBatch& renderContextBatch)
{
    PROFILE_CPU_NAMED("Build RenderGraph");

    auto& view = renderContext.View;
    auto& setup = renderContext.List->Setup;
    const bool isGBufferDebug = GBufferPass::IsDebugView(view.Mode);

    // Clear any previous graph state
    graph.Clear();
    graph.SetContext(&renderContext, &renderContextBatch);

    auto addPass = [&graph](RenderGraphPass* pass)
    {
        graph.AddPass(pass, false, true);
    };

#if USE_EDITOR
    if (view.Mode == ViewMode::QuadOverdraw)
    {
        graph.AddPass(New<QuadOverdrawRenderGraphPass>(), true, true);
        return;
    }
#endif

    if (setup.UseGlobalSDF)
    {
        addPass(GlobalSignDistanceFieldPass::Instance());
    }

    if (setup.UseGlobalSurfaceAtlas)
    {
        addPass(GlobalSurfaceAtlasPass::Instance());
    }

    // GBuffer Pass - always needed for deferred rendering and GBuffer debug views.
    addPass(GBufferPass::Instance());

    if (view.Mode == ViewMode::GlobalSDF)
    {
        graph.AddPass(New<DebugOutputRenderGraphPass>(RenderGraphDebugOutput::GlobalSDF), true, true);
        return;
    }
    if (view.Mode == ViewMode::GlobalSurfaceAtlas)
    {
        graph.AddPass(New<DebugOutputRenderGraphPass>(RenderGraphDebugOutput::GlobalSurfaceAtlas), true, true);
        return;
    }
    if (view.Mode == ViewMode::Emissive ||
        view.Mode == ViewMode::VertexColors ||
        view.Mode == ViewMode::LightmapUVsDensity)
    {
        graph.AddPass(New<CopyTextureRenderGraphPass>(TEXT("DebugLightBufferCopyPass"), TEXT("LightBuffer")), true, true);
        return;
    }
    if (view.Mode == ViewMode::PhysicsColliders)
    {
        graph.AddPass(New<CopyTextureRenderGraphPass>(TEXT("PhysicsCollidersCopyPass"), TEXT("LightBuffer")), true, true);
        return;
    }
#if USE_EDITOR
    if (view.Mode == ViewMode::MaterialComplexity)
    {
        graph.AddPass(New<MaterialComplexityRenderGraphPass>(), true, true);
        return;
    }
#endif

    // Ambient Occlusion Pass
    if ((EnumHasAnyFlags(view.Flags, ViewFlags::AO) || view.Mode == ViewMode::AmbientOcclusion) &&
        renderContext.List->Settings.AmbientOcclusion.Enabled &&
        (!isGBufferDebug || view.Mode == ViewMode::AmbientOcclusion))
    {
        addPass(AmbientOcclusionPass::Instance());
    }

    // Shadow Maps Pass
    bool drawShadows = !isGBufferDebug && EnumHasAnyFlags(view.Flags, ViewFlags::Shadows) && ShadowsPass::Instance()->IsReady();
    switch (view.Mode)
    {
    case ViewMode::QuadOverdraw:
    case ViewMode::Emissive:
    case ViewMode::LightmapUVsDensity:
    case ViewMode::GlobalSurfaceAtlas:
    case ViewMode::GlobalSDF:
    case ViewMode::MaterialComplexity:
    case ViewMode::VertexColors:
        drawShadows = false;
        break;
    }
    if (drawShadows)
    {
        addPass(ShadowsPass::Instance());
    }

    if (setup.UseMotionVectors)
    {
        graph.AddPass(New<MotionVectorsRenderGraphPass>(), true, true);
    }

    // Light Pass - deferred lighting
    if (!isGBufferDebug)
    {
        addPass(LightPass::Instance());
    }

    if (!isGBufferDebug && EnumHasAnyFlags(view.Flags, ViewFlags::GI) && renderContext.List->Settings.GlobalIllumination.Mode == GlobalIlluminationMode::DDGI)
    {
        addPass(DynamicDiffuseGlobalIlluminationPass::Instance());
    }

    if (view.Mode == ViewMode::LightBuffer)
    {
        graph.AddPass(New<LightBufferPostRenderGraphPass>(), true, true);
        return;
    }

    if (!isGBufferDebug && renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::BeforeReflectionsPass, MaterialPostFxLocation::BeforeReflectionsPass))
    {
        graph.AddPass(New<PostFxRenderGraphPass>(TEXT("PostFxBeforeReflectionsPass"), MaterialPostFxLocation::BeforeReflectionsPass, PostProcessEffectLocation::BeforeReflectionsPass, PostFxRenderGraphTarget::LightBuffer), true, true);
    }

    // Reflections Pass
    if (EnumHasAnyFlags(view.Flags, ViewFlags::Reflections) && !isGBufferDebug)
    {
        addPass(ReflectionsPass::Instance());
    }

    if (view.Mode == ViewMode::Reflections)
    {
        graph.AddPass(New<LightBufferPostRenderGraphPass>(), true, true);
        return;
    }

    if (!isGBufferDebug && renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::BeforeForwardPass, MaterialPostFxLocation::BeforeForwardPass))
    {
        graph.AddPass(New<PostFxRenderGraphPass>(TEXT("PostFxBeforeForwardPass"), MaterialPostFxLocation::BeforeForwardPass, PostProcessEffectLocation::BeforeForwardPass, PostFxRenderGraphTarget::LightBuffer), true, true);
    }

    if (!isGBufferDebug && renderContext.List->Fog.Renderer)
    {
        graph.AddPass(New<FogRenderGraphPass>(), true, true);
    }

    // Forward Pass - for transparent objects and forward rendering
    if (!isGBufferDebug)
    {
        addPass(ForwardPass::Instance());
    }

    if (!isGBufferDebug && renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::AfterForwardPass, MaterialPostFxLocation::AfterForwardPass))
    {
        graph.AddPass(New<PostFxRenderGraphPass>(TEXT("PostFxAfterForwardPass"), MaterialPostFxLocation::AfterForwardPass, PostProcessEffectLocation::AfterForwardPass, PostFxRenderGraphTarget::InputFrame), true, true);
    }

    // Skip post-processing for certain view modes
    if (isGBufferDebug)
    {
        graph.AddPass(New<GBufferDebugRenderGraphPass>(), true, true);
        return;
    }
    if (view.Mode == ViewMode::NoPostFx ||
        view.Mode == ViewMode::Wireframe)
    {
        return;
    }

    if (renderContext.List->HasAnyPostFx(renderContext, PostProcessEffectLocation::BeforePostProcessingPass, MaterialPostFxLocation::BeforePostProcessingPass))
    {
        graph.AddPass(New<PostFxRenderGraphPass>(TEXT("PostFxBeforePostProcessingPass"), MaterialPostFxLocation::BeforePostProcessingPass, PostProcessEffectLocation::BeforePostProcessingPass, PostFxRenderGraphTarget::InputFrame), true, true);
    }

    if (renderContext.Task &&
        renderContext.Task->RenderingPercentage < 1.0f &&
        setup.UpscaleLocation == RenderingUpscaleLocation::BeforePostProcessingPass)
    {
        graph.AddPass(New<UpscaleRenderGraphPass>(), true, true);
    }

    // Temporal Anti-Aliasing runs on the HDR scene frame before post-processing.
    if (renderContext.List->Settings.AntiAliasing.Mode == AntialiasingMode::TemporalAntialiasing)
    {
        addPass(TAA::Instance());
    }

    if (EnumHasAnyFlags(view.Flags, ViewFlags::DepthOfField) &&
        renderContext.List->Settings.DepthOfField.Enabled)
    {
        addPass(DepthOfFieldPass::Instance());
    }

    if (EnumHasAnyFlags(view.Flags, ViewFlags::MotionBlur) &&
        renderContext.List->Settings.MotionBlur.Enabled &&
        renderContext.List->Settings.MotionBlur.Scale > ZeroTolerance)
    {
        addPass(MotionBlurPass::Instance());
    }

    if (view.Mode == ViewMode::MotionVectors)
    {
        graph.AddPass(New<DebugOutputRenderGraphPass>(RenderGraphDebugOutput::MotionVectors), true, true);
        return;
    }

    // Eye Adaptation Pass
    if (EnumHasAnyFlags(view.Flags, ViewFlags::EyeAdaptation))
    {
        addPass(EyeAdaptationPass::Instance());
    }

    // Post Processing Pass (bloom, tone mapping, camera artifacts, color grading, etc.).
    // Match the legacy path: the pass itself decides whether to apply effects or just copy the frame.
    addPass(PostProcessingPass::Instance());
}
