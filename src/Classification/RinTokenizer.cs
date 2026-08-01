using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace RinLang.VSSDK.Classification
{
    /// <summary>
    /// A single classified span within one line of Rin source.
    /// </summary>
    internal readonly struct RinToken
    {
        public RinToken(int start, int length, string classification)
        {
            Start = start;
            Length = length;
            Classification = classification;
        }

        public int Start { get; }
        public int Length { get; }
        public string Classification { get; }
    }

    /// <summary>
    /// Line-oriented tokenizer that mirrors the TextMate grammar in
    /// extension/syntaxes/rin.tmLanguage.json from the original VS Code extension.
    /// Rin has no multi-line comments or multi-line strings, so a per-line regex
    /// scan (same approach VS's own C/C# classifiers used before Roslyn) is both
    /// correct and fast, and needs no persisted state between lines.
    /// </summary>
    internal static class RinTokenizer
    {
        private static readonly (Regex Pattern, string Classification)[] Rules;

        static RinTokenizer()
        {
            const RegexOptions o = RegexOptions.Compiled | RegexOptions.CultureInvariant;

            var builtins = string.Join("|", new[]
            {
                "abs","sqrt","pow","floor","ceil","round","min","max","random","len","upper","lower","trim",
                "substr","split","join","indexOf","replace","contains","charAt","toString","toNumber","toBool",
                "isBool","sum","mean","median","variance","stddev","mode","minOf","maxOf","normalize","scale",
                "shift","groupContainers","groupMembers","sectionVars","sectionNames","hasSection","insertDoc",
                "updateDoc","deleteDoc","findDoc","queryDocs","queryOneDoc","docIds","allDocs","countDocs","call",
                "callApi","push","pop","sort","keys","values","has","remove","writeFile","appendFile","readFile",
                "fileExists","deleteFile","isInstalled","listInstalled","loadInstalled"
            });

            Rules = new (Regex, string)[]
            {
                // Comments win outright over everything else on the line.
                (new Regex(@"//.*$", o), RinClassificationTypes.Comment),

                // Strings (with \" and \\ escapes).
                (new Regex(@"""(?:\\.|[^""\\])*""", o), RinClassificationTypes.StringLiteral),

                // .end/Name closing tag.
                (new Regex(@"\.end/[A-Za-z_][A-Za-z0-9_.]*", o), RinClassificationTypes.EndTag),

                // @import [as]
                (new Regex(@"@import\b(\s+as\b)?", o), RinClassificationTypes.ImportKeyword),

                // @container / @container.pipe|data|api|import|table|doc|object|portal|block, @Containers.Group, @Volume
                (new Regex(@"@(container(\.(pipe|data|api|import|table|doc|object|portal|block))?|Containers\.Group|Volume)\b", o), RinClassificationTypes.ContainerKeyword),

                // مفاهيم التنسيق والستايل المستقلة (بلا بادئة container.): @Object / @portal / @block
                (new Regex(@"@(Object|portal|block)\b", o), RinClassificationTypes.ContainerKeyword),

                // Section / Translations headers.
                (new Regex(@"\b(Section|Translations)\b", o), RinClassificationTypes.ContainerKeyword),

                // document / route / table / style keywords.
                (new Regex(@"\b(document|route|table|style)\b", o), RinClassificationTypes.RelationKeyword),

                // Bare @ annotation marker not already matched above.
                (new Regex(@"@", o), RinClassificationTypes.Annotation),

                // fun name( — function declarations (captures the name separately below).
                (new Regex(@"(?<=\bfun\s+)[A-Za-z_][A-Za-z0-9_]*(?=\s*\()", o), RinClassificationTypes.FunctionCall),

                // Storage keywords.
                (new Regex(@"\b(let|fun|text)\b", o), RinClassificationTypes.Storage),

                // Control keywords / logical operators / print.
                (new Regex(@"\b(if|else|while|return|break|continue|and|or|print)\b", o), RinClassificationTypes.Keyword),

                // Boolean / nil constants.
                (new Regex(@"\b(true|false|nil)\b", o), RinClassificationTypes.BooleanNil),

                // Math constants.
                (new Regex(@"(?<![\w.])(PI|E)(?![\w])", o), RinClassificationTypes.MathConstant),

                // Explicit math functions.
                (new Regex($@"(?<![\w.])(Addition|Subtraction|Multiplication|Equal)(?=\s*\()", o), RinClassificationTypes.ExplicitMathFunction),

                // Built-in stdlib functions.
                (new Regex($@"(?<![\w.])({builtins})(?=\s*\()", o), RinClassificationTypes.BuiltinFunction),

                // Any other identifier( — a plain function call.
                (new Regex(@"[A-Za-z_][A-Za-z0-9_]*(?=\s*\()", o), RinClassificationTypes.FunctionCall),

                // Numbers (int / float).
                (new Regex(@"\b\d+\.?\d*\b", o), RinClassificationTypes.Number),

                // Pipeline operator |>
                (new Regex(@"\|>", o), RinClassificationTypes.Operator),

                // Comparison / assignment / arithmetic / logical-not operators.
                (new Regex(@"(==|!=|<=|>=|<|>|=|\+|\-|\*|/|%|!)", o), RinClassificationTypes.Operator),

                // Punctuation.
                (new Regex(@"[;,:.{}\[\]()]", o), RinClassificationTypes.Punctuation),
            };
        }

        public static IEnumerable<RinToken> Tokenize(string lineText)
        {
            var claimed = new bool[lineText.Length];

            foreach (var (pattern, classification) in Rules)
            {
                foreach (Match match in pattern.Matches(lineText))
                {
                    if (match.Length == 0)
                    {
                        continue;
                    }

                    if (IsAlreadyClaimed(claimed, match.Index, match.Length))
                    {
                        continue;
                    }

                    Claim(claimed, match.Index, match.Length);
                    yield return new RinToken(match.Index, match.Length, classification);
                }
            }
        }

        private static bool IsAlreadyClaimed(bool[] claimed, int start, int length)
        {
            for (int i = start; i < start + length; i++)
            {
                if (claimed[i])
                {
                    return true;
                }
            }
            return false;
        }

        private static void Claim(bool[] claimed, int start, int length)
        {
            for (int i = start; i < start + length; i++)
            {
                claimed[i] = true;
            }
        }
    }
}
