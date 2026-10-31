namespace TS4Launcher;

internal static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        // --play: start the game with the saved settings, without the window (shortcuts, tests)
        if (args.Contains("--play"))
        {
            try { Game.Launch(); return 0; }
            catch (Exception ex) { Error(ex.Message); return 1; }
        }
        // one window: starting the launcher again does nothing
        using var single = new Mutex(true, "TS4ReGPU.Launcher", out bool first);
        if (!first) return 0;
        Photino.NET.PhotinoWindow window;
        try { window = Web.Create("Top Spin 4 ReGPU", "index.html", 1180, 760, 900, 640); }
        catch (Exception ex)
        {
            Error("The launcher needs Microsoft Edge WebView2 (part of Windows 11; a free download for Windows 10).\n\n" + ex.Message);
            return 1;
        }
        _ = new Bridge(window);
        window.WaitForClose();
        return 0;
    }

    static void Error(string text) =>
        MessageBox(IntPtr.Zero, text, "Top Spin 4 ReGPU", 0x10);

    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    static extern int MessageBox(IntPtr hWnd, string text, string caption, uint type);
}
