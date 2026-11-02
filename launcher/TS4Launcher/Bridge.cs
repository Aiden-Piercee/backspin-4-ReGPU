using System.Text.Json;
using System.Text.Json.Nodes;
using Photino.NET;

namespace TS4Launcher;

/// <summary>
/// The page's calls: { id, cmd, args } over the window's message channel (wwwroot/host.js), answered with
/// { id, ok, result | error }. Pushed to the page: import (progress), dropped (what was installed), dragging,
/// over and dropped-at (a file drag over the window, with client pixels), state.
/// </summary>
public sealed class Bridge
{
    static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    readonly PhotinoWindow _window;
    bool _busy, _dragging;
    long _lastOver;

    public Bridge(PhotinoWindow window)
    {
        _window = window;
        window.RegisterWebMessageReceivedHandler((_, text) => OnMessage(text));
        // Drops: the window's child windows exist a moment after the page loads, so take over a few times.
        _ = Task.Run(async () =>
        {
            foreach (var wait in new[] { 1500, 2000, 4000, 8000 })
            {
                await Task.Delay(wait);
                // RegisterDragDrop must run on the window's own (STA) thread, not the pool thread this loop is on.
                try { window.Invoke(() => DropTarget.Install(window.WindowHandle, OnDropped, OnHover)); }
                catch (Exception ex) { Log.Write("drop: install failed: " + ex); }
            }
        });
    }

    /// <summary>A file drag over the window (x, y: client pixels of the window that got it): lights the page's hint and, at most every 60 ms, tells it where the pointer is.</summary>
    void OnHover(bool on, int x, int y)
    {
        if (!on)
        {
            _dragging = false;
            Push("dragging", false);
            Push("over", new { x = -1, y = -1 });
            return;
        }
        if (!_dragging) { _dragging = true; Push("dragging", true); }
        var now = Environment.TickCount64;
        if (now - _lastOver < 60) return;
        _lastOver = now;
        Push("over", new { x, y });
    }

    /// <summary>Files dropped: the page works out which tile is under the point and calls "drop".</summary>
    void OnDropped(string[] paths, int x, int y)
    {
        Log.Write($"drop: received {paths.Length} path(s) at client ({x},{y}): {string.Join(" | ", paths)}");
        Push("dropped-at", new { paths, x, y });
    }

    /// <summary>
    /// Installs what was dropped on a tile ("game", "update", "dlc", or "auto" outside the tiles). Dropped things are
    /// recognised by what they hold: an .iso, a folder with default.xex, a folder with default.xexp, a zip of those,
    /// an Xbox 360 package (a title update when it holds default.xexp, otherwise DLC). The dlc tile installs every
    /// package it finds. Failures throw with a message the page shows.
    /// </summary>
    async Task HandleDrop(string tile, string[] paths)
    {
        if (_busy) throw new InvalidOperationException("Something is still being installed. Wait for it to finish.");
        _busy = true;
        try
        {
            foreach (var path in paths)
            {
                try { await Task.Run(() => DropOne(tile, path)); }
                catch (OperationCanceledException) { }
                catch (Exception ex) { Log.Write($"drop: {tile}: {path}: {ex}"); throw; }
            }
        }
        finally { _busy = false; }
    }

    async Task DropOne(string tile, string path)
    {
        Log.Write($"drop: {tile}: {path}");
        var name = Path.GetFileName(path.TrimEnd('\\', '/'));
        if (name.Length == 0) name = path;
        if (!File.Exists(path) && !Directory.Exists(path)) throw new FileNotFoundException($"{path} was not found.");
        void Progress(int pm, string file) => Push("import", new { pm, file });

        if (tile == "dlc")
        {
            var n = InstallDlcFrom(path);
            if (n == 0) throw new InvalidOperationException($"No Xbox 360 DLC packages in {name}.");
            Push("dropped", new { kind = "dlc", name = n == 1 ? name : $"{n} packages" });
            return;
        }

        if (Directory.Exists(path))
        {
            if (tile == "update")
            {
                var u = FindDirWith(path, "default.xexp", 3) ?? throw new InvalidOperationException($"No default.xexp in {name} (searched up to 3 folders deep).");
                InstallUpdateFolder(u);
                Push("dropped", new { kind = "update", name });
                return;
            }
            if (FindDirWith(path, "default.xex", 3) is { } game)
            {
                Game.SetGameDir(game);
                Push("dropped", new { kind = "folder", name = game });
                return;
            }
            if (FindDirWith(path, "default.xexp", 3) is { } upd)
            {
                InstallUpdateFolder(upd);
                Push("dropped", new { kind = "update", name });
                return;
            }
            throw new InvalidOperationException($"No default.xex (the game) or default.xexp (a title update) in {name} (searched up to 3 folders deep).");
        }

        if (path.EndsWith(".iso", StringComparison.OrdinalIgnoreCase))
        {
            Push("dropped", new { kind = "iso", name });
            await Game.ImportIso(path, Game.GameDir, Progress);
            Push("dropped", new { kind = "isodone", name });
        }
        else if (path.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
        {
            Push("dropped", new { kind = "zip", name });
            var what = await Zips.Install(path, Progress);
            Push("dropped", new { kind = what == "iso" ? "isodone" : what, name });
        }
        else if (Stfs.IsPackage(path))
        {
            var kind = tile == "update" ? "update" : Stfs.Install(path, name);
            if (tile == "update") Stfs.InstallTitleUpdate(path, Paths.UpdateDir);
            Push("dropped", new { kind, name });
        }
        else throw new InvalidOperationException($"Don't know what {name} is: drop a disc image (.iso), the game folder, a .zip of either, or an Xbox 360 package.");
    }

    /// <summary>Installs every Xbox 360 package in a file, a folder (searched recursively) or a zip as DLC; returns how many.</summary>
    static int InstallDlcFrom(string path)
    {
        int n = 0;
        if (Directory.Exists(path))
        {
            var opts = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true };
            foreach (var f in Directory.EnumerateFiles(path, "*", opts))
            {
                if (f.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) n += Zips.InstallDlc(f);
                else if (Stfs.IsPackage(f)) { Stfs.InstallDlc(f, Path.GetFileName(f)); n++; }
            }
        }
        else if (path.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) n = Zips.InstallDlc(path);
        else if (Stfs.IsPackage(path)) { Stfs.InstallDlc(path, Path.GetFileName(path)); n = 1; }
        else throw new InvalidOperationException($"{Path.GetFileName(path)} is not an Xbox 360 package (LIVE, PIRS or CON).");
        return n;
    }

    /// <summary>The folder at or below <paramref name="dir"/> (up to <paramref name="depth"/> levels) that holds <paramref name="file"/>.</summary>
    static string? FindDirWith(string dir, string file, int depth)
    {
        if (File.Exists(Path.Combine(dir, file))) return dir;
        if (depth == 0) return null;
        try
        {
            foreach (var sub in Directory.EnumerateDirectories(dir))
                if (FindDirWith(sub, file, depth - 1) is { } found) return found;
        }
        catch (UnauthorizedAccessException) { }
        return null;
    }

    /// <summary>Copies a folder holding default.xexp over user\update.</summary>
    static void InstallUpdateFolder(string src)
    {
        var tmp = Paths.UpdateDir + ".new";
        if (Directory.Exists(tmp)) Directory.Delete(tmp, true);
        CopyDir(src, tmp);
        if (Directory.Exists(Paths.UpdateDir)) Directory.Delete(Paths.UpdateDir, true);
        Directory.Move(tmp, Paths.UpdateDir);
    }

    static void CopyDir(string src, string dest)
    {
        Directory.CreateDirectory(dest);
        foreach (var f in Directory.GetFiles(src)) File.Copy(f, Path.Combine(dest, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(src)) CopyDir(d, Path.Combine(dest, Path.GetFileName(d)));
    }

    async void OnMessage(string text)
    {
        JsonNode? msg;
        try { msg = JsonNode.Parse(text); } catch { return; }
        var id = msg?["id"]?.GetValue<int>() ?? 0;
        var cmd = msg?["cmd"]?.GetValue<string>() ?? "";
        try { Post(new { id, ok = true, result = await Run(cmd, msg?["args"]) }); }
        catch (Exception ex)
        {
            Log.Write($"command {cmd} failed: {ex}");
            Post(new { id, ok = false, error = ex.Message });
        }
    }

    void Post(object o) => _window.SendWebMessage(JsonSerializer.Serialize(o, Json));
    void Push(string name, object data) => Post(new { @event = name, data });

    async Task<object?> Run(string cmd, JsonNode? a)
    {
        switch (cmd)
        {
            case "state": return State();
            case "save":
                a.Deserialize<GameSettings>(Json)!.Save();
                return State();
            case "launch":
                a?["settings"].Deserialize<GameSettings>(Json)?.Save();
                if (Game.Running) throw new InvalidOperationException("The game is already running.");
                Game.Launch();
                return null;
            case "import.iso":
                {
                    var files = await _window.ShowOpenFileAsync("Choose the Top Spin 4 disc image (ISO)", null, false, [("Xbox 360 disc image", ["*.iso"]), ("All files", ["*"])]);
                    if (files is not { Length: > 0 }) return State();
                    await HandleDrop("game", [files[0]]);
                    return State();
                }
            case "import.cancel":
                Game.CancelImport();
                return null;
            case "import.folder":
                {
                    var picked = await _window.ShowOpenFolderAsync("Choose the folder with the game files (default.xex)", null, false);
                    if (picked is not { Length: > 0 }) return State();
                    if (!File.Exists(Path.Combine(picked[0], "default.xex"))) throw new InvalidOperationException("That folder has no default.xex.");
                    Game.SetGameDir(picked[0]);
                    return State();
                }
            case "update.add":
                {
                    var files = await _window.ShowOpenFileAsync("Choose the title update package", null, false, [("Title update or zip", ["*"])]);
                    if (files is not { Length: > 0 }) return State();
                    await HandleDrop("update", [files[0]]);
                    return State();
                }
            case "dlc.add":
                {
                    var files = await _window.ShowOpenFileAsync("Choose the DLC packages", null, true, [("DLC packages or zip", ["*"])]);
                    if (files is not { Length: > 0 }) return State();
                    await HandleDrop("dlc", files);
                    return State();
                }
            case "drop":
                {
                    var tile = a?["tile"]?.GetValue<string>() ?? "auto";
                    var paths = a?["paths"].Deserialize<string[]>(Json) ?? [];
                    await HandleDrop(tile, paths);
                    return State();
                }
            case "shaders.delete":
                ShaderCache.Delete();
                return State();
            case "open":
                Game.OpenFolder(a!.GetValue<string>() switch
                {
                    "user" => Paths.UserDir,
                    "game" => Game.GameDir,
                    "install" => Paths.GameRoot,
                    _ => throw new ArgumentException("unknown folder"),
                });
                return null;
            default: throw new ArgumentException("unknown command " + cmd);
        }
    }

    public static readonly string Version = typeof(Bridge).Assembly.GetName().Version is { } v ? $"{v.Major}.{v.Minor}.{v.Build}" : "?";

    static object State() => new
    {
        version = Version,
        settings = GameSettings.Load(),
        running = Game.Running,
        exe = Game.ExePresent,
        files = Game.FilesPresent,
        gameDir = Game.GameDir,
        userDir = Paths.UserDir,
        installDir = Paths.GameRoot,
        shaders = ShaderCache.Get(),
        update = File.Exists(Path.Combine(Paths.UpdateDir, "default.xexp")),
        dlc = Directory.Exists(Paths.DlcDir) ? Directory.GetDirectories(Paths.DlcDir).Length : 0,
        resolutions = new[] { "1280x720", "1920x1080", "2560x1440", "3440x1440", "3840x2160" },
    };
}
