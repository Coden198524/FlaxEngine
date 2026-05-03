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

        // Transition resources to required states
        TransitionResources(graph, pass, context);

        // Execute the pass
        ExecutePass(graph, pass, context);

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

    PROFILE_CPU_NAMED(pass->GetName().Get());

    // Begin GPU event for debugging
    context->EventBegin(pass->GetName().Get());

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
            TransitionTexture(context, texture, texIndex, GetRequiredState(RenderGraphResourceAccess::Read, true));
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
            GPUResourceState newState = GPUResourceState::RenderTarget;
            
            // Check if it's a depth-stencil texture
            const auto& texDesc = graph->_textures[texIndex].Desc.Desc;
            if (texDesc.Format == PixelFormat::D24_UNorm_S8_UInt || 
                texDesc.Format == PixelFormat::D32_Float ||
                texDesc.Format == PixelFormat::D16_UNorm)
            {
                newState = GPUResourceState::DepthWrite;
            }
            // Check if it's a UAV texture
            else if ((texDesc.Flags & GPUTextureFlags::UnorderedAccess) != GPUTextureFlags::None)
            {
                newState = GPUResourceState::UnorderedAccess;
            }
            
            TransitionTexture(context, texture, texIndex, newState);
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
            TransitionBuffer(context, buffer, bufIndex, GetRequiredState(RenderGraphResourceAccess::Read, false));
    }

    // Transition buffers that are written
    for (int32 i = 0; i < pass->_bufferWrites.Count(); i++)
    {
        int32 bufIndex = pass->_bufferWrites[i].Index;
        if (bufIndex < 0 || bufIndex >= graph->GetBufferCount())
            continue;

        GPUBuffer* buffer = graph->GetBuffer(RenderGraphBufferRef(bufIndex));
        if (buffer)
            TransitionBuffer(context, buffer, bufIndex, GPUResourceState::UnorderedAccess);
    }
}

void RenderGraphExecutor::TransitionTexture(GPUContext* context, GPUTexture* texture, int32 textureIndex, GPUResourceState newState)
{
    if (!texture || !context)
        return;

    // Get current state
    ResourceState* currentState = _textureStates.TryGet(textureIndex);
    GPUResourceState oldState = currentState ? currentState->State : GPUResourceState::Common;

    // Skip if already in correct state
    if (oldState == newState)
        return;

    // Perform transition
    context->SetResourceState(texture, oldState, newState);

    // Update tracked state
    if (currentState)
    {
        currentState->State = newState;
    }
    else
    {
        _textureStates.Add(textureIndex, ResourceState(newState));
    }
}

void RenderGraphExecutor::TransitionBuffer(GPUContext* context, GPUBuffer* buffer, int32 bufferIndex, GPUResourceState newState)
{
    if (!buffer || !context)
        return;

    // Get current state
    ResourceState* currentState = _bufferStates.TryGet(bufferIndex);
    GPUResourceState oldState = currentState ? currentState->State : GPUResourceState::Common;

    // Skip if already in correct state
    if (oldState == newState)
        return;

    // Perform transition
    context->SetResourceState(buffer, oldState, newState);

    // Update tracked state
    if (currentState)
    {
        currentState->State = newState;
    }
    else
    {
        _bufferStates.Add(bufferIndex, ResourceState(newState));
    }
}

GPUResourceState RenderGraphExecutor::GetRequiredState(RenderGraphResourceAccess access, bool isTexture) const
{
    switch (access)
    {
    case RenderGraphResourceAccess::Read:
        return GPUResourceState::ShaderResource;
    case RenderGraphResourceAccess::Write:
        return isTexture ? GPUResourceState::RenderTarget : GPUResourceState::UnorderedAccess;
    case RenderGraphResourceAccess::ReadWrite:
        return GPUResourceState::UnorderedAccess;
    default:
        return GPUResourceState::Common;
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
