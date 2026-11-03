// A deliberately tiny stand-in for EML, used only in transactional installer tests.
using System;
using System.IO;
internal static class FakeEmm {
    private static int Main(string[] args) {
        int index = Array.IndexOf(args, "-g");
        if (index < 0 || index + 1 >= args.Length) return 2;
        string root = args[index + 1];
        string kfc = Path.Combine(root, "enshrouded.kfc");
        if (!File.Exists(kfc + ".bak")) File.Copy(kfc, kfc + ".bak");
        File.AppendAllText(kfc, "-patched");
        File.AppendAllText(Path.Combine(root, "enshrouded.kfc_resources"), "-patched");
        Directory.CreateDirectory(Path.Combine(root, ".cache"));
        File.WriteAllText(Path.Combine(root, ".cache", "files.json"), "new cache");
        if (File.Exists(Path.Combine(root, "write-container-test"))) {
            File.WriteAllText(Path.Combine(root, "enshrouded_001.dat"), "other mod overwritten by patch");
            File.WriteAllText(Path.Combine(root, "enshrouded_002.dat"), "new extra container");
        }
        if (File.Exists(Path.Combine(root, "fail-patch-test"))) return 23;
        Console.WriteLine("Fake EML patch succeeded (test only).");
        return 0;
    }
}
