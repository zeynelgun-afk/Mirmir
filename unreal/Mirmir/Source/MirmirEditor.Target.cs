using UnrealBuildTool;
using System.Collections.Generic;

public class MirmirEditorTarget : TargetRules
{
    public MirmirEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("Mirmir");
    }
}
