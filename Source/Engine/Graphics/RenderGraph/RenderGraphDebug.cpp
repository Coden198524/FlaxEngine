// Copyright (c) Wojciech Figat. All rights reserved.

#include "RenderGraphDebug.h"
#include "RenderGraphPass.h"
#include "RenderGraphCompiler.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Types/StringBuilder.h"
#include "Engine/Platform/FileSystem.h"

// Forward declaration - will be defined in RenderGraph.h
class RenderGraph;

RenderGraphDebug::RenderGraphDebug()
    : _debugEnabled(false)
{
}

bool RenderGraphDebug::ExportToDot(RenderGraph* graph, RenderGraphCompiler* compiler, const String& outputPath)
{
    if (!graph || !compiler)
        return false;

    StringBuilder output;
    output.Append(TEXT("digraph RenderGraph {\n"));
    output.Append(TEXT("    rankdir=TB;\n"));
    output.Append(TEXT("    node [shape=box, style=filled];\n"));
    output.Append(TEXT("    \n"));

    // Get sorted passes
    const auto& sortedPasses = compiler->GetSortedPasses();

    // Write nodes for each pass
    for (int32 i = 0; i < sortedPasses.Count(); i++)
    {
        RenderGraphPass* pass = sortedPasses[i];
        WriteDotNode(output, pass, i);
    }

    output.Append(TEXT("    \n"));

    // Write edges for dependencies
    // TODO: This requires access to RenderGraph resource tracking
    // For now, we'll write a simplified version based on pass order
    for (int32 i = 1; i < sortedPasses.Count(); i++)
    {
        RenderGraphPass* currentPass = sortedPasses[i];
        RenderGraphPass* previousPass = sortedPasses[i - 1];

        // Check if current pass reads what previous pass wrote
        bool hasDependency = false;
        for (int32 j = 0; j < currentPass->_textureReads.Count(); j++)
        {
            for (int32 k = 0; k < previousPass->_textureWrites.Count(); k++)
            {
                if (currentPass->_textureReads[j] == previousPass->_textureWrites[k])
                {
                    WriteDotEdge(output, i - 1, i, TEXT("Texture"));
                    hasDependency = true;
                    break;
                }
            }
            if (hasDependency)
                break;
        }

        if (!hasDependency)
        {
            for (int32 j = 0; j < currentPass->_bufferReads.Count(); j++)
            {
                for (int32 k = 0; k < previousPass->_bufferWrites.Count(); k++)
                {
                    if (currentPass->_bufferReads[j] == previousPass->_bufferWrites[k])
                    {
                        WriteDotEdge(output, i - 1, i, TEXT("Buffer"));
                        hasDependency = true;
                        break;
                    }
                }
                if (hasDependency)
                    break;
            }
        }
    }

    output.Append(TEXT("}\n"));

    // Write to file
    const String outputStr = output.ToString();
    return FileSystem::WriteAllText(outputPath, outputStr);
}

void RenderGraphDebug::CollectPassStatistics(RenderGraph* graph, RenderGraphCompiler* compiler)
{
    if (!graph || !compiler)
        return;

    _passStats.Clear();

    const auto& sortedPasses = compiler->GetSortedPasses();
    _passStats.Resize(sortedPasses.Count());

    for (int32 i = 0; i < sortedPasses.Count(); i++)
    {
        RenderGraphPass* pass = sortedPasses[i];
        PassStats& stats = _passStats[i];

        stats.Name = pass->GetName();
        stats.Culled = pass->IsCulled();
        
        // TODO: Collect actual GPU/CPU timing data
        // This would require integration with profiler
        stats.GpuTimeMs = 0.0f;
        stats.CpuTimeMs = 0.0f;
        stats.DrawCalls = 0;
        stats.Dispatches = 0;
    }
}

void RenderGraphDebug::CollectResourceStatistics(RenderGraph* graph, RenderGraphCompiler* compiler)
{
    if (!graph || !compiler)
        return;

    _resourceStats = ResourceStats();

    // TODO: Get actual resource counts from graph
    // This requires access to RenderGraph internals
    _resourceStats.TotalTextures = 0;
    _resourceStats.TotalBuffers = 0;
    _resourceStats.TotalTextureMemory = 0;
    _resourceStats.TotalBufferMemory = 0;

    // Count aliased resources
    _resourceStats.AliasedTextures = 0;
    _resourceStats.AliasedBuffers = 0;
    _resourceStats.MemorySavedByAliasing = 0;

    // TODO: Iterate through resources and calculate memory usage
    // For each texture:
    //   _resourceStats.TotalTextureMemory += CalculateTextureMemory(i);
    //   if (compiler->GetTextureAliasing(i).AliasedResourceIndex >= 0)
    //   {
    //       _resourceStats.AliasedTextures++;
    //       _resourceStats.MemorySavedByAliasing += CalculateTextureMemory(i);
    //   }
}

void RenderGraphDebug::PrintDebugInfo(RenderGraph* graph, RenderGraphCompiler* compiler)
{
    if (!graph || !compiler || !_debugEnabled)
        return;

    LOG(Info, "=== RenderGraph Debug Info ===");

    const auto& sortedPasses = compiler->GetSortedPasses();
    LOG(Info, "Total Passes: {0}", sortedPasses.Count());

    int32 culledCount = 0;
    for (int32 i = 0; i < sortedPasses.Count(); i++)
    {
        if (sortedPasses[i]->IsCulled())
            culledCount++;
    }
    LOG(Info, "Culled Passes: {0}", culledCount);
    LOG(Info, "Active Passes: {0}", sortedPasses.Count() - culledCount);

    LOG(Info, "");
    LOG(Info, "Pass Execution Order:");
    for (int32 i = 0; i < sortedPasses.Count(); i++)
    {
        RenderGraphPass* pass = sortedPasses[i];
        String passType;
        if (pass->IsRaster())
            passType = TEXT("Raster");
        else if (pass->IsCompute())
            passType = TEXT("Compute");
        else if (pass->IsCopy())
            passType = TEXT("Copy");
        else
            passType = TEXT("Unknown");

        String status = pass->IsCulled() ? TEXT("[CULLED]") : TEXT("[ACTIVE]");
        LOG(Info, "  {0}. {1} ({2}) {3}", i, pass->GetName(), passType, status);
    }

    LOG(Info, "==============================");
}

void RenderGraphDebug::PrintPassStatistics()
{
    if (!_debugEnabled)
        return;

    LOG(Info, "=== RenderGraph Pass Statistics ===");

    float totalGpuTime = 0.0f;
    float totalCpuTime = 0.0f;
    int32 totalDrawCalls = 0;
    int32 totalDispatches = 0;

    for (int32 i = 0; i < _passStats.Count(); i++)
    {
        const PassStats& stats = _passStats[i];
        if (stats.Culled)
            continue;

        LOG(Info, "Pass: {0}", stats.Name);
        LOG(Info, "  GPU Time: {0} ms", stats.GpuTimeMs);
        LOG(Info, "  CPU Time: {0} ms", stats.CpuTimeMs);
        LOG(Info, "  Draw Calls: {0}", stats.DrawCalls);
        LOG(Info, "  Dispatches: {0}", stats.Dispatches);

        totalGpuTime += stats.GpuTimeMs;
        totalCpuTime += stats.CpuTimeMs;
        totalDrawCalls += stats.DrawCalls;
        totalDispatches += stats.Dispatches;
    }

    LOG(Info, "");
    LOG(Info, "Total GPU Time: {0} ms", totalGpuTime);
    LOG(Info, "Total CPU Time: {0} ms", totalCpuTime);
    LOG(Info, "Total Draw Calls: {0}", totalDrawCalls);
    LOG(Info, "Total Dispatches: {0}", totalDispatches);
    LOG(Info, "===================================");
}

void RenderGraphDebug::PrintResourceStatistics()
{
    if (!_debugEnabled)
        return;

    LOG(Info, "=== RenderGraph Resource Statistics ===");
    LOG(Info, "Total Textures: {0}", _resourceStats.TotalTextures);
    LOG(Info, "Total Buffers: {0}", _resourceStats.TotalBuffers);
    LOG(Info, "Total Texture Memory: {0} MB", _resourceStats.TotalTextureMemory / (1024.0f * 1024.0f));
    LOG(Info, "Total Buffer Memory: {0} MB", _resourceStats.TotalBufferMemory / (1024.0f * 1024.0f));
    LOG(Info, "Aliased Textures: {0}", _resourceStats.AliasedTextures);
    LOG(Info, "Aliased Buffers: {0}", _resourceStats.AliasedBuffers);
    LOG(Info, "Memory Saved by Aliasing: {0} MB", _resourceStats.MemorySavedByAliasing / (1024.0f * 1024.0f));
    LOG(Info, "========================================");
}

void RenderGraphDebug::Clear()
{
    _passStats.Clear();
    _resourceStats = ResourceStats();
}

void RenderGraphDebug::WriteDotNode(String& output, RenderGraphPass* pass, int32 passIndex)
{
    if (!pass)
        return;

    String color = GetPassColor(pass);
    String label = pass->GetName();
    
    if (pass->IsCulled())
    {
        output.AppendFormat(TEXT("    pass{0} [label=\"{1}\\n[CULLED]\", fillcolor=\"{2}\", style=\"filled,dashed\"];\n"), 
                          passIndex, label, color);
    }
    else
    {
        output.AppendFormat(TEXT("    pass{0} [label=\"{1}\", fillcolor=\"{2}\"];\n"), 
                          passIndex, label, color);
    }
}

void RenderGraphDebug::WriteDotEdge(String& output, int32 fromPass, int32 toPass, const String& resourceName)
{
    output.AppendFormat(TEXT("    pass{0} -> pass{1} [label=\"{2}\"];\n"), 
                      fromPass, toPass, resourceName);
}

String RenderGraphDebug::GetPassColor(RenderGraphPass* pass)
{
    if (!pass)
        return TEXT("white");

    if (pass->IsCulled())
        return TEXT("lightgray");
    else if (pass->IsRaster())
        return TEXT("lightblue");
    else if (pass->IsCompute())
        return TEXT("lightgreen");
    else if (pass->IsCopy())
        return TEXT("lightyellow");
    else
        return TEXT("white");
}

uint64 RenderGraphDebug::CalculateTextureMemory(int32 textureIndex)
{
    // TODO: Get actual texture description from graph and calculate memory
    // This requires access to RenderGraph internals
    return 0;
}

uint64 RenderGraphDebug::CalculateBufferMemory(int32 bufferIndex)
{
    // TODO: Get actual buffer description from graph and calculate memory
    // This requires access to RenderGraph internals
    return 0;
}
