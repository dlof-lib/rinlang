using System;
using System.ComponentModel.Design;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using EnvDTE;
using Microsoft.VisualStudio.Shell;
using Task = System.Threading.Tasks.Task;

namespace RinLang.VSSDK.Commands
{
    /// <summary>
    /// Implements Tools &gt; "Rin: بناء المحرّك (Build Rin Engine)".
    /// Shells out to g++ against the same three engine files the Android app
    /// embeds via JNI (rin_lexer.cpp / rin_parser.cpp / rin_interpreter.cpp) plus
    /// tools/rin_run.cpp, producing build\rin_run(.exe) next to the solution —
    /// the exact binary "Run Rin File" then looks for. This is the one-click
    /// equivalent of the g++ line in README.md's Quick Start.
    /// </summary>
    internal sealed class BuildRinEngineCommand
    {
        private readonly AsyncPackage _package;
        private readonly DTE _dte;
        private readonly IVsOutputWindowPaneAccessor _output;

        private BuildRinEngineCommand(AsyncPackage package, OleMenuCommandService commandService, DTE dte, IVsOutputWindowPaneAccessor output)
        {
            _package = package ?? throw new ArgumentNullException(nameof(package));
            _dte = dte;
            _output = output;

            var commandId = new CommandID(PackageGuids.RinCommandSet, PackageIds.BuildRinEngineCommandId);
            var menuItem = new MenuCommand(Execute, commandId);
            commandService.AddCommand(menuItem);
        }

        public static async Task InitializeAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync(package.DisposalToken);

            var commandService = await package.GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
            var dte = await package.GetServiceAsync(typeof(SDTE)) as DTE;
            var output = new IVsOutputWindowPaneAccessor(package);

            new BuildRinEngineCommand(package, commandService, dte, output);
        }

        private void Execute(object sender, EventArgs e)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            string solutionDir = Path.GetDirectoryName(_dte?.Solution?.FullName ?? string.Empty);
            string repoRoot = RinBuildUtil.FindRepoRoot(solutionDir) ?? RinBuildUtil.FindRepoRoot(Environment.CurrentDirectory);

            if (repoRoot == null)
            {
                _output.WriteLine("[Rin] تعذّر تحديد جذر مستودع rinlang (لم أجد tools/rin_run.cpp بجانب الحل). افتح الحل من داخل نسخة كاملة من مستودع rinlang وأعد المحاولة.");
                return;
            }

            BuildAsync(repoRoot).FileAndForget("RinLang/BuildEngine");
        }

        private async Task BuildAsync(string repoRoot)
        {
            string buildDir = Path.Combine(repoRoot, "build");
            Directory.CreateDirectory(buildDir);
            string outputPath = Path.Combine(buildDir, RinBuildUtil.EngineBinaryName);
            string includeDir = Path.Combine(repoRoot, RinBuildUtil.EngineIncludeDir);

            var argsBuilder = new StringBuilder();
            argsBuilder.Append("-std=c++17 -O2 -o \"").Append(outputPath).Append("\" ");
            foreach (var relSource in RinBuildUtil.EngineSources)
            {
                argsBuilder.Append('"').Append(Path.Combine(repoRoot, relSource)).Append("\" ");
            }
            argsBuilder.Append("-I \"").Append(includeDir).Append('"');

            _output.WriteLine("[Rin] بناء المحرّك: g++ " + argsBuilder);

            var psi = new System.Diagnostics.ProcessStartInfo
            {
                FileName = "g++",
                Arguments = argsBuilder.ToString(),
                WorkingDirectory = repoRoot,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
            };

            try
            {
                using (var process = System.Diagnostics.Process.Start(psi))
                {
                    string stdout = await process.StandardOutput.ReadToEndAsync();
                    string stderr = await process.StandardError.ReadToEndAsync();
                    process.WaitForExit();

                    await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

                    if (!string.IsNullOrEmpty(stdout))
                    {
                        _output.WriteLine(stdout);
                    }
                    if (!string.IsNullOrEmpty(stderr))
                    {
                        _output.WriteLine("[g++] " + stderr);
                    }

                    if (process.ExitCode == 0 && File.Exists(outputPath))
                    {
                        _output.WriteLine($"[Rin] تم بناء المحرّك بنجاح: {outputPath}");
                        _output.WriteLine("[Rin] الآن \"Rin: تشغيل الملف الحالي\" سيجد هذا الملف تلقائياً.");
                    }
                    else
                    {
                        _output.WriteLine($"[Rin] فشل البناء (رمز الخروج {process.ExitCode}). تأكد من تثبيت g++ ووجوده في PATH.");
                    }
                }
            }
            catch (Exception ex)
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                _output.WriteLine("[Rin] تعذّر تشغيل g++: " + ex.Message + " — تأكد من تثبيت MinGW/MSYS2 أو WSL وإتاحة g++ في PATH.");
            }
        }
    }
}
