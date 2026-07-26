using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Shell;
using RinLang.VSSDK.Commands;
using Task = System.Threading.Tasks.Task;

namespace RinLang.VSSDK
{
    /// <summary>
    /// Entry point of the extension. The classifier/content-type MEF parts load on
    /// their own the moment a .rin file is opened, regardless of this package —
    /// this package exists specifically to register the "Run Rin File" command,
    /// so it auto-loads whenever a solution is present.
    /// </summary>
    [PackageRegistration(UseManagedResourcesOnly = false, AllowsBackgroundLoading = true)]
    [InstalledProductRegistration("Rin Language Support (VSSDK)", "Syntax classification, snippets, and a Run-File command for the Rin language.", "1.0.0")]
    [ProvideMenuResource("Menus.ctmenu", 1)]
    [Guid(PackageGuids.PackageGuidString)]
    [ProvideAutoLoad(Microsoft.VisualStudio.Shell.Interop.UIContextGuids80.SolutionExists, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideAutoLoad(Microsoft.VisualStudio.Shell.Interop.UIContextGuids80.NoSolution, PackageAutoLoadFlags.BackgroundLoad)]
    // Best-effort snippet registration: makes the .snippet files under Snippets\Rin
    // available to VS's generic "Insert Snippet" infrastructure under the "Rin"
    // language name. Full Ctrl+K,X integration in the editor additionally wants an
    // IVsLanguageInfo-backed language service tied to the same content type — see
    // README.md "Known limitations" for that follow-up step.
    [ProvideLanguageCodeExpansion(
        "{" + PackageGuids.RinLanguageGuidString + "}",
        "Rin",
        0,
        "Rin",
        @"%InstallRoot%\RinLangVSSDK\Snippets\%LCID%\SnippetsIndex.xml",
        SearchPaths = @"%InstallRoot%\RinLangVSSDK\Snippets\Rin\")]
    public sealed class RinLangPackage : AsyncPackage
    {
        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);
            await RunRinFileCommand.InitializeAsync(this);
        }
    }
}
