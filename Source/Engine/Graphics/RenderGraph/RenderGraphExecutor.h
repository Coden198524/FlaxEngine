// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphTypes.h"
#include "Engine/Core/Collections/Array.h"
#include "Engine/Core/Collections/Dictionary.h"
#include "Engine/Graphics/GPUResourceAccess.h"

// Forward declarations
class RenderGraph;
class RenderGraphPass;
class RenderGraphCompiler;
class GPUContext;
class GPUTexture;
class GPUBuffer;

/// <summary>
/// Render graph executor that executes compiled render graphs.
/// Handles pass scheduling, resource state transitions, GPU command submission, and synchronization.
/// </summary>
class FLAXENGINE_API RenderGraphExecutor
{
public:
    /// <summary>
    /// Resource state tracking information.
    /// </summary>
    struct ResourceState
    {
        /// <summary>
        /// Current GPU resource state.
        /// </summary>
        GPUResourceAccess Access;

        /// <summary>
        /// Last pass that accessed this resource.
        /// </summary>
        int32 LastAccessPass;

        ResourceState()
            : Access(GPUResourceAccess::None)
            , LastAccessPass(-1)
        {
        }

        ResourceState(GPUResourceAccess access)
            : Access(access)
            , LastAccessPass(-1)
        {
        }
    };

private:
    /// <summary>
    /// Current texture resource states.
    /// </summary>
    Dictionary<int32, ResourceState> _textureStates;

    /// <summary>
    /// Current buffer resource states.
    /// </summary>
    Dictionary<int32, ResourceState> _bufferStates;

    /// <summary>
    /// Whether async compute is enabled.
    /// </summary>
    bool _asyncComputeEnabled;

    /// <summary>
    /// Whether async copy is enabled.
    /// </summary>
    bool _asyncCopyEnabled;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraphExecutor"/> class.
    /// </summary>
    RenderGraphExecutor();

    /// <summary>
    /// Executes a compiled render graph.
    /// </summary>
    /// <param name="graph">The render graph to execute.</param>
    /// <param name="compiler">The compiler with compilation results.</param>
    /// <param name="context">The GPU context for command recording.</param>
    /// <returns>True if execution succeeded, false otherwise.</returns>
    bool Execute(RenderGraph* graph, RenderGraphCompiler* compiler, GPUContext* context);

    /// <summary>
    /// Enables or disables async compute support.
    /// </summary>
    /// <param name="enabled">True to enable async compute.</param>
    void SetAsyncComputeEnabled(bool enabled)
    {
        _asyncComputeEnabled = enabled;
    }

    /// <summary>
    /// Enables or disables async copy support.
    /// </summary>
    /// <param name="enabled">True to enable async copy.</param>
    void SetAsyncCopyEnabled(bool enabled)
    {
        _asyncCopyEnabled = enabled;
    }

    /// <summary>
    /// Gets whether async compute is enabled.
    /// </summary>
    FORCE_INLINE bool IsAsyncComputeEnabled() const
    {
        return _asyncComputeEnabled;
    }

    /// <summary>
    /// Gets whether async copy is enabled.
    /// </summary>
    FORCE_INLINE bool IsAsyncCopyEnabled() const
    {
        return _asyncCopyEnabled;
    }

    /// <summary>
    /// Clears all execution state.
    /// </summary>
    void Clear();

private:
    /// <summary>
    /// Executes a single pass.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <param name="pass">The pass to execute.</param>
    /// <param name="context">The GPU context.</param>
    void ExecutePass(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context);

    /// <summary>
    /// Transitions resources to required states before pass execution.
    /// </summary>
    /// <param name="graph">The render graph.</param>
    /// <param name="pass">The pass to prepare for.</param>
    /// <param name="context">The GPU context.</param>
    void TransitionResources(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context);

    /// <summary>
    /// Transitions a texture resource to a new state.
    /// </summary>
    /// <param name="context">The GPU context.</param>
    /// <param name="texture">The texture to transition.</param>
    /// <param name="textureIndex">The texture resource index.</param>
    /// <param name="newState">The new state.</param>
    void TransitionTexture(GPUContext* context, GPUTexture* texture, int32 textureIndex, GPUResourceAccess newAccess);

    /// <summary>
    /// Transitions a buffer resource to a new state.
    /// </summary>
    /// <param name="context">The GPU context.</param>
    /// <param name="buffer">The buffer to transition.</param>
    /// <param name="bufferIndex">The buffer resource index.</param>
    /// <param name="newState">The new state.</param>
    void TransitionBuffer(GPUContext* context, GPUBuffer* buffer, int32 bufferIndex, GPUResourceAccess newAccess);

    /// <summary>
    /// Determines the required GPU resource state for a resource access.
    /// </summary>
    /// <param name="access">The access mode.</param>
    /// <param name="isTexture">True if resource is a texture.</param>
    /// <returns>The required GPU resource state.</returns>
    GPUResourceAccess GetRequiredAccess(RenderGraphResourceAccess access, bool isTexture, bool isCompute) const;

    /// <summary>
    /// Inserts synchronization barriers between passes if needed.
    /// </summary>
    /// <param name="context">The GPU context.</param>
    /// <param name="previousPass">The previous pass.</param>
    /// <param name="currentPass">The current pass.</param>
    void InsertSynchronization(GPUContext* context, RenderGraphPass* previousPass, RenderGraphPass* currentPass);
};
