using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace TS4Launcher;

/// <summary>
/// File drops onto the launcher window. WebView2 takes drops itself (and would navigate to the dropped file), so
/// this replaces the OLE drop target of the window and of each of its child windows with one that reads the
/// dropped paths (CF_HDROP) and hands them, with the drop point in screen coordinates, to the Bridge. The child
/// windows appear a moment after the page loads, so <see cref="Install"/> is repeated for a few seconds. It must
/// run on the window's own (STA) thread.
/// </summary>
[ComVisible(true)]
public sealed class DropTarget : IDropTarget
{
    readonly Action<string[], int, int> _dropped;
    readonly Action<bool, int, int> _hover;

    DropTarget(Action<string[], int, int> dropped, Action<bool, int, int> hover) { _dropped = dropped; _hover = hover; }

    /// <summary>Takes over the drop target of the window and its child windows; logs the result of each.</summary>
    public static void Install(IntPtr window, Action<string[], int, int> dropped, Action<bool, int, int> hover)
    {
        if (window == IntPtr.Zero) { Log.Write("drop: window handle is 0"); return; }
        var ole = OleInitialize(IntPtr.Zero);
        var target = new DropTarget(dropped, hover);
        int taken = 0, refused = 0;
        void Take(IntPtr h)
        {
            var revoke = RevokeDragDrop(h);
            var reg = RegisterDragDrop(h, target);
            if (reg == 0) taken++; else refused++;
            if (reg != 0) Log.Write($"drop: window {h:X} refused: revoke 0x{revoke:X8}, register 0x{reg:X8}");
        }
        Take(window);
        EnumChildWindows(window, (h, _) => { Take(h); return true; }, IntPtr.Zero);
        Log.Write($"drop: OleInitialize 0x{ole:X8}, windows taken {taken}, refused {refused}");
    }

    /// <summary>Screen coordinates to client coordinates of <paramref name="window"/>.</summary>
    public static (int X, int Y) ToClient(IntPtr window, int x, int y)
    {
        var p = new POINT { X = x, Y = y };
        ScreenToClient(window, ref p);
        return (p.X, p.Y);
    }

    const int DropEffectCopy = 1;

    public int DragEnter(IDataObject data, int keys, POINT pt, ref int effect)
    {
        effect = HasFiles(data) ? DropEffectCopy : 0;
        if (effect != 0) _hover(true, pt.X, pt.Y);
        return 0;
    }

    public int DragOver(int keys, POINT pt, ref int effect) { effect = DropEffectCopy; _hover(true, pt.X, pt.Y); return 0; }

    public int DragLeave() { _hover(false, 0, 0); return 0; }

    public int Drop(IDataObject data, int keys, POINT pt, ref int effect)
    {
        _hover(false, 0, 0);
        var paths = Paths(data);
        effect = paths.Length > 0 ? DropEffectCopy : 0;
        if (paths.Length > 0) _dropped(paths, pt.X, pt.Y);
        return 0;
    }

    static FORMATETC HDrop() => new() { cfFormat = 15, ptd = IntPtr.Zero, dwAspect = DVASPECT.DVASPECT_CONTENT, lindex = -1, tymed = TYMED.TYMED_HGLOBAL };

    static bool HasFiles(IDataObject data)
    {
        var f = HDrop();
        return data.QueryGetData(ref f) == 0;
    }

    static string[] Paths(IDataObject data)
    {
        var f = HDrop();
        if (data.QueryGetData(ref f) != 0) return [];
        data.GetData(ref f, out var medium);
        try
        {
            var count = DragQueryFileW(medium.unionmember, 0xFFFFFFFF, null, 0);
            var list = new List<string>();
            for (uint i = 0; i < count; i++)
            {
                var sb = new StringBuilder(1024);
                DragQueryFileW(medium.unionmember, i, sb, (uint)sb.Capacity);
                list.Add(sb.ToString());
            }
            return [.. list];
        }
        finally { ReleaseStgMedium(ref medium); }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool ScreenToClient(IntPtr hwnd, ref POINT pt);
    [DllImport("ole32.dll")] static extern int OleInitialize(IntPtr reserved);
    [DllImport("ole32.dll")] static extern int RegisterDragDrop(IntPtr hwnd, IDropTarget target);
    [DllImport("ole32.dll")] static extern int RevokeDragDrop(IntPtr hwnd);
    [DllImport("ole32.dll")] static extern void ReleaseStgMedium(ref STGMEDIUM medium);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern uint DragQueryFileW(IntPtr hDrop, uint index, StringBuilder? file, uint size);
}

[ComImport, Guid("00000122-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IDropTarget
{
    [PreserveSig] int DragEnter(IDataObject data, int keys, DropTarget.POINT pt, ref int effect);
    [PreserveSig] int DragOver(int keys, DropTarget.POINT pt, ref int effect);
    [PreserveSig] int DragLeave();
    [PreserveSig] int Drop(IDataObject data, int keys, DropTarget.POINT pt, ref int effect);
}
