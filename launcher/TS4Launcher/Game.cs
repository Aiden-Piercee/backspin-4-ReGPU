using System.Diagnostics;

namespace TS4Launcher;

public static class Game
{
    public static bool Running => Process.GetProcessesByName("topspin4").Length > 0;

    /// <summary>The game folder from settings.ini (game_dir), or "game" next to the exe.</summary>
    public static string GameDir
    {
        get
        {
            var ini = Path.Combine(Paths.UserDir, "settings.ini");
            if (File.Exists(ini))
                foreach (var l in File.ReadAllLines(ini))
                    if (l.StartsWith("game_dir=", StringComparison.Ordinal) && l.Length > 9)
                        return Path.GetFullPath(l[9..], Paths.GameRoot);
            return Path.Combine(Paths.GameRoot, "game");
        }
    }

    public static bool FilesPresent => File.Exists(Path.Combine(GameDir, "default.xex"));
    public static bool ExePresent => File.Exists(Paths.Exe);

    /// <summary>Starts the game with the saved settings; the game's own window is skipped (--no-launcher).</summary>
    public static void Launch()
    {
        if (!ExePresent) throw new FileNotFoundException("topspin4.exe was not found next to the launcher.");
        if (!FilesPresent) throw new InvalidOperationException("The game files are missing. Use Import game first.");
        Process.Start(new ProcessStartInfo(Paths.Exe, "--no-launcher") { WorkingDirectory = Paths.GameRoot, UseShellExecute = false });
    }

    /// <summary>Game import and patches still live in the game's own window (topspin4.exe --setup).</summary>
    public static void Setup()
    {
        if (!ExePresent) throw new FileNotFoundException("topspin4.exe was not found next to the launcher.");
        Process.Start(new ProcessStartInfo(Paths.Exe, "--setup") { WorkingDirectory = Paths.GameRoot, UseShellExecute = false });
    }

    public static void OpenFolder(string path)
    {
        Directory.CreateDirectory(path);
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{path}\"") { UseShellExecute = false });
    }
}

public static class ShaderCache
{
    static string Dir => Path.Combine(Paths.UserDir, "shaders");
    static IEnumerable<string> Files() =>
        (Directory.Exists(Dir) ? Directory.EnumerateFiles(Dir, "*", SearchOption.AllDirectories) : [])
        .Concat(Directory.Exists(Paths.UserDir) ? Directory.EnumerateFiles(Paths.UserDir, "pipelines.*") : []);

    public static object Get() { var f = Files().Select(p => new FileInfo(p)).ToList(); return new { files = f.Count, bytes = f.Sum(x => x.Length) }; }

    public static void Delete()
    {
        if (Game.Running) throw new InvalidOperationException("Close the game first: it is using the shader cache.");
        foreach (var f in Files().ToList()) File.Delete(f);
    }
}
