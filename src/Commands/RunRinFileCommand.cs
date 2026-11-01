using System;
using System.ComponentModel.Design;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using EnvDTE;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace RinLang.VSSDK.Commands
{
    /// <summary>
    /// Implements Tools &gt; "Rin: تشغيل الملف الحالي (Run Rin File)".
    /// Locates the real Rin engine binary — built from tools/rin_run.cpp against
    /// the same rin_lexer/rin_parser/rin_interpreter sources the Android app embeds
    /// via JNI (see RinBuildUtil.EngineSources) — next to the solution, or on PATH,
    /// runs it against the active .rin document, and streams stdout/stderr to a
    /// dedicated "Rin" Output window pane. This always executes the genuine C++17
    /// engine; nothing here is simulated or reimplemented in C#.
    /// </summary>
    internal sealed class RunRinFileCommand
    {
        private readonly AsyncPackage _package;
        private readonly DTE _dte;
        private readonly IVsOutputWindowPaneAccessor _output;

        private RunRinFileCommand(AsyncPackage package, OleMenuCommandService commandService, DTE dte, IVsOutputWindowPaneAccessor output)
        {
            _package = package ?? throw new ArgumentNullException(nameof(package));
            _dte = dte;
            _output = output;

            var commandId = new CommandID(PackageGuids.RinCommandSet, PackageIds.RunRinFileCommandId);
            var menuItem = new MenuCommand(Execute, commandId);
            commandService.AddCommand(menuItem);
        }

        public static async Task InitializeAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync(package.DisposalToken);

            var commandService = await package.GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
            var dte = await package.GetServiceAsync(typeof(SDTE)) as DTE;
            var output = new IVsOutputWindowPaneAccessor(package);

            new RunRinFileCommand(package, commandService, dte, output);
        }

        private void Execute(object sender, EventArgs e)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            string filePath = _dte?.ActiveDocument?.FullName;
            if (string.IsNullOrEmpty(filePath) || !filePath.EndsWith(".rin", StringComparison.OrdinalIgnoreCase))
            {
                _output.WriteLine("[Rin] لا يوجد ملف .rin نشط في المحرر حالياً. افتح ملف .rin ثم أعد المحاولة.");
                return;
            }

            _dte.ActiveDocument.Save();

            string enginePath = LocateRinEngine(filePath);
            if (enginePath == null)
            {
                _output.WriteLine("[Rin] تعذّر العثور على محرّك Rin (rin_run). استخدم \"Rin: بناء المحرّك (Build Rin Engine)\" من قائمة Tools لبنائه تلقائياً من tools/rin_run.cpp، أو ضع ملفاً تنفيذياً باسم rin_run في PATH أو بجانب ملف الحل.");
                return;
            }

            RunProcessAsync(enginePath, filePath).FileAndForget("RinLang/RunFile");
        }

        private async Task RunProcessAsync(string rinCliPath, string rinFilePath)
        {
            _output.WriteLine($"[Rin] تشغيل: \"{rinCliPath}\" \"{rinFilePath}\"");

            var psi = new System.Diagnostics.ProcessStartInfo
            {
                FileName = rinCliPath,
                Arguments = $"\"{rinFilePath}\"",
                WorkingDirectory = Path.GetDirectoryName(rinFilePath) ?? Environment.CurrentDirectory,
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
                        _output.WriteLine("[stderr] " + stderr);
                    }
                    _output.WriteLine($"[Rin] انتهى التنفيذ برمز الخروج {process.ExitCode}.");
                }
            }
            catch (Exception ex)
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                _output.WriteLine("[Rin] خطأ أثناء التشغيل: " + ex.Message);
            }
        }

        /// <summary>
        /// Looks for the compiled engine binary next to the solution, in a
        /// conventional build\ / out\ / bin\ subfolder, or on PATH — in that order.
        /// "rin_run" is the real name (tools/rin_run.cpp); "rin_cli" is kept as a
        /// legacy alias for anyone who already built under the old name.
        /// </summary>
        private string LocateRinEngine(string rinFilePath)
        {
            bool isWindows = Environment.OSVersion.Platform == PlatformID.Win32NT;
            string[] candidateNames = isWindows
                ? new[] { "rin_run.exe", "rin_cli.exe" }
                : new[] { "rin_run", "rin_cli" };

            string solutionDir = Path.GetDirectoryName(_dte?.Solution?.FullName ?? string.Empty);
            string[] candidateDirs =
            {
                solutionDir,
                solutionDir != null ? Path.Combine(solutionDir, "build") : null,
                solutionDir != null ? Path.Combine(solutionDir, "out") : null,
                solutionDir != null ? Path.Combine(solutionDir, "bin") : null,
                Path.GetDirectoryName(rinFilePath),
            };

            foreach (var dir in candidateDirs)
            {
                if (string.IsNullOrEmpty(dir))
                {
                    continue;
                }
                foreach (var name in candidateNames)
                {
                    string candidate = Path.Combine(dir, name);
                    if (File.Exists(candidate))
                    {
                        return candidate;
                    }
                }
            }

            foreach (var pathDir in (Environment.GetEnvironmentVariable("PATH") ?? string.Empty).Split(Path.PathSeparator))
            {
                foreach (var name in candidateNames)
                {
                    string candidate = Path.Combine(pathDir, name);
                    if (File.Exists(candidate))
                    {
                        return candidate;
                    }
                }
            }

            return null;
        }
    }

    /// <summary>
    /// Small helper wrapping the "Rin" custom Output window pane so command code
    /// doesn't need to repeat the pane-creation boilerplate.
    /// </summary>
    internal sealed class IVsOutputWindowPaneAccessor
    {
        private static readonly Guid RinOutputPaneGuid = new Guid("6e1f2a4c-9b3d-4a5e-8f6a-2c1d3e4f5a6b");
        private readonly AsyncPackage _package;
        private IVsOutputWindowPane _pane;

        public IVsOutputWindowPaneAccessor(AsyncPackage package)
        {
            _package = package;
        }

        public void WriteLine(string text)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            if (_pane == null)
            {
                var outputWindow = _package.GetServiceAsync(typeof(SVsOutputWindow)).Result as IVsOutputWindow;
                outputWindow?.CreatePane(RinOutputPaneGuid, "Rin", 1, 1);
                outputWindow?.GetPane(RinOutputPaneGuid, out _pane);
            }

            _pane?.OutputStringThreadSafe(text + Environment.NewLine);
            _pane?.Activate();
        }
    }
}
