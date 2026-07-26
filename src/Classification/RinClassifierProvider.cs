using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;
using RinLang.VSSDK.ContentDefinition;

namespace RinLang.VSSDK.Classification
{
    [Export(typeof(IClassifierProvider))]
    [ContentType(RinContentDefinition.RinContentTypeName)]
    internal sealed class RinClassifierProvider : IClassifierProvider
    {
        [Import]
        internal IClassificationTypeRegistryService ClassificationRegistry { get; set; }

        public IClassifier GetClassifier(ITextBuffer buffer)
        {
            return buffer.Properties.GetOrCreateSingletonProperty(
                () => new RinClassifier(buffer, ClassificationRegistry));
        }
    }
}
