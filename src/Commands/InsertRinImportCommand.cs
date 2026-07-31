using System;
using System.Collections.Generic;
using System.ComponentModel.Design;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using EnvDTE;
using Microsoft.VisualStudio.Shell;
using Task = System.Threading.Tasks.Task;

namespace RinLang.VSSDK.Commands
{
    /// <summary>
    /// Implements Tools &gt; "Rin: إدراج مكتبة (Insert Library)".
    /// This is the Visual Studio counterpart to the "المكتبات" screen in the
    /// Android app: it lists the 6 embedded @import libraries plus any local
    /// lib/*.og.rin files found near the open solution, and inserts the chosen
    /// @import statement at the caret of the active .rin document in one click —
    /// letting a developer extend the language itself (bring in stdlib or a
    /// project's own libraries) directly from the editor, on this platform too.
    /// </summary>
    internal sealed class InsertRinImportCommand
    {
        private static readonly (string FileName, string Summary)[] EmbeddedLibraries =
        {
            ("math.og.rin", "دوال رياضية إضافية: factorial, gcd, lcm, isPrime, clamp, lerp..."),
            ("strings.og.rin", "دوال نصوص إضافية: capitalize, reverseStr, padLeft, titleCase..."),
            ("data.og.rin", "دوال مصفوفات/قواميس: range, unique, chunk, zip, take, drop..."),
            ("validate.og.rin", "دوال تحقّق آمنة (لا ترمي أخطاء): isEmail, isNumeric, isInRange..."),
            ("functional.og.rin", "دوال ترتيبية عليا: mapArr, filterArr, reduceArr, composeApply..."),
            ("oglang.og.rin", "صناعة حزم .og.rin ولغات مصغّرة: pkgInfo, rule, langNew, runProgram..."),
        };

        private readonly AsyncPackage _package;
        private readonly DTE _dte;

        private InsertRinImportCommand(AsyncPackage package, OleMenuCommandService commandService, DTE dte)
        {
            _package = package ?? throw new ArgumentNullException(nameof(package));
            _dte = dte;

            var commandId = new CommandID(PackageGuids.RinCommandSet, PackageIds.InsertLibraryCommandId);
            var menuItem = new MenuCommand(Execute, commandId);
            commandService.AddCommand(menuItem);
        }

        public static async Task InitializeAsync(AsyncPackage package)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync(package.DisposalToken);

            var commandService = await package.GetServiceAsync(typeof(IMenuCommandService)) as OleMenuCommandService;
            var dte = await package.GetServiceAsync(typeof(SDTE)) as DTE;

            new InsertRinImportCommand(package, commandService, dte);
        }

        private void Execute(object sender, EventArgs e)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            var items = new List<RinLibraryItem>();
            foreach (var lib in EmbeddedLibraries)
            {
                items.Add(new RinLibraryItem($"lib/{lib.FileName}", lib.Summary, isBuiltIn: true));
            }

            string solutionDir = Path.GetDirectoryName(_dte?.Solution?.FullName ?? string.Empty);
            string repoRoot = RinBuildUtil.FindRepoRoot(solutionDir) ?? RinBuildUtil.FindRepoRoot(Environment.CurrentDirectory);
            string libDir = repoRoot != null ? Path.Combine(repoRoot, "lib") : null;

            if (!string.IsNullOrEmpty(libDir) && Directory.Exists(libDir))
            {
                var builtInNames = new HashSet<string>(EmbeddedLibraries.Select(l => l.FileName), StringComparer.OrdinalIgnoreCase);
                foreach (var file in Directory.EnumerateFiles(libDir, "*.og.rin").OrderBy(f => f))
                {
                    string name = Path.GetFileName(file);
                    if (builtInNames.Contains(name))
                    {
                        continue; // already listed as a built-in above
                    }
                    items.Add(new RinLibraryItem($"lib/{name}", "مكتبة محلية من مجلد lib/ الخاص بالمشروع", isBuiltIn: false));
                }
            }

            var picker = new RinLibraryPickerWindow(items);
            bool? result = picker.ShowDialog();

            if (result == true && picker.SelectedItem != null)
            {
                InsertImportAtCaret(picker.SelectedItem.ImportPath, picker.UseAlias);
            }
        }

        private void InsertImportAtCaret(string importPath, bool useAlias)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            var doc = _dte?.ActiveDocument;
            if (doc == null)
            {
                return;
            }

            var textSelection = doc.Selection as TextSelection;
            if (textSelection == null)
            {
                return;
            }

            string statement;
            if (useAlias)
            {
                string alias = Path.GetFileNameWithoutExtension(importPath.Split('/').Last());
                alias = alias.Replace(".og", string.Empty);
                statement = $"@import \"{importPath}\" as {alias};";
            }
            else
            {
                statement = $"@import \"{importPath}\";";
            }

            textSelection.Insert(statement + Environment.NewLine, (int)vsInsertFlags.vsInsertFlagsInsertAtStart);
        }
    }

    /// <summary>
    /// A single pickable library: either one of the 6 embedded stdlib packages,
    /// or a local lib/*.og.rin file discovered next to the solution.
    /// </summary>
    internal sealed class RinLibraryItem
    {
        public RinLibraryItem(string importPath, string summary, bool isBuiltIn)
        {
            ImportPath = importPath;
            Summary = summary;
            IsBuiltIn = isBuiltIn;
        }

        public string ImportPath { get; }
        public string Summary { get; }
        public bool IsBuiltIn { get; }
        public string DisplayName => Path.GetFileName(ImportPath);
        public string Badge => IsBuiltIn ? "مدمجة" : "محلية";
    }

    /// <summary>
    /// Small WPF picker styled to match the Rin brand (dark background, purple
    /// accent — same palette as the editor's classification colors and the
    /// Android app's Libraries screen), built entirely in code so no XAML/MarkupCompile
    /// wiring is needed in the csproj.
    /// </summary>
    internal sealed class RinLibraryPickerWindow : Window
    {
        private readonly ListBox _listBox;
        private readonly CheckBox _aliasCheckBox;

        public RinLibraryItem SelectedItem { get; private set; }
        public bool UseAlias => _aliasCheckBox.IsChecked == true;

        public RinLibraryPickerWindow(IReadOnlyList<RinLibraryItem> items)
        {
            Title = "Rin — إدراج مكتبة (@import)";
            Width = 460;
            Height = 420;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Background = new SolidColorBrush(Color.FromRgb(0x15, 0x16, 0x1A));
            FlowDirection = FlowDirection.RightToLeft;
            ResizeMode = ResizeMode.NoResize;

            var accent = new SolidColorBrush(Color.FromRgb(0x7C, 0x5C, 0xFF));
            var foreground = new SolidColorBrush(Color.FromRgb(0xD8, 0xDE, 0xE9));

            var root = new Grid { Margin = new Thickness(14) };
            root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            var header = new TextBlock
            {
                Text = "اختر مكتبة لإدراج @import عند مؤشر الكتابة:",
                Foreground = foreground,
                FontSize = 13,
                Margin = new Thickness(0, 0, 0, 8),
                TextWrapping = TextWrapping.Wrap,
            };
            Grid.SetRow(header, 0);
            root.Children.Add(header);

            _listBox = new ListBox
            {
                Background = new SolidColorBrush(Color.FromRgb(0x1E, 0x20, 0x26)),
                Foreground = foreground,
                BorderThickness = new Thickness(1),
                BorderBrush = new SolidColorBrush(Color.FromRgb(0x30, 0x33, 0x3B)),
            };
            foreach (var item in items)
            {
                _listBox.Items.Add(new ListBoxItem
                {
                    Content = $"{item.DisplayName}   [{item.Badge}]\n{item.Summary}",
                    Tag = item,
                    Padding = new Thickness(6),
                });
            }
            if (_listBox.Items.Count > 0)
            {
                _listBox.SelectedIndex = 0;
            }
            _listBox.MouseDoubleClick += (s, e) => TryAccept();
            Grid.SetRow(_listBox, 1);
            root.Children.Add(_listBox);

            _aliasCheckBox = new CheckBox
            {
                Content = "استيراد باسم مستعار (as ...) بدل الدمج المباشر",
                Foreground = foreground,
                Margin = new Thickness(0, 10, 0, 6),
            };
            Grid.SetRow(_aliasCheckBox, 2);
            root.Children.Add(_aliasCheckBox);

            var buttonPanel = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Left };
            var insertButton = new Button
            {
                Content = "إدراج",
                Width = 90,
                Height = 28,
                Margin = new Thickness(0, 0, 8, 0),
                Background = accent,
                Foreground = Brushes.White,
                BorderThickness = new Thickness(0),
            };
            insertButton.Click += (s, e) => TryAccept();

            var cancelButton = new Button
            {
                Content = "إلغاء",
                Width = 90,
                Height = 28,
                Background = new SolidColorBrush(Color.FromRgb(0x2A, 0x2D, 0x34)),
                Foreground = foreground,
                BorderThickness = new Thickness(0),
            };
            cancelButton.Click += (s, e) => { DialogResult = false; Close(); };

            buttonPanel.Children.Add(insertButton);
            buttonPanel.Children.Add(cancelButton);
            Grid.SetRow(buttonPanel, 3);
            root.Children.Add(buttonPanel);

            Content = root;
        }

        private void TryAccept()
        {
            if (_listBox.SelectedItem is ListBoxItem selected && selected.Tag is RinLibraryItem item)
            {
                SelectedItem = item;
                DialogResult = true;
                Close();
            }
        }
    }
}
