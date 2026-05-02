# RenderGraph 架构文档

## 概述

RenderGraph 是 Flax Engine 的现代化渲染管线架构，提供自动依赖追踪、资源管理和优化执行。它取代了传统的硬编码渲染流程，提供更灵活、可维护和高性能的渲染系统。

### 核心优势

- **自动依赖管理**：Pass 之间的依赖关系自动推导，无需手动管理执行顺序
- **资源优化**：自动资源生命周期分析、内存池化和别名分配
- **Pass 剔除**：未使用的 Pass 自动剔除，减少不必要的计算
- **异步执行**：支持计算和拷贝队列的异步执行
- **调试友好**：内置可视化导出、性能分析和资源统计
- **易于扩展**：添加新 Pass 只需继承基类并声明资源依赖

## 架构设计

### 核心组件

```
RenderGraph (主图类)
├── RenderGraphPass (Pass 基类)
│   ├── RenderGraphRasterPass (光栅化 Pass)
│   ├── RenderGraphComputePass (计算 Pass)
│   └── RenderGraphCopyPass (拷贝 Pass)
├── RenderGraphBuilder (资源声明接口)
├── RenderGraphCompiler (图编译优化器)
├── RenderGraphExecutor (图执行引擎)
├── RenderGraphResourceManager (资源管理器)
└── RenderGraphDebug (调试工具)
```

### 执行流程

1. **构建阶段 (Build)**
   - 添加 Pass 到图中
   - 每个 Pass 调用 `Setup()` 声明资源依赖
   - 构建资源依赖图

2. **编译阶段 (Compile)**
   - 拓扑排序确定执行顺序
   - Pass 剔除（移除未使用的 Pass）
   - 资源生命周期分析
   - 资源别名分配（内存优化）

3. **执行阶段 (Execute)**
   - 按顺序分配资源
   - 执行每个 Pass 的 `Execute()` 方法
   - 管理资源状态转换
   - 提交 GPU 命令

## 快速入门

### 创建自定义 Pass

```cpp
// 1. 继承 RenderGraphPass 基类
class MyCustomPass : public RenderGraphRasterPass
{
private:
    RenderGraphTextureRef _inputTexture;
    RenderGraphTextureRef _outputTexture;
    
public:
    MyCustomPass() 
        : RenderGraphRasterPass(TEXT("MyCustomPass"))
    {
    }
    
    // 2. 实现 Setup 方法 - 声明资源依赖
    void Setup(RenderGraphBuilder& builder) override
    {
        // 读取输入纹理
        _inputTexture = builder.ReadTexture(_inputTexture);
        
        // 创建输出纹理
        auto desc = RenderGraphTextureDesc::Create2D(
            1920, 1080, 
            PixelFormat::R8G8B8A8_UNorm,
            GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
            TEXT("MyOutput")
        );
        _outputTexture = builder.CreateTexture(desc);
        builder.WriteTexture(_outputTexture);
    }
    
    // 3. 实现 Execute 方法 - 记录 GPU 命令
    void Execute(GPUContext* context) override
    {
        // 获取实际的 GPU 资源
        auto input = GetTexture(_inputTexture);
        auto output = GetTexture(_outputTexture);
        
        // 设置渲染目标
        context->SetRenderTarget(output->View());
        
        // 绑定输入资源
        context->BindSR(0, input);
        
        // 执行渲染命令
        context->DrawFullscreenTriangle();
        
        // 清理状态
        context->ResetRenderTarget();
        context->ResetSR();
    }
};
```

### 使用 RenderGraph

```cpp
// 创建 RenderGraph
RenderGraph graph;

// 添加 Pass
auto gbufferPass = graph.AddPass<GBufferPass>();
auto lightPass = graph.AddPass<LightPass>();
auto postProcessPass = graph.AddPass<PostProcessPass>();

// 编译图
if (!graph.Compile())
{
    LOG(Error, "Failed to compile render graph");
    return;
}

// 执行图
graph.Execute(context);

// 清理（下一帧重新构建）
graph.Clear();
```

## Pass 类型

### RasterPass (光栅化 Pass)

用于传统的光栅化渲染，使用渲染目标。

```cpp
class MyRasterPass : public RenderGraphRasterPass
{
public:
    MyRasterPass() : RenderGraphRasterPass(TEXT("MyRasterPass")) {}
    
    void Setup(RenderGraphBuilder& builder) override
    {
        // 声明渲染目标
        _colorTarget = builder.CreateTexture(desc);
        builder.WriteTexture(_colorTarget);
    }
    
    void Execute(GPUContext* context) override
    {
        // 设置渲染目标并绘制
        context->SetRenderTarget(GetTexture(_colorTarget)->View());
        // ... 绘制命令
    }
};
```

### ComputePass (计算 Pass)

用于计算着色器，可以异步执行。

```cpp
class MyComputePass : public RenderGraphComputePass
{
public:
    MyComputePass() : RenderGraphComputePass(TEXT("MyComputePass")) {}
    
    void Setup(RenderGraphBuilder& builder) override
    {
        // 声明 UAV 资源
        _buffer = builder.CreateBuffer(desc);
        builder.WriteBuffer(_buffer);
    }
    
    void Execute(GPUContext* context) override
    {
        // 绑定计算着色器和资源
        context->BindUA(0, GetBuffer(_buffer));
        context->Dispatch(threadGroupX, threadGroupY, threadGroupZ);
    }
};
```

### CopyPass (拷贝 Pass)

用于资源拷贝操作，可以异步执行。

```cpp
class MyCopyPass : public RenderGraphCopyPass
{
public:
    MyCopyPass() : RenderGraphCopyPass(TEXT("MyCopyPass")) {}
    
    void Setup(RenderGraphBuilder& builder) override
    {
        _source = builder.ReadTexture(_source);
        _dest = builder.WriteTexture(_dest);
    }
    
    void Execute(GPUContext* context) override
    {
        context->CopyTexture(GetTexture(_dest), GetTexture(_source));
    }
};
```

## 资源管理

### 创建资源

```cpp
void Setup(RenderGraphBuilder& builder) override
{
    // 创建 2D 纹理
    auto desc = RenderGraphTextureDesc::Create2D(
        width, height, 
        PixelFormat::R16G16B16A16_Float,
        GPUTextureFlags::ShaderResource | GPUTextureFlags::RenderTarget,
        TEXT("MyTexture")
    );
    _texture = builder.CreateTexture(desc);
    
    // 创建结构化缓冲区
    auto bufferDesc = RenderGraphBufferDesc::CreateStructured(
        1024, sizeof(MyStruct), true, TEXT("MyBuffer")
    );
    _buffer = builder.CreateBuffer(bufferDesc);
}
```

### 导入外部资源

```cpp
void Setup(RenderGraphBuilder& builder) override
{
    // 导入外部纹理（如 GBuffer）
    _depthBuffer = builder.ImportTexture(TEXT("DepthBuffer"), externalDepthTexture);
    builder.ReadTexture(_depthBuffer);
}
```

### 资源访问模式

```cpp
void Setup(RenderGraphBuilder& builder) override
{
    // 只读
    builder.ReadTexture(_inputTexture);
    
    // 只写
    builder.WriteTexture(_outputTexture);
    
    // 读写
    builder.ReadTexture(_rwTexture);
    builder.WriteTexture(_rwTexture);
}
```

## 高级特性

### Pass 标志

```cpp
// 永不剔除（即使输出未使用）
class ImportantPass : public RenderGraphRasterPass
{
public:
    ImportantPass() 
        : RenderGraphRasterPass(TEXT("ImportantPass"), 
                                RenderGraphPassFlags::Raster | 
                                RenderGraphPassFlags::NeverCull)
    {
    }
};

// 异步计算
class AsyncComputePass : public RenderGraphComputePass
{
public:
    AsyncComputePass() 
        : RenderGraphComputePass(TEXT("AsyncComputePass"))
    {
        // Compute flag 已自动设置
    }
};
```

### 资源别名

资源别名允许多个资源共享同一块 GPU 内存，前提是它们的生命周期不重叠。

```cpp
// 编译器自动分析资源生命周期并设置别名
// 无需手动干预，系统会自动优化内存使用
```

### 调试和可视化

```cpp
// 导出图结构为 Graphviz DOT 格式
RenderGraphDebug::ExportGraphviz(graph, TEXT("render_graph.dot"));

// 打印资源使用统计
RenderGraphDebug::PrintResourceStats(graph);

// 打印 Pass 性能统计
RenderGraphDebug::PrintPassStats(graph);
```

使用 Graphviz 可视化：
```bash
dot -Tpng render_graph.dot -o render_graph.png
```

## 性能优化

### 资源池化

RenderGraph 自动池化临时资源，减少分配开销：

```cpp
// 资源在 Pass 执行后自动返回池中
// 下一帧可以复用相同规格的资源
```

### Pass 剔除

未使用的 Pass 自动剔除：

```cpp
// 如果某个 Pass 的输出没有被任何其他 Pass 使用
// 且该 Pass 没有 NeverCull 标志，则会被自动剔除
```

### 内存优化

```cpp
// 使用 AllowAliasing 标志允许资源别名
auto desc = RenderGraphTextureDesc::Create2D(
    width, height, format, flags, TEXT("Temp")
);
desc.Flags |= RenderGraphResourceFlags::AllowAliasing;

// 使用 NoPooling 标志禁用池化（特殊资源）
desc.Flags |= RenderGraphResourceFlags::NoPooling;
```

## 迁移指南

### 从旧渲染管线迁移

**旧代码：**
```cpp
void MyPass::Render(RenderContext& renderContext, GPUContext* context)
{
    // 手动管理资源
    auto tempRT = RenderTargetPool::Get(desc);
    
    // 手动设置渲染目标
    context->SetRenderTarget(tempRT->View());
    
    // 渲染
    // ...
    
    // 手动释放资源
    RenderTargetPool::Release(tempRT);
}
```

**新代码：**
```cpp
class MyPass : public RenderGraphRasterPass
{
    RenderGraphTextureRef _tempRT;
    
public:
    void Setup(RenderGraphBuilder& builder) override
    {
        // 声明资源（自动管理）
        _tempRT = builder.CreateTexture(desc);
        builder.WriteTexture(_tempRT);
    }
    
    void Execute(GPUContext* context) override
    {
        // 获取资源（自动分配）
        context->SetRenderTarget(GetTexture(_tempRT)->View());
        
        // 渲染
        // ...
        
        // 无需手动释放（自动管理）
    }
};
```

### 关键变化

1. **资源声明**：从手动分配改为声明式
2. **依赖管理**：从手动排序改为自动推导
3. **生命周期**：从手动管理改为自动管理
4. **Pass 结构**：分离 Setup 和 Execute 阶段

## API 参考

### RenderGraph

```cpp
class RenderGraph
{
public:
    // 添加 Pass
    template<typename T, typename... Args>
    T* AddPass(Args&&... args);
    
    // 编译图
    bool Compile();
    
    // 执行图
    void Execute(GPUContext* context);
    
    // 清理图（准备下一帧）
    void Clear();
    
    // 获取资源
    GPUTexture* GetTexture(RenderGraphTextureRef handle);
    GPUBuffer* GetBuffer(RenderGraphBufferRef handle);
};
```

### RenderGraphBuilder

```cpp
class RenderGraphBuilder
{
public:
    // 创建资源
    RenderGraphTextureRef CreateTexture(const RenderGraphTextureDesc& desc);
    RenderGraphBufferRef CreateBuffer(const RenderGraphBufferDesc& desc);
    
    // 导入资源
    RenderGraphTextureRef ImportTexture(const String& name, GPUTexture* texture);
    RenderGraphBufferRef ImportBuffer(const String& name, GPUBuffer* buffer);
    
    // 声明访问
    void ReadTexture(RenderGraphTextureRef handle);
    void WriteTexture(RenderGraphTextureRef handle);
    void ReadBuffer(RenderGraphBufferRef handle);
    void WriteBuffer(RenderGraphBufferRef handle);
};
```

### RenderGraphPass

```cpp
class RenderGraphPass
{
public:
    // 构造函数
    RenderGraphPass(const String& name, RenderGraphPassFlags flags);
    
    // 必须实现的方法
    virtual void Setup(RenderGraphBuilder& builder) = 0;
    virtual void Execute(GPUContext* context) = 0;
    
    // 辅助方法
    GPUTexture* GetTexture(RenderGraphTextureRef handle);
    GPUBuffer* GetBuffer(RenderGraphBufferRef handle);
};
```

## 最佳实践

### 1. 资源命名

```cpp
// 使用描述性名称便于调试
auto desc = RenderGraphTextureDesc::Create2D(
    width, height, format, flags, 
    TEXT("GBuffer_Albedo")  // 清晰的名称
);
```

### 2. Pass 粒度

```cpp
// 好：单一职责的 Pass
class ShadowMapPass : public RenderGraphRasterPass { };
class ShadowFilterPass : public RenderGraphComputePass { };

// 避免：过大的 Pass
class AllShadowsPass : public RenderGraphRasterPass { }; // 太大
```

### 3. 资源复用

```cpp
// 好：让系统自动复用
auto temp1 = builder.CreateTexture(desc);
// ... 使用 temp1
// temp1 生命周期结束后，系统自动复用内存

// 避免：手动管理多个临时资源
```

### 4. 条件 Pass

```cpp
// 根据设置动态添加 Pass
if (settings.EnableSSAO)
{
    graph.AddPass<SSAOPass>();
}

if (settings.EnableBloom)
{
    graph.AddPass<BloomPass>();
}
```

### 5. 性能分析

```cpp
// 使用 PROFILE_GPU 宏
void Execute(GPUContext* context) override
{
    PROFILE_GPU("MyPass");
    // ... 渲染命令
}
```

## 常见问题

### Q: 如何在 Pass 之间传递数据？

A: 通过资源引用传递：

```cpp
class PassA : public RenderGraphRasterPass
{
public:
    RenderGraphTextureRef GetOutput() const { return _output; }
};

class PassB : public RenderGraphRasterPass
{
    RenderGraphTextureRef _input;
    
public:
    void SetInput(RenderGraphTextureRef input) { _input = input; }
    
    void Setup(RenderGraphBuilder& builder) override
    {
        builder.ReadTexture(_input);
    }
};

// 使用
auto passA = graph.AddPass<PassA>();
auto passB = graph.AddPass<PassB>();
passB->SetInput(passA->GetOutput());
```

### Q: 如何处理可选的输入？

A: 检查引用有效性：

```cpp
void Setup(RenderGraphBuilder& builder) override
{
    if (_optionalInput.IsValid())
    {
        builder.ReadTexture(_optionalInput);
    }
}
```

### Q: 如何导出最终结果？

A: 使用 ImportTexture 导入输出目标：

```cpp
// 导入 backbuffer
auto backbuffer = builder.ImportTexture(TEXT("Backbuffer"), renderTarget);
builder.WriteTexture(backbuffer);
```

### Q: Pass 执行顺序如何确定？

A: 自动根据资源依赖关系确定。如果 PassB 读取 PassA 的输出，则 PassA 必然在 PassB 之前执行。

### Q: 如何调试 Pass 被剔除的问题？

A: 使用调试工具：

```cpp
RenderGraphDebug::ExportGraphviz(graph, TEXT("debug.dot"));
// 查看哪些 Pass 被标记为 culled
```

## C# 绑定

RenderGraph 支持 C# 脚本化渲染：

```csharp
// 创建自定义 Pass
public class MyCustomPass : RenderGraphRasterPass
{
    private RenderGraphTextureRef output;
    
    public MyCustomPass() : base("MyCustomPass") { }
    
    public override void Setup(RenderGraphBuilder builder)
    {
        var desc = RenderGraphTextureDesc.Create2D(
            1920, 1080, PixelFormat.R8G8B8A8_UNorm, 
            GPUTextureFlags.ShaderResource | GPUTextureFlags.RenderTarget,
            "MyOutput"
        );
        output = builder.CreateTexture(desc);
        builder.WriteTexture(output);
    }
    
    public override void Execute(GPUContext context)
    {
        var texture = GetTexture(output);
        // ... 渲染命令
    }
}

// 使用
var graph = new RenderGraph();
graph.AddPass(new MyCustomPass());
graph.Compile();
graph.Execute(context);
```

## 示例：完整渲染管线

```cpp
void BuildRenderGraph(RenderGraph& graph, RenderContext& renderContext)
{
    // 1. GBuffer Pass
    auto gbuffer = graph.AddPass<GBufferPass>();
    
    // 2. Shadow Pass
    auto shadows = graph.AddPass<ShadowsPass>();
    
    // 3. Lighting Pass
    auto lighting = graph.AddPass<LightPass>();
    lighting->SetGBufferInput(gbuffer->GetGBuffer());
    lighting->SetShadowInput(shadows->GetShadowMap());
    
    // 4. SSAO Pass (可选)
    if (renderContext.View.Flags & ViewFlags::AO)
    {
        auto ssao = graph.AddPass<SSAOPass>();
        ssao->SetDepthInput(gbuffer->GetDepth());
        lighting->SetAOInput(ssao->GetOutput());
    }
    
    // 5. Post Processing
    auto postProcess = graph.AddPass<PostProcessPass>();
    postProcess->SetInput(lighting->GetOutput());
    
    // 6. TAA (可选)
    if (renderContext.View.Flags & ViewFlags::TAA)
    {
        auto taa = graph.AddPass<TAAPass>();
        taa->SetInput(postProcess->GetOutput());
        taa->SetMotionVectors(gbuffer->GetMotionVectors());
    }
    
    // 编译并执行
    graph.Compile();
    graph.Execute(renderContext.GPUContext);
}
```

## 独立渲染服务

并非所有渲染功能都需要迁移到 RenderGraph 架构。某些服务是独立的异步系统，有自己的调度和生命周期管理，不属于每帧渲染管线的一部分。

### 不需要迁移的服务

以下服务保持为独立系统，**不应**迁移到 RenderGraph：

#### 1. AtmospherePreCompute（大气散射预计算）

**为什么不迁移：**
- 异步预计算服务，跨多帧缓存结果
- 不是每帧渲染管线的一部分
- 按需触发，有自己的调度逻辑
- 使用独立的 SceneRenderTask 进行计算
- 结果通过 `GetCache()` 方法提供给其他 Pass

**使用方式：**
```cpp
// 在 RenderGraph Pass 中访问预计算数据
void MyAtmospherePass::Execute(GPUContext* context)
{
    AtmosphereCache cache;
    if (AtmospherePreCompute::GetCache(&cache))
    {
        // 使用预计算的纹理
        context->BindSR(0, cache.Transmittance);
        context->BindSR(1, cache.Irradiance);
        context->BindSR(2, cache.Inscatter);
    }
}
```

#### 2. ProbesRenderer（光照探针渲染）

**为什么不迁移：**
- 独立的异步烘焙服务，在后台运行
- 处理环境探针和天空光照的烘焙，有自己的调度和超时逻辑
- 探针按需烘焙并缓存，可能跨多帧完成
- 使用独立的 SceneRenderTask 渲染立方体贴图面
- 烘焙过程可以分布到多帧（通过 `MaxWorkPerFrame` 控制）

**使用方式：**
```cpp
// 请求烘焙环境探针
ProbesRenderer::Bake(environmentProbe, timeout);

// 请求烘焙天空光照
ProbesRenderer::Bake(skyLight, timeout);

// 烘焙完成后，数据自动存储在 Actor 属性中
// 光照 Pass 直接从 Actor 访问烘焙数据
```

### 设计原则

判断一个渲染功能是否应该迁移到 RenderGraph：

**应该迁移（作为 RenderGraph Pass）：**
- ✅ 每帧执行的渲染操作
- ✅ 需要与其他 Pass 共享资源
- ✅ 有明确的输入输出依赖关系
- ✅ 需要资源生命周期管理和优化

**保持独立（不迁移）：**
- ❌ 异步后台任务（预计算、烘焙）
- ❌ 跨多帧的长时间操作
- ❌ 有独立调度逻辑的服务
- ❌ 结果缓存并复用的系统
- ❌ 使用独立 RenderTask 的服务

### 与 RenderGraph 的集成

独立服务可以通过以下方式与 RenderGraph 集成：

1. **作为外部资源提供者**：预计算的纹理可以被 RenderGraph Pass 访问
2. **触发机制**：RenderGraph Pass 可以检测并触发预计算（如 `AtmospherePreCompute::GetCache()` 会自动触发更新）
3. **数据访问**：烘焙的数据存储在 Actor 属性中，Pass 直接访问

```cpp
// 示例：在 RenderGraph Pass 中使用独立服务的数据
class MyLightingPass : public RenderGraphRasterPass
{
public:
    void Execute(GPUContext* context) override
    {
        // 访问大气预计算数据
        AtmosphereCache atmosphere;
        if (AtmospherePreCompute::GetCache(&atmosphere))
        {
            context->BindSR(5, atmosphere.Inscatter);
        }
        
        // 访问环境探针数据（已烘焙并缓存在 Actor 中）
        for (auto* probe : environmentProbes)
        {
            auto probeTexture = probe->GetProbeTexture();
            // 使用探针纹理进行光照计算
        }
    }
};
```

## 总结

RenderGraph 提供了一个现代化、高效且易于使用的渲染管线架构。通过声明式的资源管理和自动依赖追踪，它大大简化了渲染代码的编写和维护，同时提供了出色的性能和灵活性。

关键要点：
- ✅ 使用声明式 API 而非命令式
- ✅ 让系统自动管理资源和依赖
- ✅ 保持 Pass 小而专注
- ✅ 利用调试工具进行优化
- ✅ 遵循最佳实践以获得最佳性能

## 相关文档

- `RenderGraph.h` - 主图类 API
- `RenderGraphPass.h` - Pass 基类 API
- `RenderGraphBuilder.h` - 资源声明 API
- `RenderGraphTypes.h` - 类型定义
- `TestRenderGraph.cpp` - 单元测试示例
- `RenderGraph.cs` - C# 绑定 API
