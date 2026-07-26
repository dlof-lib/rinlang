using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Utilities;

namespace RinLang.VSSDK.ContentDefinition
{
    /// <summary>
    /// Registers the "rin" content type and associates it with .rin files
    /// (including the *.og.rin / *.min.rin patterns used by DLoF/Rin projects).
    /// This is the managed-code equivalent of the "languages" + "extensions"
    /// section of the VS Code package.json.
    /// </summary>
    internal static class RinContentDefinition
    {
        public const string RinContentTypeName = "rin";

        [Export]
        [Name(RinContentTypeName)]
        [BaseDefinition("code")]
        internal static ContentTypeDefinition RinContentTypeDefinition;

        [Export]
        [FileExtension(".rin")]
        [ContentType(RinContentTypeName)]
        internal static FileExtensionToContentTypeDefinition RinFileExtensionDefinition;
    }
}
