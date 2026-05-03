// Copyright (c) Wojciech Figat. All rights reserved.

#pragma once

#include "RenderGraphTypes.h"
#include "RenderGraphBuilder.h"
#include "Engine/Core/Collections/Array.h"
#include "Engine/Core/Collections/Dictionary.h"
#include "Engine/Core/Types/String.h"

// Forward declarations
class RenderGraphPass;
class RenderGraphCompiler;
class RenderGraphExecutor;
class RenderGraphResourceManager;
class GPUContext;
class GPUTexture;
class GPUBuffer;

/// <summary>
/// Render graph that manages rendering passes and resources.
/// Provides automatic dependency tracking, resource management, and optimized execution.
/// </summary>
class FLAXENGINE_API RenderGraph : public RenderGraphBuilder
{
    friend class RenderGraphCompiler;
    friend class RenderGraphExecutor;
    friend class RenderGraphResourceManager;

public:
    /// <summary>
    /// Internal texture resource information.
    /// </summary>
    struct TextureResource
    {
        /// <summary>
        /// The texture description.
        /// </summary>
        RenderGraphTextureDesc Desc;

        /// <summary>
        /// The actual GPU texture (allocated during execution).
        /// </summary>
        GPUTexture* Texture;

        /// <summary>
        /// The pass that produces this resource (writes to it first).
        /// </summary>
        int32 ProducerPass;

        /// <summary>
        /// Whether this resource is imported (external).
        /// </summary>
        bool IsImported;

        TextureResource()
            : Texture(nullptr)
            , ProducerPass(-1)
            , IsImported(false)
        {
        }
    };

    /// <summary>
    /// Internal buffer resource information.
    /// </summary>
    struct BufferResource
    {
        /// <summary>
        /// The buffer description.
        /// </summary>
        RenderGraphBufferDesc Desc;

        /// <summary>
        /// The actual GPU buffer (allocated during execution).
        /// </summary>
        GPUBuffer* Buffer;

        /// <summary>
        /// The pass that produces this resource (writes to it first).
        /// </summary>
        int32 ProducerPass;

        /// <summary>
        /// Whether this resource is imported (external).
        /// </summary>
        bool IsImported;

        BufferResource()
            : Buffer(nullptr)
            , ProducerPass(-1)
            , IsImported(false)
        {
        }
    };

private:
    /// <summary>
    /// All passes in the graph.
    /// </summary>
    Array<RenderGraphPass*> _passes;

    /// <summary>
    /// All texture resources in the graph.
    /// </summary>
    Array<TextureResource> _textures;

    /// <summary>
    /// All buffer resources in the graph.
    /// </summary>
    Array<BufferResource> _buffers;

    /// <summary>
    /// The compiler for graph optimization.
    /// </summary>
    RenderGraphCompiler* _compiler;

    /// <summary>
    /// The executor for graph execution.
    /// </summary>
    RenderGraphExecutor* _executor;

    /// <summary>
    /// The resource manager for resource allocation.
    /// </summary>
    RenderGraphResourceManager* _resourceManager;

    /// <summary>
    /// Whether the graph has been compiled.
    /// </summary>
    bool _compiled;

    /// <summary>
    /// Whether the graph is currently being built (during Setup phase).
    /// </summary>
    bool _building;

public:
    /// <summary>
    /// Initializes a new instance of the <see cref="RenderGraph"/> class.
    /// </summary>
    RenderGraph();

    /// <summary>
    /// Finalizes an instance of the <see cref="RenderGraph"/> class.
    /// </summary>
    ~RenderGraph();

public:
    /// <summary>
    /// Adds a pass to the render graph.
    /// </summary>
    /// <param name="pass">The pass to add (ownership is transferred to the graph).</param>
    /// <returns>The pass index.</returns>
    int32 AddPass(RenderGraphPass* pass);

    /// <summary>
    /// Compiles the render graph, performing optimization and dependency resolution.
    /// </summary>
    /// <returns>True if compilation succeeded, false otherwise.</returns>
    bool Compile();

    /// <summary>
    /// Executes the compiled render graph.
    /// </summary>
    /// <param name="context">The GPU context for command recording.</param>
    /// <returns>True if execution succeeded, false otherwise.</returns>
    bool Execute(GPUContext* context);

    /// <summary>
    /// Clears the render graph, removing all passes and resources.
    /// </summary>
    void Clear();

    /// <summary>
    /// Gets the number of passes in the graph.
    /// </summary>
    FORCE_INLINE int32 GetPassCount() const
    {
        return _passes.Count();
    }

    /// <summary>
    /// Gets a pass by index.
    /// </summary>
    /// <param name="index">The pass index.</param>
    /// <returns>The pass.</returns>
    FORCE_INLINE RenderGraphPass* GetPass(int32 index) const
    {
        return _passes[index];
    }

    /// <summary>
    /// Gets the number of texture resources in the graph.
    /// </summary>
    FORCE_INLINE int32 GetTextureCount() const
    {
        return _textures.Count();
    }

    /// <summary>
    /// Gets the number of buffer resources in the graph.
    /// </summary>
    FORCE_INLINE int32 GetBufferCount() const
    {
        return _buffers.Count();
    }

    /// <summary>
    /// Gets whether the graph has been compiled.
    /// </summary>
    FORCE_INLINE bool IsCompiled() const
    {
        return _compiled;
    }

    /// <summary>
    /// Gets the compiler.
    /// </summary>
    FORCE_INLINE RenderGraphCompiler* GetCompiler() const
    {
        return _compiler;
    }

    /// <summary>
    /// Gets the executor.
    /// </summary>
    FORCE_INLINE RenderGraphExecutor* GetExecutor() const
    {
        return _executor;
    }

    /// <summary>
    /// Gets the resource manager.
    /// </summary>
    FORCE_INLINE RenderGraphResourceManager* GetResourceManager() const
    {
        return _resourceManager;
    }

public:
    // RenderGraphBuilder interface implementation

    /// <summary>
    /// Creates a new texture resource in the render graph.
    /// </summary>
    /// <param name="desc">The texture description.</param>
    /// <returns>The texture reference.</returns>
    RenderGraphTextureRef CreateTexture(const RenderGraphTextureDesc& desc) override;

    /// <summary>
    /// Imports an external texture into the render graph.
    /// </summary>
    /// <param name="name">The resource name.</param>
    /// <param name="texture">The external texture.</param>
    /// <returns>The texture reference.</returns>
    RenderGraphTextureRef ImportTexture(const String& name, GPUTexture* texture) override;

    /// <summary>
    /// Creates a new buffer resource in the render graph.
    /// </summary>
    /// <param name="desc">The buffer description.</param>
    /// <returns>The buffer reference.</returns>
    RenderGraphBufferRef CreateBuffer(const RenderGraphBufferDesc& desc) override;

    /// <summary>
    /// Imports an external buffer into the render graph.
    /// </summary>
    /// <param name="name">The resource name.</param>
    /// <param name="buffer">The external buffer.</param>
    /// <returns>The buffer reference.</returns>
    RenderGraphBufferRef ImportBuffer(const String& name, GPUBuffer* buffer) override;

    /// <summary>
    /// Gets the actual GPU texture for a render graph texture reference.
    /// </summary>
    /// <param name="handle">The texture reference.</param>
    /// <returns>The GPU texture.</returns>
    GPUTexture* GetTexture(RenderGraphTextureRef handle) override;

    /// <summary>
    /// Gets the actual GPU buffer for a render graph buffer reference.
    /// </summary>
    /// <param name="handle">The buffer reference.</param>
    /// <returns>The GPU buffer.</returns>
    GPUBuffer* GetBuffer(RenderGraphBufferRef handle) override;

private:
    /// <summary>
    /// Builds the dependency graph by calling Setup on all passes.
    /// </summary>
    void BuildDependencies();

    /// <summary>
    /// Allocates physical resources for all virtual resources.
    /// </summary>
    void AllocateResources();

    /// <summary>
    /// Releases all allocated resources.
    /// </summary>
    void ReleaseResources();
};
