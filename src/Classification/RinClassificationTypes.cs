using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;

namespace RinLang.VSSDK.Classification
{
    /// <summary>
    /// Declares every classification "bucket" the Rin classifier can hand out.
    /// These mirror the TextMate scopes used in rin.tmLanguage.json so the same
    /// mental model (comment / string / keyword / storage / container-keyword / ...)
    /// carries over from the VS Code extension to Visual Studio.
    /// </summary>
    internal static class RinClassificationTypes
    {
        public const string Comment = "Rin.Comment";
        public const string StringLiteral = "Rin.String";
        public const string Number = "Rin.Number";
        public const string BooleanNil = "Rin.BooleanNil";
        public const string MathConstant = "Rin.MathConstant";
        public const string Keyword = "Rin.Keyword";
        public const string Storage = "Rin.Storage";
        public const string ContainerKeyword = "Rin.ContainerKeyword";
        public const string RelationKeyword = "Rin.RelationKeyword";
        public const string EndTag = "Rin.EndTag";
        public const string ImportKeyword = "Rin.ImportKeyword";
        public const string Annotation = "Rin.Annotation";
        public const string FunctionCall = "Rin.FunctionCall";
        public const string BuiltinFunction = "Rin.BuiltinFunction";
        public const string ExplicitMathFunction = "Rin.ExplicitMathFunction";
        public const string Operator = "Rin.Operator";
        public const string Punctuation = "Rin.Punctuation";

        [Export] [Name(Comment)] internal static ClassificationTypeDefinition CommentType;
        [Export] [Name(StringLiteral)] internal static ClassificationTypeDefinition StringType;
        [Export] [Name(Number)] internal static ClassificationTypeDefinition NumberType;
        [Export] [Name(BooleanNil)] internal static ClassificationTypeDefinition BooleanNilType;
        [Export] [Name(MathConstant)] internal static ClassificationTypeDefinition MathConstantType;
        [Export] [Name(Keyword)] internal static ClassificationTypeDefinition KeywordType;
        [Export] [Name(Storage)] internal static ClassificationTypeDefinition StorageType;
        [Export] [Name(ContainerKeyword)] internal static ClassificationTypeDefinition ContainerKeywordType;
        [Export] [Name(RelationKeyword)] internal static ClassificationTypeDefinition RelationKeywordType;
        [Export] [Name(EndTag)] internal static ClassificationTypeDefinition EndTagType;
        [Export] [Name(ImportKeyword)] internal static ClassificationTypeDefinition ImportKeywordType;
        [Export] [Name(Annotation)] internal static ClassificationTypeDefinition AnnotationType;
        [Export] [Name(FunctionCall)] internal static ClassificationTypeDefinition FunctionCallType;
        [Export] [Name(BuiltinFunction)] internal static ClassificationTypeDefinition BuiltinFunctionType;
        [Export] [Name(ExplicitMathFunction)] internal static ClassificationTypeDefinition ExplicitMathFunctionType;
        [Export] [Name(Operator)] internal static ClassificationTypeDefinition OperatorType;
        [Export] [Name(Punctuation)] internal static ClassificationTypeDefinition PunctuationType;
    }
}
