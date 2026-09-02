using System.Collections.Generic;
using System.Linq;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;

namespace RinLang.VSSDK.Classification
{
    /// <summary>
    /// Classifies Rin source text line-by-line using <see cref="RinTokenizer"/> and
    /// maps each token to its registered <see cref="IClassificationType"/>.
    /// </summary>
    internal sealed class RinClassifier : IClassifier
    {
        private readonly ITextBuffer _buffer;
        private readonly IDictionary<string, IClassificationType> _classificationTypes;

        public RinClassifier(ITextBuffer buffer, IClassificationTypeRegistryService registryService)
        {
            _buffer = buffer;

            _classificationTypes = new Dictionary<string, IClassificationType>
            {
                [RinClassificationTypes.Comment] = registryService.GetClassificationType(RinClassificationTypes.Comment),
                [RinClassificationTypes.StringLiteral] = registryService.GetClassificationType(RinClassificationTypes.StringLiteral),
                [RinClassificationTypes.Number] = registryService.GetClassificationType(RinClassificationTypes.Number),
                [RinClassificationTypes.BooleanNil] = registryService.GetClassificationType(RinClassificationTypes.BooleanNil),
                [RinClassificationTypes.MathConstant] = registryService.GetClassificationType(RinClassificationTypes.MathConstant),
                [RinClassificationTypes.Keyword] = registryService.GetClassificationType(RinClassificationTypes.Keyword),
                [RinClassificationTypes.Storage] = registryService.GetClassificationType(RinClassificationTypes.Storage),
                [RinClassificationTypes.ContainerKeyword] = registryService.GetClassificationType(RinClassificationTypes.ContainerKeyword),
                [RinClassificationTypes.RelationKeyword] = registryService.GetClassificationType(RinClassificationTypes.RelationKeyword),
                [RinClassificationTypes.EndTag] = registryService.GetClassificationType(RinClassificationTypes.EndTag),
                [RinClassificationTypes.ImportKeyword] = registryService.GetClassificationType(RinClassificationTypes.ImportKeyword),
                [RinClassificationTypes.Annotation] = registryService.GetClassificationType(RinClassificationTypes.Annotation),
                [RinClassificationTypes.FunctionCall] = registryService.GetClassificationType(RinClassificationTypes.FunctionCall),
                [RinClassificationTypes.BuiltinFunction] = registryService.GetClassificationType(RinClassificationTypes.BuiltinFunction),
                [RinClassificationTypes.ExplicitMathFunction] = registryService.GetClassificationType(RinClassificationTypes.ExplicitMathFunction),
                [RinClassificationTypes.Operator] = registryService.GetClassificationType(RinClassificationTypes.Operator),
                [RinClassificationTypes.Punctuation] = registryService.GetClassificationType(RinClassificationTypes.Punctuation),
                [RinClassificationTypes.MakeDirective] = registryService.GetClassificationType(RinClassificationTypes.MakeDirective),
                [RinClassificationTypes.Capability] = registryService.GetClassificationType(RinClassificationTypes.Capability),
                [RinClassificationTypes.ReckonKeyword] = registryService.GetClassificationType(RinClassificationTypes.ReckonKeyword),
                [RinClassificationTypes.ReckonName] = registryService.GetClassificationType(RinClassificationTypes.ReckonName),
                [RinClassificationTypes.ReckonItem] = registryService.GetClassificationType(RinClassificationTypes.ReckonItem),
            };
        }

#pragma warning disable CS0067 // Rin's grammar is line-local; VS re-classifies changed lines automatically.
        public event System.EventHandler<ClassificationChangedEventArgs> ClassificationChanged;
#pragma warning restore CS0067

        public IList<ClassificationSpan> GetClassificationSpans(SnapshotSpan span)
        {
            var result = new List<ClassificationSpan>();
            var snapshot = span.Snapshot;

            foreach (var line in snapshot.Lines)
            {
                var lineSpan = line.Extent;
                if (!lineSpan.IntersectsWith(span) && !(lineSpan.Length == 0 && span.Contains(lineSpan.Start)))
                {
                    continue;
                }

                string text = line.GetText();
                foreach (var token in RinTokenizer.Tokenize(text).OrderBy(t => t.Start))
                {
                    if (!_classificationTypes.TryGetValue(token.Classification, out var classificationType))
                    {
                        continue;
                    }

                    var tokenSpan = new SnapshotSpan(snapshot, line.Start + token.Start, token.Length);
                    if (tokenSpan.IntersectsWith(span))
                    {
                        result.Add(new ClassificationSpan(tokenSpan, classificationType));
                    }
                }
            }

            return result;
        }
    }
}
