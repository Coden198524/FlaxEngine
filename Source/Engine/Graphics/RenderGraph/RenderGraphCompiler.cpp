// Copyright (c) Wojciech Figat. All rights reserved.

#include "RenderGraphCompiler.h"
#include "RenderGraph.h"
#include "RenderGraphPass.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Collections/Dictionary.h"

bool RenderGraphCompiler::Compile(RenderGraph* graph)
{
    if (!graph)
        return false;

    // Clear previous compilation data
    Clear();

    // Step 1: Cull unused passes
    CullPasses(graph);

    // Step 2: Determine execution order (topological sort)
    if (!DetermineExecutionOrder(graph))
    {
        LOG(Error, "RenderGraph compilation failed: cyclic dependency detected");
        return false;
    }

    // Step 3: Analyze resource lifetimes
    AnalyzeResourceLifetimes(graph);

    // Step 4: Optimize memory allocation
    OptimizeMemoryAllocation(graph);

    return true;
}

void RenderGraphCompiler::Clear()
{
    _sortedPasses.Clear();
    _textureLifetimes.Clear();
    _bufferLifetimes.Clear();
    _textureAliasing.Clear();
    _bufferAliasing.Clear();
}

void RenderGraphCompiler::CullPasses(RenderGraph* graph)
{
    // Algorithm:
    // 1. Start from passes marked as NeverCull or with exported outputs
    // 2. Recursively mark all dependencies as used
    // 3. Mark all other passes as culled
    
    HashSet<int32> usedPasses;
    
    // Mark all passes as culled initially
    for (int32 i = 0; i < graph->GetPassCount(); i++)
    {
        RenderGraphPass* pass = graph->GetPass(i);
        if (pass)
            pass->_culled = true;
    }
    
    // Find passes that cannot be culled
    for (int32 i = 0; i < graph->GetPassCount(); i++)
    {
        RenderGraphPass* pass = graph->GetPass(i);
        if (!pass)
            continue;
            
        // Check if pass cannot be culled
        if (!pass->CanCull() || (i < graph->_neverCullPasses.Count() && graph->_neverCullPasses[i]))
        {
            MarkPassAsUsed(graph, pass, usedPasses);
            continue;
        }
        
        // Check if pass has exported outputs
        bool hasExportedOutput = false;
        
        // Check texture writes
        for (int32 j = 0; j < pass->_textureWrites.Count(); j++)
        {
            int32 texIndex = pass->_textureWrites[j].Index;
            if (texIndex >= 0 && texIndex < graph->GetTextureCount())
            {
                const auto& texResource = graph->_textures[texIndex];
                if ((texResource.Desc.Flags & RenderGraphResourceFlags::Exported) != RenderGraphResourceFlags::None)
                {
                    hasExportedOutput = true;
                    break;
                }
            }
        }
        
        // Check buffer writes
        if (!hasExportedOutput)
        {
            for (int32 j = 0; j < pass->_bufferWrites.Count(); j++)
            {
                int32 bufIndex = pass->_bufferWrites[j].Index;
                if (bufIndex >= 0 && bufIndex < graph->GetBufferCount())
                {
                    const auto& bufResource = graph->_buffers[bufIndex];
                    if ((bufResource.Desc.Flags & RenderGraphResourceFlags::Exported) != RenderGraphResourceFlags::None)
                    {
                        hasExportedOutput = true;
                        break;
                    }
                }
            }
        }
        
        if (hasExportedOutput)
        {
            MarkPassAsUsed(graph, pass, usedPasses);
        }
    }
}

void RenderGraphCompiler::MarkPassAsUsed(RenderGraph* graph, RenderGraphPass* pass, HashSet<int32>& usedPasses)
{
    if (!pass || pass->_passIndex < 0)
        return;

    // Already marked
    if (usedPasses.Contains(pass->_passIndex))
        return;

    // Mark this pass as used
    usedPasses.Add(pass->_passIndex);
    pass->_culled = false;

    // Recursively mark all passes that produce resources this pass reads
    
    // For each texture this pass reads
    for (int32 i = 0; i < pass->_textureReads.Count(); i++)
    {
        int32 texIndex = pass->_textureReads[i].Index;
        if (texIndex >= 0 && texIndex < graph->GetTextureCount())
        {
            // Find the pass that produces this texture
            int32 producerPassIndex = graph->_textures[texIndex].ProducerPass;
            if (producerPassIndex >= 0 && producerPassIndex < graph->GetPassCount() && producerPassIndex != pass->_passIndex)
            {
                RenderGraphPass* producerPass = graph->GetPass(producerPassIndex);
                MarkPassAsUsed(graph, producerPass, usedPasses);
            }
        }
    }
    
    // For each buffer this pass reads
    for (int32 i = 0; i < pass->_bufferReads.Count(); i++)
    {
        int32 bufIndex = pass->_bufferReads[i].Index;
        if (bufIndex >= 0 && bufIndex < graph->GetBufferCount())
        {
            // Find the pass that produces this buffer
            int32 producerPassIndex = graph->_buffers[bufIndex].ProducerPass;
            if (producerPassIndex >= 0 && producerPassIndex < graph->GetPassCount() && producerPassIndex != pass->_passIndex)
            {
                RenderGraphPass* producerPass = graph->GetPass(producerPassIndex);
                MarkPassAsUsed(graph, producerPass, usedPasses);
            }
        }
    }
}

bool RenderGraphCompiler::DetermineExecutionOrder(RenderGraph* graph)
{
    // Topological sort using depth-first search
    HashSet<int32> visited;
    HashSet<int32> recursionStack;

    _sortedPasses.Clear();

    // Iterate through all non-culled passes in graph
    for (int32 i = 0; i < graph->GetPassCount(); i++)
    {
        RenderGraphPass* pass = graph->GetPass(i);
        if (!pass || pass->_culled)
            continue;
            
        if (!visited.Contains(pass->_passIndex))
        {
            if (!TopologicalSortDFS(graph, pass, visited, recursionStack))
                return false; // Cycle detected
        }
    }

    return true;
}

bool RenderGraphCompiler::TopologicalSortDFS(RenderGraph* graph, RenderGraphPass* pass, HashSet<int32>& visited, HashSet<int32>& recursionStack)
{
    if (!pass || pass->_culled)
        return true;

    // Check for cycle
    if (recursionStack.Contains(pass->_passIndex))
    {
        LOG(Error, "RenderGraph cycle detected at pass: {0}", pass->GetName());
        return false;
    }

    // Already visited
    if (visited.Contains(pass->_passIndex))
        return true;

    // Mark as being processed
    recursionStack.Add(pass->_passIndex);

    // Visit all dependencies (passes that write to resources this pass reads)
    
    // For each texture this pass reads
    for (int32 i = 0; i < pass->_textureReads.Count(); i++)
    {
        int32 texIndex = pass->_textureReads[i].Index;
        if (texIndex >= 0 && texIndex < graph->GetTextureCount())
        {
            // Find the pass that produces this texture
            int32 producerPassIndex = graph->_textures[texIndex].ProducerPass;
            if (producerPassIndex >= 0 && producerPassIndex < graph->GetPassCount() && producerPassIndex != pass->_passIndex)
            {
                RenderGraphPass* producerPass = graph->GetPass(producerPassIndex);
                if (!TopologicalSortDFS(graph, producerPass, visited, recursionStack))
                    return false;
            }
        }
    }
    
    // For each buffer this pass reads
    for (int32 i = 0; i < pass->_bufferReads.Count(); i++)
    {
        int32 bufIndex = pass->_bufferReads[i].Index;
        if (bufIndex >= 0 && bufIndex < graph->GetBufferCount())
        {
            // Find the pass that produces this buffer
            int32 producerPassIndex = graph->_buffers[bufIndex].ProducerPass;
            if (producerPassIndex >= 0 && producerPassIndex < graph->GetPassCount() && producerPassIndex != pass->_passIndex)
            {
                RenderGraphPass* producerPass = graph->GetPass(producerPassIndex);
                if (!TopologicalSortDFS(graph, producerPass, visited, recursionStack))
                    return false;
            }
        }
    }

    // Mark as visited
    visited.Add(pass->_passIndex);
    recursionStack.Remove(pass->_passIndex);

    // Add to sorted list
    _sortedPasses.Add(pass);

    return true;
}

void RenderGraphCompiler::AnalyzeResourceLifetimes(RenderGraph* graph)
{
    int32 textureCount = graph->GetTextureCount();
    int32 bufferCount = graph->GetBufferCount();

    _textureLifetimes.Resize(textureCount);
    _bufferLifetimes.Resize(bufferCount);

    // Initialize lifetimes
    for (int32 i = 0; i < textureCount; i++)
    {
        _textureLifetimes[i] = ResourceLifetime();
        const auto& texResource = graph->_textures[i];
        _textureLifetimes[i].IsImported = (texResource.Desc.Flags & RenderGraphResourceFlags::Imported) != RenderGraphResourceFlags::None;
        _textureLifetimes[i].IsExported = (texResource.Desc.Flags & RenderGraphResourceFlags::Exported) != RenderGraphResourceFlags::None;
    }

    for (int32 i = 0; i < bufferCount; i++)
    {
        _bufferLifetimes[i] = ResourceLifetime();
        const auto& bufResource = graph->_buffers[i];
        _bufferLifetimes[i].IsImported = (bufResource.Desc.Flags & RenderGraphResourceFlags::Imported) != RenderGraphResourceFlags::None;
        _bufferLifetimes[i].IsExported = (bufResource.Desc.Flags & RenderGraphResourceFlags::Exported) != RenderGraphResourceFlags::None;
    }

    // Analyze usage in sorted passes
    for (int32 passIndex = 0; passIndex < _sortedPasses.Count(); passIndex++)
    {
        RenderGraphPass* pass = _sortedPasses[passIndex];
        if (pass->_culled)
            continue;

        // Update texture lifetimes
        for (int32 i = 0; i < pass->_textureReads.Count(); i++)
        {
            int32 texIndex = pass->_textureReads[i].Index;
            if (texIndex >= 0 && texIndex < textureCount)
            {
                auto& lifetime = _textureLifetimes[texIndex];
                if (lifetime.FirstUse < 0)
                    lifetime.FirstUse = passIndex;
                lifetime.LastUse = passIndex;
            }
        }

        for (int32 i = 0; i < pass->_textureWrites.Count(); i++)
        {
            int32 texIndex = pass->_textureWrites[i].Index;
            if (texIndex >= 0 && texIndex < textureCount)
            {
                auto& lifetime = _textureLifetimes[texIndex];
                if (lifetime.FirstUse < 0)
                    lifetime.FirstUse = passIndex;
                lifetime.LastUse = passIndex;
            }
        }

        // Update buffer lifetimes
        for (int32 i = 0; i < pass->_bufferReads.Count(); i++)
        {
            int32 bufIndex = pass->_bufferReads[i].Index;
            if (bufIndex >= 0 && bufIndex < bufferCount)
            {
                auto& lifetime = _bufferLifetimes[bufIndex];
                if (lifetime.FirstUse < 0)
                    lifetime.FirstUse = passIndex;
                lifetime.LastUse = passIndex;
            }
        }

        for (int32 i = 0; i < pass->_bufferWrites.Count(); i++)
        {
            int32 bufIndex = pass->_bufferWrites[i].Index;
            if (bufIndex >= 0 && bufIndex < bufferCount)
            {
                auto& lifetime = _bufferLifetimes[bufIndex];
                if (lifetime.FirstUse < 0)
                    lifetime.FirstUse = passIndex;
                lifetime.LastUse = passIndex;
            }
        }
    }
}

void RenderGraphCompiler::OptimizeMemoryAllocation(RenderGraph* graph)
{
    int32 textureCount = _textureLifetimes.Count();
    int32 bufferCount = _bufferLifetimes.Count();

    _textureAliasing.Resize(textureCount);
    _bufferAliasing.Resize(bufferCount);

    // Initialize aliasing info
    for (int32 i = 0; i < textureCount; i++)
        _textureAliasing[i] = AliasingInfo();

    for (int32 i = 0; i < bufferCount; i++)
        _bufferAliasing[i] = AliasingInfo();

    // Simple aliasing algorithm: resources with non-overlapping lifetimes can share memory
    // TODO: Implement more sophisticated aliasing based on resource size and alignment
    
    // For textures
    for (int32 i = 0; i < textureCount; i++)
    {
        const auto& lifetime = _textureLifetimes[i];
        
        // Skip imported/exported resources
        if (lifetime.IsImported || lifetime.IsExported)
            continue;

        // Skip if no usage
        if (lifetime.FirstUse < 0)
            continue;

        // Try to find a resource to alias with
        for (int32 j = 0; j < i; j++)
        {
            const auto& otherLifetime = _textureLifetimes[j];
            
            // Skip if already aliased or imported/exported
            if (_textureAliasing[j].AliasedResourceIndex >= 0 || otherLifetime.IsImported || otherLifetime.IsExported)
                continue;

            // Check if lifetimes don't overlap
            if (otherLifetime.LastUse < lifetime.FirstUse || lifetime.LastUse < otherLifetime.FirstUse)
            {
                // Can alias with this resource
                _textureAliasing[i].AliasedResourceIndex = j;
                break;
            }
        }
    }

    // For buffers (same algorithm)
    for (int32 i = 0; i < bufferCount; i++)
    {
        const auto& lifetime = _bufferLifetimes[i];
        
        if (lifetime.IsImported || lifetime.IsExported)
            continue;

        if (lifetime.FirstUse < 0)
            continue;

        for (int32 j = 0; j < i; j++)
        {
            const auto& otherLifetime = _bufferLifetimes[j];
            
            if (_bufferAliasing[j].AliasedResourceIndex >= 0 || otherLifetime.IsImported || otherLifetime.IsExported)
                continue;

            if (otherLifetime.LastUse < lifetime.FirstUse || lifetime.LastUse < otherLifetime.FirstUse)
            {
                _bufferAliasing[i].AliasedResourceIndex = j;
                break;
            }
        }
    }
}
