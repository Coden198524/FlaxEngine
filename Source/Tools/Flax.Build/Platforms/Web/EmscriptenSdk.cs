// Copyright (c) Wojciech Figat. All rights reserved.

using System;
using System.IO;
using System.Linq;

namespace Flax.Build.Platforms
{
    /// <summary>
    /// The Emscripten SDK (https://emscripten.org/).
    /// </summary>
    /// <seealso cref="Sdk" />
    public sealed class EmscriptenSdk : Sdk
    {
        /// <summary>
        /// The singleton instance.
        /// </summary>
        public static readonly EmscriptenSdk Instance = new EmscriptenSdk();

        /// <inheritdoc />
        public override TargetPlatform[] Platforms => new[]
        {
            TargetPlatform.Windows,
            TargetPlatform.Linux,
            TargetPlatform.Mac,
        };

        /// <summary>
        /// Full path to the current SDK folder with binaries, tools and sources (eg. '%EMSDK%\upstream').
        /// </summary>
        public string EmscriptenPath;

        /// <summary>
        /// Full path to the CMake toolchain file (eg. '%EMSDK%\upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake').
        /// </summary>
        public string CMakeToolchainPath;

        /// <summary>
        /// Initializes a new instance of the <see cref="AndroidSdk"/> class.
        /// </summary>
        public EmscriptenSdk()
        {
            if (!Platforms.Contains(Platform.BuildTargetPlatform))
                return;

            // Find Emscripten SDK folder path
            var sdkPath = Environment.GetEnvironmentVariable("EMSDK");
            if (string.IsNullOrEmpty(sdkPath))
                sdkPath = FindSdkPath();
            if (string.IsNullOrEmpty(sdkPath))
            {
                Log.Warning("Missing Emscripten SDK. Cannot build for Web platform.");
            }
            else if (!Directory.Exists(sdkPath))
            {
                Log.Warning(string.Format("Specified Emscripten SDK folder in EMSDK env variable doesn't exist ({0})", sdkPath));
            }
            else
            {
                SetupEnvironment(sdkPath);
                RootPath = sdkPath;
                EmscriptenPath = Path.Combine(sdkPath, "upstream");
                CMakeToolchainPath = Path.Combine(EmscriptenPath, "emscripten/cmake/Modules/Platform/Emscripten.cmake");
                var versionPath = Path.Combine(EmscriptenPath, "emscripten", "emscripten-version.txt");
                if (File.Exists(versionPath))
                {
                    try
                    {
                        // Read version
                        var versionStr = File.ReadAllLines(versionPath)[0];
                        versionStr = versionStr.Trim();
                        if (versionStr.StartsWith('\"') && versionStr.EndsWith('\"'))
                            versionStr = versionStr.Substring(1, versionStr.Length - 2);
                        Version = new Version(versionStr);
                        
                        var minVersion = new Version(4, 0);
                        if (Version < minVersion)
                        {
                            Log.Error(string.Format("Unsupported Emscripten SDK version {0}. Minimum supported is {1}.", Version, minVersion));
                            return;
                        }
                        Log.Info(string.Format("Found Emscripten SDK {0} at {1}", Version, RootPath));
                        IsValid = true;
                    }
                    catch (Exception ex)
                    {
                        Log.Error($"Failed to read Emscripten SDK version from file '{versionPath}'");
                        Log.Exception(ex);
                    }
                }
                else
                    Log.Warning($"Missing file {versionPath}");
            }
        }

        private static string FindSdkPath()
        {
            string[] searchDirs =
            {
                Path.Combine(Globals.EngineRoot, "emsdk"),
                Path.Combine(Globals.EngineRoot, "..", "emsdk"),
                Path.Combine(Globals.Root, "emsdk"),
                Path.Combine(Globals.Root, "..", "emsdk"),
            };

            foreach (var searchDir in searchDirs)
            {
                var path = Utilities.RemovePathRelativeParts(searchDir);
                var versionPath = Path.Combine(path, "upstream", "emscripten", "emscripten-version.txt");
                if (File.Exists(versionPath))
                    return path;
            }

            return null;
        }

        private static void SetupEnvironment(string sdkPath)
        {
            Environment.SetEnvironmentVariable("EMSDK", sdkPath);
            var configPath = Path.Combine(sdkPath, ".emscripten");
            if (File.Exists(configPath))
                Environment.SetEnvironmentVariable("EM_CONFIG", configPath);

            var nodePath = FindToolPath(Path.Combine(sdkPath, "node"), "node.exe", "bin");
            if (nodePath != null)
                Environment.SetEnvironmentVariable("EMSDK_NODE", nodePath);

            var pythonPath = FindToolPath(Path.Combine(sdkPath, "python"), "python.exe");
            if (pythonPath != null)
                Environment.SetEnvironmentVariable("EMSDK_PYTHON", pythonPath);

            string[] paths =
            {
                sdkPath,
                Path.Combine(sdkPath, "upstream", "emscripten"),
                Path.Combine(sdkPath, "upstream", "bin"),
                nodePath != null ? Path.GetDirectoryName(nodePath) : null,
                pythonPath != null ? Path.GetDirectoryName(pythonPath) : null,
            };
            PrependPath(paths);
        }

        private static string FindToolPath(string root, string fileName, string subDir = null)
        {
            if (!Directory.Exists(root))
                return null;

            var dirs = Directory.GetDirectories(root).OrderBy(x => x).ToArray();
            for (int i = dirs.Length - 1; i >= 0; i--)
            {
                var path = subDir != null ? Path.Combine(dirs[i], subDir, fileName) : Path.Combine(dirs[i], fileName);
                if (File.Exists(path))
                    return path;
            }

            return null;
        }

        private static void PrependPath(string[] paths)
        {
            var currentPath = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
            var parts = currentPath.Split(Path.PathSeparator).Where(x => !string.IsNullOrWhiteSpace(x)).ToList();
            for (int i = paths.Length - 1; i >= 0; i--)
            {
                var path = paths[i];
                if (string.IsNullOrEmpty(path))
                    continue;
                parts.RemoveAll(x => string.Equals(x.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar), StringComparison.OrdinalIgnoreCase));
                parts.Insert(0, path);
            }
            Environment.SetEnvironmentVariable("PATH", string.Join(Path.PathSeparator.ToString(), parts));
        }
    }
}
