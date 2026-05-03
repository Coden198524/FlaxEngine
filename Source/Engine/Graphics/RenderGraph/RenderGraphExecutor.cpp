// Copyright (c) Wojciech Figat. All rights reserved.

#include "RenderGraphExecutor.h"
#include "RenderGraph.h"
#include "RenderGraphPass.h"
#include "RenderGraphCompiler.h"
#include "Engine/Graphics/GPUContext.h"
#include "Engine/Graphics/Textures/GPUTexture.h"
#include "Engine/Graphics/GPUBuffer.h"
#include "Engine/Core/Log.h"
#include "Engine/Profiler/ProfilerCPU.h"

RenderGraphExecutor::RenderGraphExecutor()
    : _asyncComputeEnabled(false)
    , _asyncCopyEnabled(false)
{
}

bool RenderGraphExecutor::Execute(RenderGraph* graph, RenderGraphCompiler* compiler, GPUContext* context)
{
    if (!graph || !compiler || !context)
        return false;

    PROFILE_CPU_NAMED("RenderGraph.Execute");

    // Clear previous state
    _textureStates.Clear();
    _bufferStates.Clear();

    // Get sorted passes from compiler
    const auto& sortedPasses = compiler->GetSortedPasses();

    // Execute passes in order
    RenderGraphPass* previousPass = nullptr;
    for (int32 i = 0; i < sortedPasses.Count(); i++)
    {
        RenderGraphPass* pass = sortedPasses[i];
        
        // Skip culled passes
        if (pass->_culled)
            continue;

        // Insert synchronization if needed
        if (previousPass)
            InsertSynchronization(context, previousPass, pass);

        // Keep D3D-style APIs from carrying stale SRV/UAV/RT bindings across graph passes.
        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();

        // Transition resources to required states
        TransitionResources(graph, pass, context);

        // Execute the pass
        ExecutePass(graph, pass, context);

        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();

        previousPass = pass;
    }

    return true;
}

void RenderGraphExecutor::Clear()
{
    _textureStates.Clear();
    _bufferStates.Clear();
}

void RenderGraphExecutor::ExecutePass(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context)
{
    if (!pass || !context)
        return;

    PROFILE_CPU_NAMED("RenderGraph.Pass");

    // Begin GPU event for debugging
    context->EventBegin(pass->GetName().GetText());

    // Execute the pass
    pass->Execute(context);

    // End GPU event
    context->EventEnd();
}

void RenderGraphExecutor::TransitionResources(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context)
{
    if (!pass || !context)
        return;

    // Transition textures that are read
    for (int32 i = 0; i < pass->_textureReads.Count(); i++)
    {
        int32 texIndex = pass->_textureReads[i].Index;
        if (texIndex < 0 || texIndex >= graph->GetTextureCount())
            continue;

        GPUTexture* texture = graph->GetTexture(RenderGraphTextureRef(texIndex));
        if (texture)
            TransitionTexture(context, texture, texIndex, GetRequiredAccess(RenderGraphResourceAccess::Read, true, pass->IsCompute()));
    }

    // Transition textures that are written
    for (int32 i = 0; i < pass->_textureWrites.Count(); i++)
    {
        int32 texIndex = pass->_textureWrites[i].Index;
        if (texIndex < 0 || texIndex >= graph->GetTextureCount())
            continue;

        GPUTexture* texture = graph->GetTexture(RenderGraphTextureRef(texIndex));
        if (texture)
        {
            // Determine state based on usage
            GPUResourceAccess newAccess = GPUResourceAccess::RenderTarget;
            
            // Check if it's a depth-stencil texture
            const auto& texDesc = graph->_textures[texIndex].Desc.Desc;
            if (texDesc.Format == PixelFormat::D24_UNorm_S8_UInt || 
                texDesc.Format == PixelFormat::D32_Float ||
                texDesc.Format == PixelFormat::D16_UNorm)
            {
                newAccess = GPUResourceAccess::DepthWrite;
            }
            // Check if it's a UAV texture
            else if ((texDesc.Flags & GPUTextureFlags::UnorderedAccess) != GPUTextureFlags::None)
            {
                newAccess = GPUResourceAccess::UnorderedAccess;
            }
            
            TransitionTexture(context, texture, texIndex, newAccess);
        }
    }

    // Transition buffers that are read
    for (int32 i = 0; i < pass->_bufferReads.Count(); i++)
    {
        int32 bufIndex = pass->_bufferReads[i].Index;
        if (bufIndex < 0 || bufIndex >= graph->GetBufferCount())
            continue;

        GPUBuffer* buffer = graph->GetBuffer(RenderGraphBufferRef(bufIndex));
        if (buffer)
            TransitionBuffer(context, buffer, bufIndex, GetRequiredAccess(RenderGraphResourceAccess::Read, false, pass->IsCompute()));
    }

    // Transition buffers that are written
    for (int32 i = 0; i < pass->_bufferWrites.Count(); i++)
    {
        int32 bufIndex = pass->_bufferWrites[i].Index;
        if (bufIndex < 0 || bufIndex >= graph->GetBufferCount())
            continue;

        GPUBuffer* buffer = graph->GetBuffer(RenderGraphBufferRef(bufIndex));
        if (buffer)
            TransitionBuffer(context, buffer, bufIndex, GPUResourceAccess::UnorderedAccess);
    }
}

void RenderGraphExecutor::TransitionTexture(GPUContext* context, GPUTexture* texture, int32 textureIndex, GPUResourceAccess newAccess)
{
    if (!texture || !context)
        return;

    // Get current access
    ResourceState* currentState = _textureStates.TryGet(textureIndex);
    GPUResourceAccess oldAccess = currentState ? currentState->Access : GPUResourceAccess::None;

    // Skip if already in correct access
    if (oldAccess == newAccess)
        return;

    // Perform transition
    context->Transition(texture, newAccess);

    // Update tracked access
    if (currentState)
    {
        currentState->Access = newAccess;
    }
    else
    {
        _textureStates.Add(textureIndex, ResourceState(newAccess));
    }
}

void RenderGraphExecutor::TransitionBuffer(GPUContext* context, GPUBuffer* buffer, int32 bufferIndex, GPUResourceAccess newAccess)
{
    if (!buffer || !context)
        return;

    // Get current access
    ResourceState* currentState = _bufferStates.TryGet(bufferIndex);
    GPUResourceAccess oldAccess = currentState ? currentState->Access : GPUResourceAccess::None;

    // Skip if already in correct access
    if (oldAccess == newAccess)
        return;

    // Perform transition
    context->Transition(buffer, newAccess);

    // Update tracked access
    if (currentState)
    {
        currentState->Access = newAccess;
    }
    else
    {
        _bufferStates.Add(bufferIndex, ResourceState(newAccess));
    }
}

GPUResourceAccess RenderGraphExecutor::GetRequiredAccess(RenderGraphResourceAccess access, bool isTexture, bool isCompute) const
{
    switch (access)
    {
    case RenderGraphResourceAccess::Read:
        return isCompute ? GPUResourceAccess::ShaderReadCompute : GPUResourceAccess::ShaderReadGraphics;
    case RenderGraphResourceAccess::Write:
        return isTexture ? GPUResourceAccess::RenderTarget : GPUResourceAccess::UnorderedAccess;
    case RenderGraphResourceAccess::ReadWrite:
        return GPUResourceAccess::UnorderedAccess;
    default:
        return GPUResourceAccess::None;
    }
}

void RenderGraphExecutor::InsertSynchronization(GPUContext* context, RenderGraphPass* previousPass, RenderGraphPass* currentPass)
{
    if (!context || !previousPass || !currentPass)
        return;

    // Check if passes can run in parallel
    bool canRunInParallel = false;

    // Async compute: compute passes can overlap with raster passes
    if (_asyncComputeEnabled)
    {
        if (previousPass->IsRaster() && currentPass->IsCompute())
            canRunInParallel = true;
        else if (previousPass->IsCompute() && currentPass->IsRaster())
            canRunInParallel = true;
    }

    // Async copy: copy passes can overlap with other passes
    if (_asyncCopyEnabled)
    {
        if (previousPass->IsCopy() || currentPass->IsCopy())
            canRunInParallel = true;
    }

    // If passes cannot run in parallel, insert a barrier
    if (!canRunInParallel)
    {
        // Check for resource dependencies
        bool hasResourceDependency = false;

        // Check if current pass reads what previous pass wrote
        for (int32 i = 0; i < currentPass->_textureReads.Count(); i++)
        {
            for (int32 j = 0; j < previousPass->_textureWrites.Count(); j++)
            {
                if (currentPass->_textureReads[i] == previousPass->_textureWrites[j])
                {
                    hasResourceDependency = true;
                    break;
                }
            }
            if (hasResourceDependency)
                break;
        }

        if (!hasResourceDependency)
        {
            for (int32 i = 0; i < currentPass->_bufferReads.Count(); i++)
            {
                for (int32 j = 0; j < previousPass->_bufferWrites.Count(); j++)
                {
                    if (currentPass->_bufferReads[i] == previousPass->_bufferWrites[j])
                    {
                        hasResourceDependency = true;
                        break;
                    }
                }
                if (hasResourceDependency)
                    break;
            }
        }

        // Insert barrier if there's a dependency
        if (hasResourceDependency)
        {
            context->FlushState();
        }
    }
}
