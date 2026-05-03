// Copyright (c) Wojciech Figat. All rights reserved.

using System;
using System.Runtime.CompilerServices;

namespace FlaxEngine
{
    /// <summary>
    /// RenderGraph 资源句柄的基类。
    /// </summary>
    public struct RenderGraphResourceHandle
    {
        /// <summary>
        /// 资源索引。
        /// </summary>
        public int Index;

        /// <summary>
        /// 检查句柄是否有效。
        /// </summary>
        public bool IsValid => Index >= 0;

        /// <summary>
        /// 初始化资源句柄。
        /// </summary>
        /// <param name="index">资源索引。</param>
        public RenderGraphResourceHandle(int index)
        {
            Index = index;
        }
    }

    /// <summary>
    /// RenderGraph 纹理资源的引用。
    /// </summary>
    public struct RenderGraphTextureRef
    {
        /// <summary>
        /// 资源索引。
        /// </summary>
        public int Index;

        /// <summary>
        /// 检查句柄是否有效。
        /// </summary>
        public bool IsValid => Index >= 0;

        /// <summary>
        /// 初始化纹理引用。
        /// </summary>
        /// <param name="index">资源索引。</param>
        public RenderGraphTextureRef(int index)
        {
            Index = index;
        }

        /// <summary>
        /// 无效的纹理引用。
        /// </summary>
        public static readonly RenderGraphTextureRef Invalid = new RenderGraphTextureRef(-1);
    }

    /// <summary>
    /// RenderGraph 缓冲区资源的引用。
    /// </summary>
    public struct RenderGraphBufferRef
    {
        /// <summary>
        /// 资源索引。
        /// </summary>
        public int Index;

        /// <summary>
        /// 检查句柄是否有效。
        /// </summary>
        public bool IsValid => Index >= 0;

        /// <summary>
        /// 初始化缓冲区引用。
        /// </summary>
        /// <param name="index">资源索引。</param>
        public RenderGraphBufferRef(int index)
        {
            Index = index;
        }

        /// <summary>
        /// 无效的缓冲区引用。
        /// </summary>
        public static readonly RenderGraphBufferRef Invalid = new RenderGraphBufferRef(-1);
    }

    /// <summary>
    /// RenderGraph Pass 标志。
    /// </summary>
    [Flags]
    internal enum RenderGraphPassFlagsLegacy
    {
        /// <summary>
        /// 无标志。
        /// </summary>
        None = 0,

        /// <summary>
        /// 光栅化 Pass（渲染到 RenderTarget）。
        /// </summary>
        Raster = 1 << 0,

        /// <summary>
        /// 计算 Pass（执行计算着色器）。
        /// </summary>
        Compute = 1 << 1,

        /// <summary>
        /// 拷贝 Pass（资源拷贝操作）。
        /// </summary>
        Copy = 1 << 2,

        /// <summary>
        /// 永不剔除此 Pass（即使输出未被使用）。
        /// </summary>
        NeverCull = 1 << 3,

        /// <summary>
        /// 支持异步计算队列。
        /// </summary>
        AsyncCompute = 1 << 4,

        /// <summary>
        /// 支持异步拷贝队列。
        /// </summary>
        AsyncCopy = 1 << 5,
    }

    /// <summary>
    /// RenderGraph 纹理描述。
    /// </summary>
    public struct RenderGraphTextureDesc
    {
        /// <summary>
        /// 资源名称（用于调试）。
        /// </summary>
        public string Name;

        /// <summary>
        /// 纹理宽度。
        /// </summary>
        public int Width;

        /// <summary>
        /// 纹理高度。
        /// </summary>
        public int Height;

        /// <summary>
        /// 纹理深度（3D 纹理）。
        /// </summary>
        public int Depth;

        /// <summary>
        /// 数组大小（纹理数组）。
        /// </summary>
        public int ArraySize;

        /// <summary>
        /// Mip 级别数量。
        /// </summary>
        public int MipLevels;

        /// <summary>
        /// 多重采样数量。
        /// </summary>
        public int MultiSampleLevel;

        /// <summary>
        /// 像素格式。
        /// </summary>
        public PixelFormat Format;

        /// <summary>
        /// GPU 纹理标志。
        /// </summary>
        public GPUTextureFlags Flags;

        /// <summary>
        /// 创建默认的 2D 纹理描述。
        /// </summary>
        /// <param name="name">资源名称。</param>
        /// <param name="width">纹理宽度。</param>
        /// <param name="height">纹理高度。</param>
        /// <param name="format">像素格式。</param>
        /// <param name="flags">GPU 纹理标志。</param>
        /// <returns>纹理描述。</returns>
        public static RenderGraphTextureDesc Create2D(string name, int width, int height, PixelFormat format, GPUTextureFlags flags = GPUTextureFlags.ShaderResource | GPUTextureFlags.RenderTarget)
        {
            return new RenderGraphTextureDesc
            {
                Name = name,
                Width = width,
                Height = height,
                Depth = 1,
                ArraySize = 1,
                MipLevels = 1,
                MultiSampleLevel = 1,
                Format = format,
                Flags = flags,
            };
        }
    }

    /// <summary>
    /// RenderGraph 缓冲区描述。
    /// </summary>
    public struct RenderGraphBufferDesc
    {
        /// <summary>
        /// 资源名称（用于调试）。
        /// </summary>
        public string Name;

        /// <summary>
        /// 缓冲区大小（字节）。
        /// </summary>
        public int Size;

        /// <summary>
        /// 元素步长（字节）。
        /// </summary>
        public int Stride;

        /// <summary>
        /// GPU 缓冲区标志。
        /// </summary>
        public GPUBufferFlags Flags;

        /// <summary>
        /// 创建结构化缓冲区描述。
        /// </summary>
        /// <param name="name">资源名称。</param>
        /// <param name="elementCount">元素数量。</param>
        /// <param name="elementSize">元素大小（字节）。</param>
        /// <param name="flags">GPU 缓冲区标志。</param>
        /// <returns>缓冲区描述。</returns>
        public static RenderGraphBufferDesc CreateStructured(string name, int elementCount, int elementSize, GPUBufferFlags flags = GPUBufferFlags.ShaderResource)
        {
            return new RenderGraphBufferDesc
            {
                Name = name,
                Size = elementCount * elementSize,
                Stride = elementSize,
                Flags = flags,
            };
        }
    }

    /// <summary>
    /// RenderGraph 构建器接口，用于在 Pass 设置阶段声明资源。
    /// </summary>
    public abstract class RenderGraphBuilder
    {
        /// <summary>
        /// 在 RenderGraph 中创建新的纹理资源。
        /// </summary>
        /// <param name="desc">纹理描述。</param>
        /// <returns>纹理引用。</returns>
        public abstract RenderGraphTextureRef CreateTexture(RenderGraphTextureDesc desc);

        /// <summary>
        /// 将外部纹理导入到 RenderGraph 中。
        /// </summary>
        /// <param name="name">资源名称。</param>
        /// <param name="texture">外部纹理。</param>
        /// <returns>纹理引用。</returns>
        public abstract RenderGraphTextureRef ImportTexture(string name, GPUTexture texture);

        /// <summary>
        /// 在 RenderGraph 中创建新的缓冲区资源。
        /// </summary>
        /// <param name="desc">缓冲区描述。</param>
        /// <returns>缓冲区引用。</returns>
        public abstract RenderGraphBufferRef CreateBuffer(RenderGraphBufferDesc desc);

        /// <summary>
        /// 将外部缓冲区导入到 RenderGraph 中。
        /// </summary>
        /// <param name="name">资源名称。</param>
        /// <param name="buffer">外部缓冲区。</param>
        /// <returns>缓冲区引用。</returns>
        public abstract RenderGraphBufferRef ImportBuffer(string name, GPUBuffer buffer);

        /// <summary>
        /// 获取纹理引用对应的实际 GPU 纹理（仅在 Execute 阶段有效）。
        /// </summary>
        /// <param name="handle">纹理引用。</param>
        /// <returns>GPU 纹理。</returns>
        public abstract GPUTexture GetTexture(RenderGraphTextureRef handle);

        /// <summary>
        /// 获取缓冲区引用对应的实际 GPU 缓冲区（仅在 Execute 阶段有效）。
        /// </summary>
        /// <param name="handle">缓冲区引用。</param>
        /// <returns>GPU 缓冲区。</returns>
        public abstract GPUBuffer GetBuffer(RenderGraphBufferRef handle);
    }

    /// <summary>
    /// RenderGraph Pass 的基类。定义了 Pass 设置和执行的接口。
    /// </summary>
    public abstract class RenderGraphPass
    {
        /// <summary>
        /// Pass 名称（用于调试和性能分析）。
        /// </summary>
        public string Name { get; protected set; }

        /// <summary>
        /// Pass 执行标志。
        /// </summary>
        public RenderGraphPassFlags Flags { get; protected set; }

        /// <summary>
        /// 此 Pass 是否已被剔除。
        /// </summary>
        public bool IsCulled { get; internal set; }

        /// <summary>
        /// 初始化 RenderGraphPass。
        /// </summary>
        /// <param name="name">Pass 名称。</param>
        /// <param name="flags">Pass 标志。</param>
        protected RenderGraphPass(string name, RenderGraphPassFlags flags = RenderGraphPassFlags.Raster)
        {
            Name = name;
            Flags = flags;
            IsCulled = false;
        }

        /// <summary>
        /// 检查此 Pass 是否为光栅化 Pass。
        /// </summary>
        public bool IsRaster => (Flags & RenderGraphPassFlags.Raster) != 0;

        /// <summary>
        /// 检查此 Pass 是否为计算 Pass。
        /// </summary>
        public bool IsCompute => (Flags & RenderGraphPassFlags.Compute) != 0;

        /// <summary>
        /// 检查此 Pass 是否为拷贝 Pass。
        /// </summary>
        public bool IsCopy => (Flags & RenderGraphPassFlags.Copy) != 0;

        /// <summary>
        /// 检查此 Pass 是否可以被剔除。
        /// </summary>
        public bool CanCull => (Flags & RenderGraphPassFlags.NeverCull) == 0;

        /// <summary>
        /// 设置阶段：声明资源依赖并配置 Pass。
        /// 在图构建期间、编译之前调用。
        /// </summary>
        /// <param name="builder">用于声明资源的 RenderGraph 构建器。</param>
        public abstract void Setup(RenderGraphBuilder builder);

        /// <summary>
        /// 执行阶段：为此 Pass 记录 GPU 命令。
        /// 在图执行期间、编译之后调用。
        /// </summary>
        /// <param name="context">用于记录命令的 GPU 上下文。</param>
        public abstract void Execute(GPUContext context);
    }

    /// <summary>
    /// 光栅化 Pass，渲染到一个或多个 RenderTarget。
    /// </summary>
    public abstract class RenderGraphRasterPass : RenderGraphPass
    {
        /// <summary>
        /// 初始化 RenderGraphRasterPass。
        /// </summary>
        /// <param name="name">Pass 名称。</param>
        protected RenderGraphRasterPass(string name)
            : base(name, RenderGraphPassFlags.Raster)
        {
        }
    }

    /// <summary>
    /// 计算 Pass，调度计算着色器。
    /// </summary>
    public abstract class RenderGraphComputePass : RenderGraphPass
    {
        /// <summary>
        /// 初始化 RenderGraphComputePass。
        /// </summary>
        /// <param name="name">Pass 名称。</param>
        protected RenderGraphComputePass(string name)
            : base(name, RenderGraphPassFlags.Compute)
        {
        }
    }

    /// <summary>
    /// 拷贝 Pass，执行资源拷贝操作。
    /// </summary>
    public abstract class RenderGraphCopyPass : RenderGraphPass
    {
        /// <summary>
        /// 初始化 RenderGraphCopyPass。
        /// </summary>
        /// <param name="name">Pass 名称。</param>
        protected RenderGraphCopyPass(string name)
            : base(name, RenderGraphPassFlags.Copy)
        {
        }
    }

    /// <summary>
    /// RenderGraph 系统，用于声明式渲染管线构建和执行。
    /// </summary>
    /// <remarks>
    /// RenderGraph 提供了一种声明式的方式来构建渲染管线。
    /// 它自动处理资源生命周期、内存分配、Pass 调度和依赖解析。
    /// 
    /// 使用示例：
    /// <code>
    /// // 创建自定义 Pass
    /// class MyRenderPass : RenderGraphRasterPass
    /// {
    ///     private RenderGraphTextureRef _output;
    ///     
    ///     public MyRenderPass() : base("MyPass") { }
    ///     
    ///     public override void Setup(RenderGraphBuilder builder)
    ///     {
    ///         var desc = RenderGraphTextureDesc.Create2D("Output", 1920, 1080, PixelFormat.R8G8B8A8_UNorm);
    ///         _output = builder.CreateTexture(desc);
    ///     }
    ///     
    ///     public override void Execute(GPUContext context)
    ///     {
    ///         // 记录渲染命令
    ///     }
    /// }
    /// 
    /// // 使用 RenderGraph
    /// var graph = new RenderGraph();
    /// graph.AddPass(new MyRenderPass());
    /// graph.Compile();
    /// graph.Execute(context);
    /// </code>
    /// </remarks>
    public partial class RenderGraph
    {
        /// <summary>
        /// 添加 Pass 到 RenderGraph。
        /// </summary>
        /// <param name="pass">要添加的 Pass。</param>
        public void AddPass(RenderGraphPass pass)
        {
            if (pass == null)
                throw new ArgumentNullException(nameof(pass));
            
            Internal_AddPass(pass);
        }

        /// <summary>
        /// 编译 RenderGraph，执行 Pass 剔除和资源分配优化。
        /// </summary>
        public void Compile()
        {
            Internal_Compile();
        }

        /// <summary>
        /// 执行 RenderGraph，按照编译后的顺序执行所有 Pass。
        /// </summary>
        /// <param name="context">GPU 上下文。</param>
        public void Execute(GPUContext context)
        {
            if (context == null)
                throw new ArgumentNullException(nameof(context));
            
            Internal_Execute(context);
        }

        /// <summary>
        /// 清除 RenderGraph，移除所有 Pass 和资源。
        /// </summary>
        public void Clear()
        {
            Internal_Clear();
        }

        #region Internal Calls

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void Internal_AddPass(RenderGraphPass pass);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void Internal_Compile();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void Internal_Execute(GPUContext context);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private extern void Internal_Clear();

        #endregion
    }
}
