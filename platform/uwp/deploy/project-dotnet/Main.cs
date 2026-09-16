using System.Runtime.CompilerServices;
using Godot;

// The line run.ps1 -DotNet waits for: C# ran on the headset, out of one NativeAOT library.
public partial class Main : Node
{
    public override void _Ready()
    {
        GD.Print($"BOOT: C# on Godot {Engine.GetVersionInfo()["string"]}, {OS.GetName()} {OS.GetVersion()} ({OS.GetModelName()}), " +
                 $"{(RuntimeFeature.IsDynamicCodeSupported ? "JIT" : "NativeAOT")}, {System.Environment.Version}");
        GetTree().Quit();
    }
}
