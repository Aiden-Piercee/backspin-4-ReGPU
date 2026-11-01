namespace TS4Launcher;

/// <summary>Appends lines to user\launcher.log (never throws).</summary>
public static class Log
{
    static readonly object Gate = new();

    public static void Write(string text)
    {
        try
        {
            lock (Gate)
            {
                Directory.CreateDirectory(Paths.UserDir);
                File.AppendAllText(Path.Combine(Paths.UserDir, "launcher.log"), $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {text}{Environment.NewLine}");
            }
        }
        catch { }
    }
}
