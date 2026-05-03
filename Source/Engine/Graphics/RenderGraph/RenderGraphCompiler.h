// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphTypes.h"
#include "Engine/Core/Collections/Array.h"
#include "Engine/Core/Collections/HashSet.h"

// Forward declarations
class RenderGraph;
class RenderGraphPass;

/// <summary>
/// Render graph compiler that optimizes the graph before execution.
/// Performs pass culling, resource lifetime analysis, memory allocation optimization, and execution order determination.
/// </summary>
class FLAXENGINE_API RenderGraphCompiler
{
public:
    /// <summary>
    /// Resource lifetime information.
    /// </summary>
    struct ResourceLifetime
    {
        /// <summary>
        /// First pass that uses this resource (index in sorted pass list).
        /// </summary>
        int32 FirstUse;

        /// <summary>
        /// Last pass that uses this resource (index in sorted pass list).
        /// </summary>
        int32 LastUse;

        /// <summary>
        /// Whether this resource is imported (external).
        /// </summary>
        bool IsImported;

        /// <summary>
        /// Whether this resource is exported (must persist).
        /// </summary>
        bool IsExported;

        ResourceLifetime()
            : FirstUse(-1)
            , LastUse(-1)
            , IsImported(false)
            , IsExported(false)
        {
        }
    };

    /// <summary>
    /// Memory aliasing information for resource optimization.
    /// </summary>
    struct AliasingInfo
    {
        /// <summary>
        /// Resource index that this resource can alias with.
        /// </summary>
        int32 AliasedResourceIndex;

        /// <summary>
        /// Memory offset for aliasing.
        /// </summary>
        uint64 MemoryOffset;

        AliasingInfo()
            : AliasedResourceIndex(-1)
            , MemoryOffset(0)
        {
        }
    };

private:
    /// <summary>
    /// Sorted list of passes to execute.
    /// </summary>
    Array<RenderGraphPass*> _sortedPasses;

    /// <summary>
    /// Texture resource lifetimes.
    /// </summary>
    Array<ResourceLifetime> _textureLifetimes;

    /// <summary>
    /// Buffer resource lifetimes.
    /// </summary>
    Array<ResourceLifetime> _bufferLifetimes;

    /// <summary>
    /// Texture aliasing information.
    /// </summary>
    Array<AliasingInfo> _textureAliasing;

    /// <summary>
    /// Buffer aliasing information.
    /// </summary>
    Array<AliasingInfo> _bufferAliasing;

public:
    /// <summary>
    /// Compiles the render graph.
    /// </summary>
    /// <param name="graph">The render graph to compile.</param>
    /// <returns>True if compilation succeeded, false otherwise.</returns>
    bool Compile(RenderGraph* graph);

    /// <summary>
    /// Gets the sorted list of passes to execute.
    /// </summary>
    FORCE_INLINE const Array<RenderGraphPass*>& GetSortedPasses() const
    {
        return _sortedPasses;
    }

    /// <summary>
    /// Gets the lifetime information for a texture resource.
    /// </summary>
    /// <param name="textureIndex">The texture resource index.</param>
    /// <returns>The lifetime information.</returns>
    FORCE_INLINE const ResourceLifetime& GetTextureLifetime(int32 textureIndex) const
    {
        return _textureLifetimes[textureIndex];
    }

    /// <summary>
    /// Gets the lifetime information for a buffer resource.
    /// </summary>
    /// <param name="bufferIndex">The buffer resource index.</param>
    /// <returns>The lifetime information.</returns>
    FORCE_INLINE const ResourceLifetime& GetBufferLifetime(int32 bufferIndex) const
    {
        return _bufferLifetimes[bufferIndex];
    }

    /// <summary>
    /// Gets the aliasing information for a texture resource.
    /// </summary>
    /// <param name="textureIndex">The texture resource index.</param>
    /// <returns>The aliasing information.</returns>
    FORCE_INLINE const AliasingInfo& GetTextureAliasing(int32 textureIndex) const
    {
        return _textureAliasing[textureIndex];
    }

    /// <summary>
    /// Gets the aliasing information for a buffer resource.
    /// </summary>
    /// <param name="bufferIndex">The buffer resource index.</param>
    /// <returns>The aliasing information.</returns>
    FORCE_INLINE const AliasingInfo& GetBufferAliasing(int32 bufferIndex) const
    {
        return _bufferAliasing[bufferIndex];
    }

    /// <summary>
    /// Clears all compilation data.
    /// </summary>
    void Clear();

private:
    /// <summary>
    /// Performs pass culling to remove unused passes.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    void CullPasses(RenderGraph* graph);

    /// <summary>
    /// Determines the execution order using topological sort.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <returns>True if successful (no cycles), false otherwise.</returns>
    bool DetermineExecutionOrder(RenderGraph* graph);

    /// <summary>
    /// Analyzes resource lifetimes.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    void AnalyzeResourceLifetimes(RenderGraph* graph);

    /// <summary>
    /// Optimizes memory allocation through resource aliasing.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    void OptimizeMemoryAllocation(RenderGraph* graph);

    /// <summary>
    /// Marks a pass and its dependencies as used (recursive).
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <param name="pass">The pass to mark.</param>
    /// <param name="usedPasses">Set of used pass indices.</param>
    void MarkPassAsUsed(RenderGraph* graph, RenderGraphPass* pass, HashSet<int32>& usedPasses);

    /// <summary>
    /// Performs topological sort using depth-first search.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <param name="pass">Current pass.</param>
    /// <param name="visited">Set of visited passes.</param>
    /// <param name="recursionStack">Set of passes in current recursion stack (for cycle detection).</param>
    /// <returns>True if no cycle detected, false otherwise.</returns>
    bool TopologicalSortDFS(RenderGraph* graph, RenderGraphPass* pass, HashSet<int32>& visited, HashSet<int32>& recursionStack);
};
