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
    }

    void OnMessage(string text)
    {
        JsonNode? msg;
        try { msg = JsonNode.Parse(text); } catch { return; }
        var id = msg?["id"]?.GetValue<int>() ?? 0;
        var cmd = msg?["cmd"]?.GetValue<string>() ?? "";
        try { Post(new { id, ok = true, result = Run(cmd, msg?["args"]) }); }
        catch (Exception ex) { Post(new { id, ok = false, error = ex.Message }); }
    }

    void Post(object o) => _window.SendWebMessage(JsonSerializer.Serialize(o, Json));

    object? Run(string cmd, JsonNode? a)
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
            case "setup":
                Game.Setup();
                return null;
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
        resolutions = new[] { "1280x720", "1920x1080", "2560x1440", "3440x1440", "3840x2160" },
    };
}
