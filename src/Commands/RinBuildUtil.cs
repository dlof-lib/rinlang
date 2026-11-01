using System;
using System.IO;
using System.Linq;

namespace RinLang.VSSDK.Commands
{
    /// <summary>
    /// Shared helpers for locating the rinlang repo (relative to the open solution)
    /// and compiling the real C++17 engine (app/src/main/cpp/rin_lexer|rin_parser|
    /// rin_interpreter + tools/rin_run.cpp) into a standalone "rin_run" binary,
    /// exactly the way README.md's "جرّبها بنفسك" g++ one-liner does it. Used by
    /// both BuildRinEngineCommand (to produce the binary) and RunRinFileCommand
    /// (to run it) so the two commands agree on paths and naming.
    /// </summary>
    internal static class RinBuildUtil
    {
        /// <summary>
        /// Source files that make up the standalone engine, relative to the repo root.
        /// Kept in one place so Run/Build never drift apart on what "the engine" means.
        /// </summary>
        public static readonly string[] EngineSources =
        {
            Path.Combine("app", "src", "main", "cpp", "rin_lexer.cpp"),
            Path.Combine("app", "src", "main", "cpp", "rin_parser.cpp"),
            Path.Combine("app", "src", "main", "cpp", "rin_interpreter.cpp"),
            Path.Combine("tools", "rin_run.cpp"),
        };

        public static readonly string EngineIncludeDir = Path.Combine("app", "src", "main", "cpp");

        /// <summary>
        /// Starting from the solution directory, walks up looking for the rinlang
        /// repo root — identified by having both tools/rin_run.cpp and the cpp
        /// engine sources — so this works whether the .sln sits at the repo root
        /// or one level down (e.g. a "src" or "vssdk" subfolder).
        /// </summary>
        public static string FindRepoRoot(string startDir)
        {
            var dir = string.IsNullOrEmpty(startDir) ? null : new DirectoryInfo(startDir);

            while (dir != null)
            {
                bool looksLikeRepoRoot = File.Exists(Path.Combine(dir.FullName, "tools", "rin_run.cpp"))
                    && EngineSources.All(rel => File.Exists(Path.Combine(dir.FullName, rel)));

                if (looksLikeRepoRoot)
                {
                    return dir.FullName;
                }

                dir = dir.Parent;
            }

            return null;
        }

        public static string EngineBinaryName =>
            Environment.OSVersion.Platform == PlatformID.Win32NT ? "rin_run.exe" : "rin_run";
    }
}
