using System;

namespace RinLang.VSSDK
{
    /// <summary>
    /// GUIDs shared between the managed package code and RinLangPackage.vsct.
    /// Keep these two files in sync if you ever regenerate them.
    /// </summary>
    internal static class PackageGuids
    {
        public const string PackageGuidString = "04db0285-87e6-4ba8-8adf-9f58ab989a15";
        public const string RinCommandSetGuidString = "abdbacfa-5434-418b-8cba-4130edcff327";
        public const string RinLanguageGuidString = "3e2a9c7d-5f1b-4a8e-9d0c-7b6e5f4a3d21";

        public static readonly Guid RinCommandSet = new Guid(RinCommandSetGuidString);
    }
}
