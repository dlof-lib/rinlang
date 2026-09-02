using System.ComponentModel.Composition;
using System.Windows.Media;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Utilities;

namespace RinLang.VSSDK.Classification
{
    // Each definition below is a 1:1 port of a scope/color pair from
    // extension/themes/rin-dark-color-theme.json in the original VS Code
    // extension, so the editor looks the same in both products.

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Comment)]
    [Name(RinClassificationTypes.Comment)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinCommentFormat : ClassificationFormatDefinition
    {
        public RinCommentFormat()
        {
            DisplayName = "Rin - Comment";
            ForegroundColor = Color.FromRgb(0x5C, 0x63, 0x70);
            IsItalic = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.StringLiteral)]
    [Name(RinClassificationTypes.StringLiteral)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinStringFormat : ClassificationFormatDefinition
    {
        public RinStringFormat()
        {
            DisplayName = "Rin - String";
            ForegroundColor = Color.FromRgb(0x4D, 0xD6, 0xB0);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Number)]
    [Name(RinClassificationTypes.Number)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinNumberFormat : ClassificationFormatDefinition
    {
        public RinNumberFormat()
        {
            DisplayName = "Rin - Number";
            ForegroundColor = Color.FromRgb(0xF2, 0xA6, 0x5A);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.BooleanNil)]
    [Name(RinClassificationTypes.BooleanNil)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinBooleanNilFormat : ClassificationFormatDefinition
    {
        public RinBooleanNilFormat()
        {
            DisplayName = "Rin - Boolean / nil";
            ForegroundColor = Color.FromRgb(0xFF, 0x8A, 0x6B);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.MathConstant)]
    [Name(RinClassificationTypes.MathConstant)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinMathConstantFormat : ClassificationFormatDefinition
    {
        public RinMathConstantFormat()
        {
            DisplayName = "Rin - Math constant (PI, E)";
            ForegroundColor = Color.FromRgb(0xFF, 0xCB, 0x6B);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Keyword)]
    [Name(RinClassificationTypes.Keyword)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinKeywordFormat : ClassificationFormatDefinition
    {
        public RinKeywordFormat()
        {
            DisplayName = "Rin - Keyword (if/else/while/return/and/or/print)";
            ForegroundColor = Color.FromRgb(0xC7, 0x92, 0xEA);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Storage)]
    [Name(RinClassificationTypes.Storage)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinStorageFormat : ClassificationFormatDefinition
    {
        public RinStorageFormat()
        {
            DisplayName = "Rin - Storage (let/fun/text)";
            ForegroundColor = Color.FromRgb(0x7C, 0x5C, 0xFF);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.ContainerKeyword)]
    [Name(RinClassificationTypes.ContainerKeyword)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinContainerKeywordFormat : ClassificationFormatDefinition
    {
        public RinContainerKeywordFormat()
        {
            DisplayName = "Rin - Container annotation (@container, @Containers.Group, @Volume, Section)";
            ForegroundColor = Color.FromRgb(0x57, 0xD6, 0xB8);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.RelationKeyword)]
    [Name(RinClassificationTypes.RelationKeyword)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinRelationKeywordFormat : ClassificationFormatDefinition
    {
        public RinRelationKeywordFormat()
        {
            DisplayName = "Rin - Document/route/table keyword";
            ForegroundColor = Color.FromRgb(0x66, 0xB8, 0xFF);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.EndTag)]
    [Name(RinClassificationTypes.EndTag)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinEndTagFormat : ClassificationFormatDefinition
    {
        public RinEndTagFormat()
        {
            DisplayName = "Rin - .end/ closing tag";
            ForegroundColor = Color.FromRgb(0xFF, 0x5C, 0x7A);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.ImportKeyword)]
    [Name(RinClassificationTypes.ImportKeyword)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinImportKeywordFormat : ClassificationFormatDefinition
    {
        public RinImportKeywordFormat()
        {
            DisplayName = "Rin - import / as";
            ForegroundColor = Color.FromRgb(0xFF, 0xCB, 0x6B);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Annotation)]
    [Name(RinClassificationTypes.Annotation)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinAnnotationFormat : ClassificationFormatDefinition
    {
        public RinAnnotationFormat()
        {
            DisplayName = "Rin - @ annotation marker";
            ForegroundColor = Color.FromRgb(0x7C, 0x5C, 0xFF);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.FunctionCall)]
    [Name(RinClassificationTypes.FunctionCall)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinFunctionCallFormat : ClassificationFormatDefinition
    {
        public RinFunctionCallFormat()
        {
            DisplayName = "Rin - Function call";
            ForegroundColor = Color.FromRgb(0x82, 0xAA, 0xFF);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.BuiltinFunction)]
    [Name(RinClassificationTypes.BuiltinFunction)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinBuiltinFunctionFormat : ClassificationFormatDefinition
    {
        public RinBuiltinFunctionFormat()
        {
            DisplayName = "Rin - Built-in function (stdlib)";
            ForegroundColor = Color.FromRgb(0x89, 0xDD, 0xFF);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.ExplicitMathFunction)]
    [Name(RinClassificationTypes.ExplicitMathFunction)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinExplicitMathFunctionFormat : ClassificationFormatDefinition
    {
        public RinExplicitMathFunctionFormat()
        {
            DisplayName = "Rin - Explicit math function (Addition/Subtraction/...)";
            ForegroundColor = Color.FromRgb(0xF2, 0xA6, 0x5A);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Operator)]
    [Name(RinClassificationTypes.Operator)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinOperatorFormat : ClassificationFormatDefinition
    {
        public RinOperatorFormat()
        {
            DisplayName = "Rin - Operator";
            ForegroundColor = Color.FromRgb(0xD8, 0xDE, 0xE9);
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Punctuation)]
    [Name(RinClassificationTypes.Punctuation)]
    [UserVisible(false)]
    [Order(After = Priority.High)]
    internal sealed class RinPunctuationFormat : ClassificationFormatDefinition
    {
        public RinPunctuationFormat()
        {
            DisplayName = "Rin - Punctuation";
            ForegroundColor = Color.FromRgb(0xAB, 0xB2, 0xBF);
        }
    }

    // Make Unit (@make.(name) ... ) policy surface -- see docs/MAKE_UNIT.md.

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.MakeDirective)]
    [Name(RinClassificationTypes.MakeDirective)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinMakeDirectiveFormat : ClassificationFormatDefinition
    {
        public RinMakeDirectiveFormat()
        {
            DisplayName = "Rin - Make Unit directive (kind/use/need/allow/deny/strict/input/output/public/private/version/description)";
            ForegroundColor = Color.FromRgb(0xC5, 0x86, 0xC0);
            IsBold = true;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RinClassificationTypes.Capability)]
    [Name(RinClassificationTypes.Capability)]
    [UserVisible(true)]
    [Order(After = Priority.High)]
    internal sealed class RinCapabilityFormat : ClassificationFormatDefinition
    {
        public RinCapabilityFormat()
        {
            DisplayName = "Rin - Make Unit capability name (after use/need/allow/deny)";
            ForegroundColor = Color.FromRgb(0x57, 0xD6, 0xB8);
        }
    }
}
