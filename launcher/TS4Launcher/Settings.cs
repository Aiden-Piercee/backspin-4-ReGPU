namespace TS4Launcher;

/// <summary>
/// The game's settings file, user\settings.ini next to topspin4.exe (src/ts4_settings.cpp reads and writes the
/// same keys). Keys this launcher does not edit are kept as they are.
/// </summary>
public sealed class GameSettings
{
    public string Profile { get; set; } = "Player";
    public string Resolution { get; set; } = "1280x720";
    public string Aspect { get; set; } = "auto";
    public bool Fullscreen { get; set; }
    public int Msaa { get; set; } = 1;
    public string Upscaler { get; set; } = "off";
    public int RenderScale { get; set; } = 100;
    public double Sharpness { get; set; } = 0.5;
    public bool SkipIntro { get; set; } = true;
    public bool FastGeometry { get; set; }
    public int Language { get; set; } = 1;

    static string IniPath => Path.Combine(Paths.UserDir, "settings.ini");

    static readonly System.Globalization.CultureInfo Inv = System.Globalization.CultureInfo.InvariantCulture;

    public static GameSettings Load()
    {
        var s = new GameSettings();
        foreach (var (k, v) in Read())
            switch (k)
            {
                case "profile": if (v.Length > 0) s.Profile = v; break;
                case "resolution": s.Resolution = v; break;
                case "aspect": s.Aspect = v; break;
                case "fullscreen": s.Fullscreen = v == "1"; break;
                case "msaa": s.Msaa = int.TryParse(v, out var m) ? m : 1; break;
                case "upscaler": s.Upscaler = v; break;
                case "render_scale": s.RenderScale = int.TryParse(v, out var rs) ? rs : 100; break;
                case "sharpness": s.Sharpness = double.TryParse(v, System.Globalization.NumberStyles.Float, Inv, out var sh) ? sh : 0.5; break;
                case "skip_intro": s.SkipIntro = v != "0"; break;
                case "fast_geometry": s.FastGeometry = v == "1"; break;
                case "language": s.Language = int.TryParse(v, out var l) && l > 0 ? l : 1; break;
            }
        return s;
    }

    public void Save()
    {
        var edit = new Dictionary<string, string>
        {
            ["profile"] = new string(Profile.Where(c => c >= 32 && c < 127).Take(15).ToArray()) is { Length: > 0 } p ? p : "Player",
            ["resolution"] = Resolution,
            ["aspect"] = Aspect,
            ["fullscreen"] = Fullscreen ? "1" : "0",
            ["msaa"] = Msaa.ToString(),
            ["upscaler"] = Upscaler,
            ["render_scale"] = Math.Clamp(RenderScale, 25, 100).ToString(),
            ["sharpness"] = Math.Clamp(Sharpness, 0, 1).ToString(Inv),
            ["skip_intro"] = SkipIntro ? "1" : "0",
            ["fast_geometry"] = FastGeometry ? "1" : "0",
            ["language"] = Language.ToString(),
        };
        var lines = File.Exists(IniPath) ? File.ReadAllLines(IniPath).ToList() : ["# Top Spin 4 (TS4) settings - edited by the launcher."];
        foreach (var (k, v) in edit)
        {
            var i = lines.FindIndex(l => l.StartsWith(k + "=", StringComparison.Ordinal));
            if (i >= 0) lines[i] = k + "=" + v; else lines.Add(k + "=" + v);
        }
        Directory.CreateDirectory(Paths.UserDir);
        File.WriteAllLines(IniPath, lines);
    }

    static IEnumerable<(string, string)> Read()
    {
        if (!File.Exists(IniPath)) yield break;
        foreach (var line in File.ReadAllLines(IniPath))
        {
            var eq = line.IndexOf('=');
            if (eq <= 0 || line[0] == '#' || line[0] == ';') continue;
            yield return (line[..eq], line[(eq + 1)..]);
        }
    }
}

public static class Paths
{
    /// <summary>Folder with topspin4.exe: this launcher's folder, or the one above it (launcher\TS4Launcher.exe).</summary>
    public static string GameRoot { get; } = Find();
    public static string Exe => Path.Combine(GameRoot, "topspin4.exe");
    /// <summary>TS4_USER_DIR if set (side-by-side instances), else user\ next to the exe, as the game does.</summary>
    public static string UserDir => Environment.GetEnvironmentVariable("TS4_USER_DIR") is { Length: > 0 } d ? Path.GetFullPath(d) : Path.Combine(GameRoot, "user");

    /// <summary>The console's update: device (default.xexp, ...), as src/loader.cpp reads it.</summary>
    public static string UpdateDir => Path.Combine(UserDir, "update");

    /// <summary>Downloadable content, shared by all profiles: dlc\&lt;package&gt; (src/kernel/imports_xam.cpp).</summary>
    public static string DlcDir => Path.Combine(UserDir, "dlc");

    static string Find()
    {
        var here = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        foreach (var dir in new[] { here, Path.GetDirectoryName(here), Path.GetDirectoryName(Path.GetDirectoryName(here)) })
            if (dir != null && File.Exists(Path.Combine(dir, "topspin4.exe"))) return dir;
        return here;
    }
}
