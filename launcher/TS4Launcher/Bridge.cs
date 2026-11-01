using System.Text.Json;
using System.Text.Json.Nodes;
using Photino.NET;

namespace TS4Launcher;

/// <summary>
/// The page's calls: { id, cmd, args } over the window's message channel (wwwroot/host.js), answered with
/// { id, ok, result | error }.
/// </summary>
public sealed class Bridge
{
    static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    readonly PhotinoWindow _window;

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
                try { window.Invoke(() => DropTarget.Install(window.WindowHandle, OnDropped, over => Push("dragging", over))); } catch { }
            }
        });
    }

    /// <summary>Files dropped on the window: a disc image, a game folder, or the title update package.</summary>
    async void OnDropped(string[] paths)
    {
        try
        {
            foreach (var path in paths)
            {
                if (Directory.Exists(path))
                {
                    var game = FindGameFolder(path) ?? throw new InvalidOperationException($"No default.xex in {path} (or in one folder below it).");
                    Game.SetGameDir(game);
                    Push("dropped", new { kind = "folder", name = game });
                }
                else if (path.EndsWith(".iso", StringComparison.OrdinalIgnoreCase))
                {
                    Push("dropped", new { kind = "iso", name = Path.GetFileName(path) });
                    await Game.ImportIso(path, Game.GameDir, (pm, file) => Push("import", new { pm, file }));
                    Push("dropped", new { kind = "isodone", name = Path.GetFileName(path) });
                }
                else if (path.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
                {
                    Push("dropped", new { kind = "zip", name = Path.GetFileName(path) });
                    var what = await Zips.Install(path, (pm, file) => Push("import", new { pm, file }));
                    Push("dropped", new { kind = what == "iso" ? "isodone" : what, name = Path.GetFileName(path) });
                }
                else if (Stfs.IsPackage(path))
                {
                    Stfs.InstallTitleUpdate(path, Paths.UpdateDir);
                    Push("dropped", new { kind = "update", name = Path.GetFileName(path) });
                }
                else throw new InvalidOperationException($"Don't know what {Path.GetFileName(path)} is: drop a disc image (.iso), the game folder, a .zip of either, or the title update package.");
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { Push("dropped", new { kind = "error", name = ex.Message }); }
        Push("state", State());
    }

    static string? FindGameFolder(string dir)
    {
        if (File.Exists(Path.Combine(dir, "default.xex"))) return dir;
        foreach (var sub in Directory.EnumerateDirectories(dir))
            if (File.Exists(Path.Combine(sub, "default.xex"))) return sub;
        return null;
    }

    async void OnMessage(string text)
    {
        JsonNode? msg;
        try { msg = JsonNode.Parse(text); } catch { return; }
        var id = msg?["id"]?.GetValue<int>() ?? 0;
        var cmd = msg?["cmd"]?.GetValue<string>() ?? "";
        try { Post(new { id, ok = true, result = await Run(cmd, msg?["args"]) }); }
        catch (Exception ex) { Post(new { id, ok = false, error = ex.Message }); }
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
                    try { await Game.ImportIso(files[0], Game.GameDir, (pm, file) => Push("import", new { pm, file })); }
                    catch (OperationCanceledException) { }
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
                    if (files[0].EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) await Zips.Install(files[0], (pm, file) => Push("import", new { pm, file }));
                    else Stfs.InstallTitleUpdate(files[0], Paths.UpdateDir);
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

    static object State() => new
    {
        settings = GameSettings.Load(),
        running = Game.Running,
        exe = Game.ExePresent,
        files = Game.FilesPresent,
        gameDir = Game.GameDir,
        userDir = Paths.UserDir,
        installDir = Paths.GameRoot,
        shaders = ShaderCache.Get(),
        update = File.Exists(Path.Combine(Paths.UpdateDir, "default.xexp")),
        resolutions = new[] { "1280x720", "1920x1080", "2560x1440", "3440x1440", "3840x2160" },
    };
}
