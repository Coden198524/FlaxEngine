// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphTypes.h"
#include "Engine/Core/Types/String.h"
#include "Engine/Core/Collections/Array.h"
#include "Engine/Core/Collections/Dictionary.h"

// Forward declarations
class RenderGraph;
class RenderGraphPass;
class RenderGraphCompiler;

/// <summary>
/// Render graph debugging and profiling utilities.
/// Provides graph visualization, performance analysis, resource usage statistics, and debug output.
/// </summary>
class FLAXENGINE_API RenderGraphDebug
{
public:
    /// <summary>
    /// Pass performance statistics.
    /// </summary>
    struct PassStats
    {
        /// <summary>
        /// Pass name.
        /// </summary>
        String Name;

        /// <summary>
        /// GPU execution time in milliseconds.
        /// </summary>
        float GpuTimeMs;

        /// <summary>
        /// CPU setup time in milliseconds.
        /// </summary>
        float CpuTimeMs;

        /// <summary>
        /// Number of draw calls.
        /// </summary>
        int32 DrawCalls;

        /// <summary>
        /// Number of dispatches.
        /// </summary>
        int32 Dispatches;

        /// <summary>
        /// Whether the pass was culled.
        /// </summary>
        bool Culled;

        PassStats()
            : GpuTimeMs(0.0f)
            , CpuTimeMs(0.0f)
            , DrawCalls(0)
            , Dispatches(0)
            , Culled(false)
        {
        }
    };

    /// <summary>
    /// Resource usage statistics.
    /// </summary>
    struct ResourceStats
    {
        /// <summary>
        /// Total number of textures.
        /// </summary>
        int32 TotalTextures;

        /// <summary>
        /// Total number of buffers.
        /// </summary>
        int32 TotalBuffers;

        /// <summary>
        /// Total texture memory in bytes.
        /// </summary>
        uint64 TotalTextureMemory;

        /// <summary>
        /// Total buffer memory in bytes.
        /// </summary>
        uint64 TotalBufferMemory;

        /// <summary>
        /// Number of aliased textures.
        /// </summary>
        int32 AliasedTextures;

        /// <summary>
        /// Number of aliased buffers.
        /// </summary>
        int32 AliasedBuffers;

        /// <summary>
        /// Memory saved through aliasing in bytes.
        /// </summary>
        uint64 MemorySavedByAliasing;

        ResourceStats()
            : TotalTextures(0)
            , TotalBuffers(0)
            , TotalTextureMemory(0)
            , TotalBufferMemory(0)
            , AliasedTextures(0)
            , AliasedBuffers(0)
            , MemorySavedByAliasing(0)
        {
        }
    };

private:
    /// <summary>
    /// Pass statistics collected during execution.
    /// </summary>
    Array<PassStats> _passStats;

    /// <summary>
    /// Resource usage statistics.
    /// </summary>
    ResourceStats _resourceStats;

    /// <summary>
    /// Whether debug mode is enabled.
    /// </summary>
    bool _debugEnabled;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphDebug"/> class.
    /// </summary>
    RenderGraphDebug();

    /// <summary>
    /// Enables or disables debug mode.
    /// </summary>
    /// <param name="enabled">True to enable debug mode.</param>
    void SetDebugEnabled(bool enabled)
    {
        _debugEnabled = enabled;
    }

    /// <summary>
    /// Gets whether debug mode is enabled.
    /// </summary>
    FORCE_INLINE bool IsDebugEnabled() const
    {
        return _debugEnabled;
    }

    /// <summary>
    /// Exports the render graph to Graphviz DOT format for visualization.
    /// </summary>
    /// <param name="graph">The render graph to export.</param>
    /// <param name="compiler">The compiler with compilation results.</param>
    /// <param name="outputPath">The output file path.</param>
    /// <returns>True if export succeeded, false otherwise.</returns>
    bool ExportToDot(RenderGraph* graph, RenderGraphCompiler* compiler, const String& outputPath);

    /// <summary>
    /// Collects performance statistics for all passes.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <param name="compiler">The compiler with compilation results.</param>
    void CollectPassStatistics(RenderGraph* graph, RenderGraphCompiler* compiler);

    /// <summary>
    /// Collects resource usage statistics.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <param name="compiler">The compiler with compilation results.</param>
    void CollectResourceStatistics(RenderGraph* graph, RenderGraphCompiler* compiler);

    /// <summary>
    /// Gets the pass statistics.
    /// </summary>
    FORCE_INLINE const Array<PassStats>& GetPassStats() const
    {
        return _passStats;
    }

    /// <summary>
    /// Gets the resource statistics.
    /// </summary>
    FORCE_INLINE const ResourceStats& GetResourceStats() const
    {
        return _resourceStats;
    }

    /// <summary>
    /// Prints debug information to the log.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <param name="compiler">The compiler with compilation results.</param>
    void PrintDebugInfo(RenderGraph* graph, RenderGraphCompiler* compiler);

    /// <summary>
    /// Prints pass statistics to the log.
    /// </summary>
    void PrintPassStatistics();

    /// <summary>
    /// Prints resource statistics to the log.
    /// </summary>
    void PrintResourceStatistics();

    /// <summary>
    /// Clears all collected statistics.
    /// </summary>
    void Clear();

private:
    /// <summary>
    /// Writes a DOT node for a pass.
    /// </summary>
    /// <param name="output">The output string.</param>
    /// <param name="pass">The pass.</param>
    /// <param name="passIndex">The pass index.</param>
    void WriteDotNode(String& output, RenderGraphPass* pass, int32 passIndex);

    /// <summary>
    /// Writes a DOT edge for a resource dependency.
    /// </summary>
    /// <param name="output">The output string.</param>
    /// <param name="fromPass">The source pass index.</param>
    /// <param name="toPass">The destination pass index.</param>
    /// <param name="resourceName">The resource name.</param>
    void WriteDotEdge(String& output, int32 fromPass, int32 toPass, const String& resourceName);

    /// <summary>
    /// Gets a color for a pass type in DOT format.
    /// </summary>
    /// <param name="pass">The pass.</param>
    /// <returns>The color string.</returns>
    String GetPassColor(RenderGraphPass* pass);

    /// <summary>
    /// Calculates memory size for a texture.
    /// </summary>
    /// <param name="textureIndex">The texture index.</param>
    /// <returns>The memory size in bytes.</returns>
    uint64 CalculateTextureMemory(int32 textureIndex);

    /// <summary>
    /// Calculates memory size for a buffer.
    /// </summary>
    /// <param name="bufferIndex">The buffer index.</param>
    /// <returns>The memory size in bytes.</returns>
    uint64 CalculateBufferMemory(int32 bufferIndex);
};
