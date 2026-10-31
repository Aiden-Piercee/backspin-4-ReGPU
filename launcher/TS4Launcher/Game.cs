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

    /// <summary>Saves the game folder to settings.ini (game_dir).</summary>
    public static void SetGameDir(string dir)
    {
        var ini = Path.Combine(Paths.UserDir, "settings.ini");
        Directory.CreateDirectory(Paths.UserDir);
        var lines = File.Exists(ini) ? File.ReadAllLines(ini).ToList() : [];
        var i = lines.FindIndex(l => l.StartsWith("game_dir=", StringComparison.Ordinal));
        if (i >= 0) lines[i] = "game_dir=" + dir; else lines.Add("game_dir=" + dir);
        File.WriteAllLines(ini, lines);
    }

    static Process? _import;

    /// <summary>Extracts a disc image with topspin4.exe --import (no window), reporting progress (0-1000, file).</summary>
    public static async Task ImportIso(string iso, string dest, Action<int, string> progress)
    {
        if (!ExePresent) throw new FileNotFoundException("topspin4.exe was not found next to the launcher.");
        if (_import is { HasExited: false }) throw new InvalidOperationException("An import is already running.");
        var psi = new ProcessStartInfo(Paths.Exe, $"--import \"{iso}\" \"{dest}\"")
        {
            WorkingDirectory = Paths.GameRoot, UseShellExecute = false, RedirectStandardOutput = true, CreateNoWindow = true,
        };
        var p = _import = Process.Start(psi)!;
        string? error = null;
        bool done = false;
        string? line;
        while ((line = await p.StandardOutput.ReadLineAsync()) != null)
        {
            if (line.StartsWith("PROGRESS ")) {
                var parts = line.Split(' ', 3);
                if (parts.Length >= 2 && int.TryParse(parts[1], out var pm)) progress(pm, parts.Length > 2 ? parts[2] : "");
            }
            else if (line == "DONE") done = true;
            else if (line.StartsWith("ERROR ")) error = line[6..];
        }
        await p.WaitForExitAsync();
        if (_import == null) throw new OperationCanceledException("Import cancelled.");
        _import = null;
        if (error != null) throw new InvalidOperationException(error);
        if (!done) throw new InvalidOperationException("The import stopped before it finished.");
        SetGameDir(dest);
    }

    public static void CancelImport()
    {
        var p = _import;
        _import = null;
        try { p?.Kill(true); } catch { }
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
