// Copyright (c) Wojciech Figat. All rights reserved.

#include "RenderGraphCompiler.h"
#include "RenderGraphPass.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Collections/Dictionary.h"

// Forward declaration - will be defined in RenderGraph.h
class RenderGraph;

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
    // Note: Implementation requires access to RenderGraph internals
    // This is a placeholder that will be completed when RenderGraph is available
    
    // Algorithm:
    // 1. Start from passes marked as NeverCull or with exported outputs
    // 2. Recursively mark all dependencies as used
    // 3. Mark all other passes as culled
    
    HashSet<int32> usedPasses;
    
    // TODO: Iterate through all passes in graph
    // For each pass that cannot be culled or has exported outputs:
    //   MarkPassAsUsed(graph, pass, usedPasses);
    
    // TODO: Mark all passes not in usedPasses as culled
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
    // TODO: Iterate through pass->_textureReads and pass->_bufferReads
    // Find the passes that write to those resources and mark them as used
}

bool RenderGraphCompiler::DetermineExecutionOrder(RenderGraph* graph)
{
    // Topological sort using depth-first search
    HashSet<int32> visited;
    HashSet<int32> recursionStack;

    _sortedPasses.Clear();

    // TODO: Iterate through all non-culled passes in graph
    // For each pass:
    //   if (!visited.Contains(pass->_passIndex))
    //   {
    //       if (!TopologicalSortDFS(graph, pass, visited, recursionStack))
    //           return false; // Cycle detected
    //   }

    // Reverse the list (DFS produces reverse topological order)
    _sortedPasses.Reverse();

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
    // TODO: For each resource this pass reads:
    //   Find the pass that writes to it
    //   if (!TopologicalSortDFS(graph, writerPass, visited, recursionStack))
    //       return false;

    // Mark as visited
    visited.Add(pass->_passIndex);
    recursionStack.Remove(pass->_passIndex);

    // Add to sorted list
    _sortedPasses.Add(pass);

    return true;
}

void RenderGraphCompiler::AnalyzeResourceLifetimes(RenderGraph* graph)
{
    // TODO: Get texture and buffer counts from graph
    int32 textureCount = 0; // graph->GetTextureCount();
    int32 bufferCount = 0;  // graph->GetBufferCount();

    _textureLifetimes.Resize(textureCount);
    _bufferLifetimes.Resize(bufferCount);

    // Initialize lifetimes
    for (int32 i = 0; i < textureCount; i++)
    {
        _textureLifetimes[i] = ResourceLifetime();
        // TODO: Check if texture is imported/exported
    }

    for (int32 i = 0; i < bufferCount; i++)
    {
        _bufferLifetimes[i] = ResourceLifetime();
        // TODO: Check if buffer is imported/exported
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
