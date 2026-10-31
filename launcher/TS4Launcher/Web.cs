using Photino.NET;

namespace TS4Launcher;

/// <summary>
/// The launcher window: Photino showing wwwroot (WebView2). Pages come from the launcher's own scheme handler
/// at https://launcher.ts4/ (no server, nothing on the network).
/// </summary>
public static class Web
{
    public const string Base = "https://launcher.ts4/";

    public static PhotinoWindow Create(string title, string page, int width, int height, int minWidth, int minHeight)
    {
        var icon = Path.Combine(AppContext.BaseDirectory, "ts4.ico");
        var w = new PhotinoWindow()
            .SetTitle(title)
            .SetSize(width, height)
            .SetMinSize(minWidth, minHeight)
            .SetUseOsDefaultSize(false)
            .Center()
            .SetLogVerbosity(0)
            .SetContextMenuEnabled(false)
            .SetDevToolsEnabled(Environment.GetEnvironmentVariable("TS4_LAUNCHER_DEVTOOLS") == "1")
            .SetTemporaryFilesPath(Path.Combine(Path.GetTempPath(), "TS4Launcher-webview"))
            .RegisterCustomSchemeHandler("https", Serve);
        if (File.Exists(icon)) w.SetIconFile(icon);
        w.Load(new Uri(Base + page));
        return w;
    }

    // Only the launcher's own host; anything else gets nothing.
    static Stream? Serve(object sender, string scheme, string url, out string contentType)
    {
        contentType = "text/plain";
        if (!url.StartsWith(Base, StringComparison.OrdinalIgnoreCase)) return null;
        var rel = Uri.UnescapeDataString(new Uri(url).AbsolutePath.TrimStart('/'));
        var root = Path.Combine(AppContext.BaseDirectory, "wwwroot");
        contentType = Path.GetExtension(rel).ToLowerInvariant() switch
        {
            ".html" => "text/html",
            ".js" => "text/javascript",
            ".css" => "text/css",
            ".png" => "image/png",
            ".svg" => "image/svg+xml",
            _ => "application/octet-stream",
        };
        var full = Path.GetFullPath(Path.Combine(root, rel));
        if (!full.StartsWith(Path.GetFullPath(root) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) || !File.Exists(full)) return null;
        try { return contentType == "text/html" ? Stamped(full, root) : File.OpenRead(full); } catch { return null; }
    }

    // Scripts and stylesheets named by their file's time ("app.js?v=<ticks>"): WebView2 caches them, and a handler
    // can't send headers, so an updated launcher would keep running the old app.js.
    static Stream Stamped(string page, string root)
    {
        var html = System.Text.RegularExpressions.Regex.Replace(File.ReadAllText(page), @"(src|href)=""([\w.-]+\.(?:js|css))""", m =>
        {
            var file = Path.Combine(root, m.Groups[2].Value);
            return File.Exists(file) ? $"{m.Groups[1].Value}=\"{m.Groups[2].Value}?v={File.GetLastWriteTimeUtc(file).Ticks}\"" : m.Value;
        });
        return new MemoryStream(System.Text.Encoding.UTF8.GetBytes(html));
    }
}
