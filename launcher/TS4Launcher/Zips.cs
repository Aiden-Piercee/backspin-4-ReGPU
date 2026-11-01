using System.IO.Compression;

namespace TS4Launcher;

/// <summary>
/// A dropped .zip: looks inside to see what it holds and installs it for the game. default.xexp = a title update
/// (unpacked to user\update); default.xex = the extracted game folder (unpacked to the game folder); one .iso =
/// the disc image (imported); an Xbox 360 package (STFS) = a title update package.
/// </summary>
public static class Zips
{
    /// <summary>Returns what it was: "update", "folder" or "iso".</summary>
    public static async Task<string> Install(string zipPath, Action<int, string> progress)
    {
        using var z = ZipFile.OpenRead(zipPath);
        var files = z.Entries.Where(e => e.Name.Length > 0).ToList();

        ZipArchiveEntry? Shortest(Func<ZipArchiveEntry, bool> f) => files.Where(f).OrderBy(e => e.FullName.Length).FirstOrDefault();
        static string Dir(ZipArchiveEntry e) => e.FullName[..(e.FullName.Length - e.Name.Length)];

        var xexp = Shortest(e => e.Name.Equals("default.xexp", StringComparison.OrdinalIgnoreCase));
        if (xexp != null)
        {
            var tmp = Paths.UpdateDir + ".new";
            if (Directory.Exists(tmp)) Directory.Delete(tmp, true);
            ExtractUnder(z, Dir(xexp), tmp);
            if (Directory.Exists(Paths.UpdateDir)) Directory.Delete(Paths.UpdateDir, true);
            Directory.Move(tmp, Paths.UpdateDir);
            return "update";
        }

        var xex = Shortest(e => e.Name.Equals("default.xex", StringComparison.OrdinalIgnoreCase));
        if (xex != null)
        {
            var dest = Game.GameDir;
            ExtractUnder(z, Dir(xex), dest);
            Game.SetGameDir(dest);
            return "folder";
        }

        var iso = Shortest(e => e.Name.EndsWith(".iso", StringComparison.OrdinalIgnoreCase));
        if (iso != null)
        {
            var tmp = Path.Combine(Paths.UserDir, "tmp");
            Directory.CreateDirectory(tmp);
            var file = Path.Combine(tmp, "import.iso");
            try
            {
                iso.ExtractToFile(file, true);
                await Game.ImportIso(file, Game.GameDir, progress);
            }
            finally { try { File.Delete(file); } catch { } }
            return "iso";
        }

        foreach (var e in files.OrderBy(e => e.Length))
        {
            if (e.Length < 0x2000) continue;
            var tmp = Path.Combine(Paths.UserDir, "tmp");
            Directory.CreateDirectory(tmp);
            var file = Path.Combine(tmp, "package.bin");
            try
            {
                e.ExtractToFile(file, true);
                if (!Stfs.IsPackage(file)) continue;
                return Stfs.Install(file, e.Name);
            }
            finally { try { File.Delete(file); } catch { } }
        }

        throw new InvalidOperationException("That zip has no title update (default.xexp or an update package), no game folder (default.xex) and no .iso.");
    }

    /// <summary>Installs every Xbox 360 package inside a zip as DLC; returns how many.</summary>
    public static int InstallDlc(string zipPath)
    {
        using var z = ZipFile.OpenRead(zipPath);
        int n = 0;
        var tmp = Path.Combine(Paths.UserDir, "tmp");
        Directory.CreateDirectory(tmp);
        foreach (var e in z.Entries.Where(e => e.Name.Length > 0 && e.Length >= 0x2000))
        {
            var file = Path.Combine(tmp, "dlc.bin");
            try
            {
                e.ExtractToFile(file, true);
                if (!Stfs.IsPackage(file)) continue;
                Stfs.InstallDlc(file, e.Name);
                n++;
            }
            finally { try { File.Delete(file); } catch { } }
        }
        return n;
    }

    /// <summary>Unpacks the entries under <paramref name="prefix"/> into dest, without the prefix; names are checked to stay inside dest.</summary>
    static void ExtractUnder(ZipArchive z, string prefix, string dest)
    {
        Directory.CreateDirectory(dest);
        var root = Path.GetFullPath(dest) + Path.DirectorySeparatorChar;
        foreach (var e in z.Entries)
        {
            if (!e.FullName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
            var rel = e.FullName[prefix.Length..].Replace('/', Path.DirectorySeparatorChar);
            if (rel.Length == 0) continue;
            var target = Path.GetFullPath(Path.Combine(dest, rel));
            if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) continue;
            if (e.Name.Length == 0) { Directory.CreateDirectory(target); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            e.ExtractToFile(target, true);
        }
    }
}
