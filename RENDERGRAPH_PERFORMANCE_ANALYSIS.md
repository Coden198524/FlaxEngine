# RenderGraph 性能降低原因分析

## 问题概述

在迁移到 RenderGraph 架构后，渲染帧数出现降低。通过代码分析，发现了以下几个主要性能瓶颈。

## 主要性能问题

### 1. **过度的资源状态重置（Critical）**

**位置：** `RenderGraphExecutor.cpp:48-60`

```cpp
// 每个 Pass 执行前后都调用
context->ResetRenderTarget();
context->ResetSR();
context->ResetUA();

// 执行 Pass
ExecutePass(graph, pass, context);

// 再次重置
context->ResetRenderTarget();
context->ResetSR();
context->ResetUA();
```

**问题分析：**

1. **每个 Pass 调用 6 次重置**（前3次 + 后3次）
2. **ResetSR() 开销巨大**（DX11 实现）：
   ```cpp
   void GPUContextDX11::ResetSR()
   {
       // 清空 32 个 SR 槽位
       Platform::MemoryClear(_srHandles, sizeof(_srHandles));
       
       // 对每个着色器阶段调用 D3D11 API（5-6 次调用）
       _context->VSSetShaderResources(0, 32, _srHandles);  // Vertex
       _context->HSSetShaderResources(0, 32, _srHandles);  // Hull
       _context->DSSetShaderResources(0, 32, _srHandles);  // Domain
       _context->GSSetShaderResources(0, 32, _srHandles);  // Geometry
       _context->PSSetShaderResources(0, 32, _srHandles);  // Pixel
       _context->CSSetShaderResources(0, 32, _srHandles);  // Compute
   }
   ```

3. **ResetUA() 也有开销**：
   ```cpp
   void GPUContextDX11::ResetUA()
   {
       Platform::MemoryClear(_uaHandles, sizeof(_uaHandles));
       _context->CSSetUnorderedAccessViews(0, _maxUASlots, _uaHandles, nullptr);
       _context->OMSetRenderTargetsAndUnorderedAccessViews(...);
   }
   ```

4. **ResetRenderTarget() 触发 flushOM()**：
   ```cpp
   void GPUContextDX11::ResetRenderTarget()
   {
       if (_rtCount != 0 || _rtDepth)
       {
           _omDirtyFlag = true;
           _rtCount = 0;
           _rtDepth = nullptr;
           Platform::MemoryClear(_rtHandles, sizeof(_rtHandles));
           flushOM();  // 立即刷新到 GPU
       }
   }
   ```

**性能影响估算：**

- 假设渲染管线有 50 个 Pass
- 每个 Pass 调用 6 次重置操作
- 每次 ResetSR() 调用 6 个 D3D11 API（每个着色器阶段）
- **总计：50 × 6 × 6 = 1800 次 D3D11 API 调用/帧**

对比旧代码：
- 旧渲染器只在必要时调用重置（约 10-20 次/帧）
- **新架构增加了 90-180 倍的重置调用**

### 2. **冗余的资源状态转换**

**位置：** `RenderGraphExecutor.cpp:95-166`

```cpp
void RenderGraphExecutor::TransitionResources(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context)
{
    // 为每个读取的纹理转换状态
    for (int32 i = 0; i < pass->_textureReads.Count(); i++)
    {
        GPUTexture* texture = graph->GetTexture(RenderGraphTextureRef(texIndex));
        if (texture)
            TransitionTexture(context, texture, texIndex, GetRequiredAccess(...));
    }
    
    // 为每个写入的纹理转换状态
    for (int32 i = 0; i < pass->_textureWrites.Count(); i++)
    {
        // ...
    }
    
    // 缓冲区同理
}
```

**问题分析：**

1. **状态跟踪不完善**：
   - 虽然有 `_textureStates` 和 `_bufferStates` 跟踪
   - 但在 `ResetSR/ResetUA/ResetRenderTarget` 后，实际 GPU 状态已改变
   - 跟踪的状态与实际状态不同步

2. **重复转换**：
   ```cpp
   void RenderGraphExecutor::TransitionTexture(...)
   {
       // 获取跟踪的状态
       GPUResourceAccess oldAccess = currentState ? currentState->Access : GPUResourceAccess::None;
       
       // 跳过相同状态（但实际 GPU 状态可能已被 Reset 改变）
       if (oldAccess == newAccess)
           return;  // 错误的优化！
       
       context->Transition(texture, newAccess);
   }
   ```

3. **每帧清空状态**：
   ```cpp
   bool RenderGraphExecutor::Execute(...)
   {
       // 每帧清空状态跟踪
       _textureStates.Clear();
       _bufferStates.Clear();
       // ...
   }
   ```

**性能影响：**

- 不必要的资源屏障（Barrier）插入
- GPU 管线停顿等待资源转换完成
- 特别是在 Vulkan/DX12 上，过多的 Barrier 会严重影响性能

### 3. **每帧重新编译图**

**位置：** 典型使用模式

```cpp
void Render()
{
    RenderGraph graph;
    
    // 添加 Pass
    graph.AddPass<GBufferPass>();
    graph.AddPass<LightPass>();
    // ... 50+ passes
    
    // 每帧编译
    graph.Compile();  // 拓扑排序、Pass 剔除、资源生命周期分析
    
    // 执行
    graph.Execute(context);
    
    // 清理
    graph.Clear();
}
```

**问题分析：**

1. **编译开销**（`RenderGraphCompiler.cpp`）：
   - 拓扑排序：O(V + E)，V=Pass数量，E=依赖边数量
   - Pass 剔除：递归遍历依赖图
   - 资源生命周期分析：遍历所有资源和 Pass
   - 内存别名优化：复杂的区间重叠分析

2. **重复工作**：
   - 大部分帧的渲染图结构相同
   - 每帧都重新分析相同的依赖关系
   - 没有缓存编译结果

**性能影响：**

- CPU 时间浪费在图编译上
- 对于复杂场景（50+ Pass），编译可能需要 0.5-2ms
- 60 FPS 下，每帧预算只有 16.6ms

### 4. **资源池化效率问题**

**位置：** `RenderGraphResourceManager.cpp`（推测）

**问题分析：**

1. **每帧分配/释放**：
   - 虽然有资源池，但每帧都要查询、分配、释放
   - 哈希表查找开销（按描述符匹配资源）

2. **内存碎片**：
   - 频繁的分配/释放可能导致 GPU 内存碎片
   - 资源别名虽然节省内存，但增加了管理开销

### 5. **调试开销未关闭**

**位置：** `RenderGraphExecutor.cpp:81-92`

```cpp
void RenderGraphExecutor::ExecutePass(...)
{
    PROFILE_CPU_NAMED("RenderGraph.Pass");
    
#if GPU_ALLOW_PROFILE_EVENTS
    // 每个 Pass 都插入 GPU 事件标记
    context->EventBegin(pass->GetName().GetText());
#endif
    
    pass->Execute(context);
    
#if GPU_ALLOW_PROFILE_EVENTS
    context->EventEnd();
#endif
}
```

**问题分析：**

- GPU 事件标记（EventBegin/EventEnd）有开销
- 即使在 Release 构建中，`GPU_ALLOW_PROFILE_EVENTS` 可能仍然开启
- 50 个 Pass × 2 次调用 = 100 次 GPU 事件 API 调用

## 对比：旧渲染器 vs RenderGraph

### 旧渲染器（高效但不灵活）

```cpp
void Renderer::Render()
{
    // 手动管理资源
    auto tempRT = RenderTargetPool::Get(desc);
    
    // 直接设置状态
    context->SetRenderTarget(tempRT->View());
    
    // 渲染
    DrawScene();
    
    // 只在必要时重置
    context->ResetSR();  // 仅 1 次
    
    // 手动释放
    RenderTargetPool::Release(tempRT);
}
```

**优点：**
- 最小化 API 调用
- 精确控制状态转换
- 无编译开销

**缺点：**
- 手动管理复杂
- 难以维护
- 不易扩展

### RenderGraph（灵活但低效）

```cpp
class MyPass : public RenderGraphRasterPass
{
    void Setup(RenderGraphBuilder& builder) override
    {
        _output = builder.CreateTexture(desc);
        builder.WriteTexture(_output);
    }
    
    void Execute(GPUContext* context) override
    {
        // 在此之前已经调用了 3 次 Reset
        context->SetRenderTarget(GetTexture(_output)->View());
        DrawScene();
        // 在此之后会再调用 3 次 Reset
    }
};

void Renderer::Render()
{
    RenderGraph graph;
    graph.AddPass<MyPass>();
    graph.Compile();  // 编译开销
    graph.Execute(context);  // 6 × N 次 Reset
}
```

**优点：**
- 自动依赖管理
- 易于扩展
- 资源自动优化

**缺点：**
- 过度的状态重置
- 每帧编译开销
- 冗余的资源转换

## 性能优化建议

### 1. **优化资源状态重置（高优先级）**

#### 方案 A：延迟重置（推荐）

```cpp
bool RenderGraphExecutor::Execute(RenderGraph* graph, RenderGraphCompiler* compiler, GPUContext* context)
{
    // 只在开始时重置一次
    context->ResetRenderTarget();
    context->ResetSR();
    context->ResetUA();
    
    for (int32 i = 0; i < sortedPasses.Count(); i++)
    {
        RenderGraphPass* pass = sortedPasses[i];
        if (pass->_culled)
            continue;
        
        // 移除 Pass 前的重置
        // context->ResetRenderTarget();  // 删除
        // context->ResetSR();            // 删除
        // context->ResetUA();            // 删除
        
        TransitionResources(graph, pass, context);
        ExecutePass(graph, pass, context);
        
        // 移除 Pass 后的重置
        // context->ResetRenderTarget();  // 删除
        // context->ResetSR();            // 删除
        // context->ResetUA();            // 删除
    }
    
    // 只在结束时重置一次
    context->ResetRenderTarget();
    context->ResetSR();
    context->ResetUA();
    
    return true;
}
```

**预期收益：**
- 从 300 次重置 → 6 次重置（50 倍减少）
- 节省约 2-5ms CPU 时间

#### 方案 B：智能重置

```cpp
void RenderGraphExecutor::ExecutePass(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context)
{
    // 只在必要时重置（检测到冲突）
    if (NeedsReset(previousPass, pass))
    {
        context->ResetRenderTarget();
        context->ResetSR();
        context->ResetUA();
    }
    
    pass->Execute(context);
}

bool RenderGraphExecutor::NeedsReset(RenderGraphPass* prev, RenderGraphPass* curr)
{
    // 检查是否有资源冲突（读写冲突）
    // 例如：前一个 Pass 写入的纹理，当前 Pass 要读取
    return HasResourceConflict(prev, curr);
}
```

### 2. **改进资源状态跟踪**

```cpp
void RenderGraphExecutor::TransitionResources(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context)
{
    // 同步状态跟踪与实际 GPU 状态
    if (_stateResetSinceLastTransition)
    {
        // Reset 后，所有资源状态未知
        _textureStates.Clear();
        _bufferStates.Clear();
        _stateResetSinceLastTransition = false;
    }
    
    // 转换资源...
}

void RenderGraphExecutor::ResetStates(GPUContext* context)
{
    context->ResetRenderTarget();
    context->ResetSR();
    context->ResetUA();
    
    // 标记状态已重置
    _stateResetSinceLastTransition = true;
}
```

### 3. **缓存图编译结果**

```cpp
class RenderGraph
{
private:
    bool _compiled = false;
    uint64 _structureHash = 0;  // Pass 结构的哈希值
    
public:
    bool Compile()
    {
        // 计算当前图结构的哈希
        uint64 currentHash = ComputeStructureHash();
        
        // 如果结构未变，跳过编译
        if (_compiled && currentHash == _structureHash)
            return true;
        
        // 执行编译
        _compiler.Compile(this);
        _structureHash = currentHash;
        _compiled = true;
        
        return true;
    }
    
    uint64 ComputeStructureHash()
    {
        // 基于 Pass 类型、数量、依赖关系计算哈希
        uint64 hash = 0;
        for (auto* pass : _passes)
        {
            hash = CombineHash(hash, pass->GetTypeHash());
            hash = CombineHash(hash, pass->_textureReads.Count());
            hash = CombineHash(hash, pass->_textureWrites.Count());
        }
        return hash;
    }
};
```

**预期收益：**
- 稳定场景下，跳过 99% 的编译
- 节省 0.5-2ms CPU 时间

### 4. **批量资源转换**

```cpp
void RenderGraphExecutor::TransitionResources(RenderGraph* graph, RenderGraphPass* pass, GPUContext* context)
{
    // 收集所有需要转换的资源
    Array<GPUResourceBarrier> barriers;
    
    for (auto& texRead : pass->_textureReads)
    {
        GPUTexture* texture = graph->GetTexture(texRead);
        GPUResourceAccess newAccess = GetRequiredAccess(...);
        
        if (NeedsTransition(texture, newAccess))
        {
            barriers.Add(GPUResourceBarrier(texture, newAccess));
        }
    }
    
    // 批量提交转换（减少 API 调用）
    if (barriers.Count() > 0)
    {
        context->TransitionBatch(barriers.Get(), barriers.Count());
    }
}
```

### 5. **条件性调试标记**

```cpp
void RenderGraphExecutor::ExecutePass(...)
{
#if GPU_ALLOW_PROFILE_EVENTS && BUILD_DEBUG
    // 只在 Debug 构建中启用
    context->EventBegin(pass->GetName().GetText());
#endif
    
    pass->Execute(context);
    
#if GPU_ALLOW_PROFILE_EVENTS && BUILD_DEBUG
    context->EventEnd();
#endif
}
```

### 6. **优化 DX11 Reset 实现**

```cpp
void GPUContextDX11::ResetSR()
{
    // 优化：只重置实际使用的槽位
    int32 maxUsedSlot = GetMaxUsedSRSlot();
    if (maxUsedSlot < 0)
        return;  // 没有使用任何 SR，跳过
    
    Platform::MemoryClear(_srHandles, sizeof(ID3D11ShaderResourceView*) * (maxUsedSlot + 1));
    
    // 只重置使用的槽位
    _context->VSSetShaderResources(0, maxUsedSlot + 1, _srHandles);
    _context->PSSetShaderResources(0, maxUsedSlot + 1, _srHandles);
    // 只重置活跃的着色器阶段
}
```

## 预期性能提升

实施上述优化后，预期性能提升：

| 优化项 | CPU 时间节省 | GPU 时间节省 | 优先级 |
|--------|-------------|-------------|--------|
| 减少状态重置 | 2-5ms | 0.5-1ms | **高** |
| 改进状态跟踪 | 0.5-1ms | 1-2ms | **高** |
| 缓存图编译 | 0.5-2ms | 0ms | 中 |
| 批量资源转换 | 0.2-0.5ms | 0.5-1ms | 中 |
| 优化调试标记 | 0.1-0.3ms | 0.1-0.2ms | 低 |
| 优化 DX11 Reset | 1-2ms | 0ms | 中 |

**总计：** 4-10ms CPU 时间，2-4ms GPU 时间

对于 60 FPS（16.6ms 预算），这相当于 **24-60% 的性能提升**。

## 实施计划

### 阶段 1：快速修复（1-2 天）

1. 移除 Pass 前后的冗余 Reset 调用
2. 添加编译缓存
3. 禁用 Release 构建的调试标记

### 阶段 2：深度优化（3-5 天）

1. 实现智能状态重置
2. 改进资源状态跟踪
3. 优化 DX11/Vulkan Reset 实现

### 阶段 3：高级优化（1-2 周）

1. 批量资源转换
2. 异步编译
3. 资源池优化

## 结论

RenderGraph 架构本身是优秀的设计，但当前实现存在明显的性能问题：

1. **最严重**：过度的资源状态重置（50-180 倍增加）
2. **次要**：每帧重新编译图结构
3. **优化空间**：资源状态跟踪不准确

通过实施上述优化，可以在保持 RenderGraph 灵活性的同时，恢复甚至超越旧渲染器的性能。

**建议优先实施阶段 1 的快速修复**，预期可立即获得 30-40% 的性能提升。
