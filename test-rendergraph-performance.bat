@echo off
REM Performance test script for RenderGraph optimizations
REM Run this after successful compilation

echo ========================================
echo RenderGraph Performance Test
echo ========================================
echo.

echo [1/4] Checking build output...
if not exist "Binaries\Editor\Win64\Development\FlaxEditor.exe" (
    echo ERROR: FlaxEditor.exe not found in Development build
    echo Please ensure compilation completed successfully
    exit /b 1
)
echo ✓ FlaxEditor.exe found

echo.
echo [2/4] Test Instructions:
echo ----------------------------------------
echo 1. Launch FlaxEditor.exe
echo 2. Open the Bistro project
echo 3. Load the default scene (Content/Bistro Scene.scene)
echo 4. Press F11 to open the Profiler
echo 5. Check the following metrics:
echo.
echo    BEFORE optimization (baseline):
echo    - CPU Frame Time: ~25-30ms (40 FPS)
echo    - RenderGraph Execute: ~3-5ms
echo    - GPU Frame Time: ~20-25ms
echo.
echo    AFTER optimization (expected):
echo    - CPU Frame Time: ~18-23ms (45-55 FPS)
echo    - RenderGraph Execute: ~1-3ms (2-5ms saved)
echo    - GPU Frame Time: ~19-24ms (0.5-1ms saved)
echo.
echo    Expected improvement: +20-35%% frame rate
echo.
echo [3/4] Additional verification:
echo ----------------------------------------
echo - Use RenderDoc to capture a frame
echo - Check D3D11 API call count reduction
echo - Verify ResetSR/ResetUA calls: should be ~6 per frame (was ~300)
echo - Verify GPU event markers disabled in Release build
echo.
echo [4/4] Launching editor...
echo ----------------------------------------

cd Binaries\Editor\Win64\Release
start FlaxEditor.exe

echo.
echo ✓ Editor launched
echo.
echo Please perform the tests above and compare with baseline metrics
echo documented in RENDERGRAPH_OPTIMIZATIONS_APPLIED.md
echo.
echo Press any key to exit...
pause >nul
