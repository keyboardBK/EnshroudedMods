using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("SpeedMod 설치")]
[assembly: AssemblyProduct("SpeedMod Installer")]
[assembly: AssemblyCompany("KeyboardBK")]
[assembly: AssemblyVersion("1.1.0.0")]
[assembly: AssemblyFileVersion("1.1.0.0")]

namespace SpeedModSetup {
    internal static class Program {
        internal const string Version = "0.2.1-alpha";
        [STAThread]
        private static int Main(string[] args) {
            try {
                if (args.Length >= 2 && args[0] == "--extract") {
                    string destination = Path.GetFullPath(args[1]);
                    if (Directory.Exists(destination) && Directory.GetFileSystemEntries(destination).Length != 0)
                        throw new IOException("압축 해제 대상은 빈 폴더여야 합니다.");
                    Payload.Extract(destination);
                    return 0;
                }
                string game = Argument(args, "--game") ?? Discovery.FindGame();
                if (Array.IndexOf(args, "--check") >= 0 || Array.IndexOf(args, "--install") >= 0 || Array.IndexOf(args, "--remove") >= 0) {
                    if (String.IsNullOrWhiteSpace(game)) throw new ArgumentException("--game 폴더를 지정해 주세요.");
                    if (Array.IndexOf(args, "--remove") >= 0 && (Array.IndexOf(args, "--check") >= 0 || Array.IndexOf(args, "--install") >= 0))
                        throw new ArgumentException("제거 옵션은 설치·검사와 함께 사용할 수 없습니다.");
                    var result = Runner.Run(game, Array.IndexOf(args, "--check") >= 0,
                        Array.IndexOf(args, "--offline") >= 0, Argument(args, "--dependencies"), Console.WriteLine,
                        Array.IndexOf(args, "--remove") >= 0, Array.IndexOf(args, "--items-restored") >= 0);
                    string report = Argument(args, "--report");
                    if (report != null) File.WriteAllText(Path.GetFullPath(report), result.Json, new UTF8Encoding(false));
                    Console.WriteLine(result.Json);
                    return result.ExitCode;
                }
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                string removalPreview = Argument(args, "--preview-remove-png");
                Form form = removalPreview != null ? (Form)new RemovalDialog() : new SetupForm(game);
                string preview = removalPreview ?? Argument(args, "--preview-png");
                if (preview != null) {
                    form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual; form.Location = new Point(-32000, -32000);
                    form.Shown += delegate {
                        var timer = new Timer { Interval = 500 };
                        timer.Tick += delegate {
                            timer.Stop(); timer.Dispose();
                            using (var bitmap = new Bitmap(form.Width, form.Height)) {
                                form.DrawToBitmap(bitmap, new Rectangle(0, 0, form.Width, form.Height));
                                bitmap.Save(Path.GetFullPath(preview), System.Drawing.Imaging.ImageFormat.Png);
                            }
                            form.Close();
                        };
                        timer.Start();
                    };
                }
                Application.Run(form);
                return 0;
            } catch (Exception ex) {
                if (args.Length == 0 || (args.Length > 0 && args[0] == "--game" && args.Length == 2))
                    MessageBox.Show(ex.Message, "SpeedMod 설치", MessageBoxButtons.OK, MessageBoxIcon.Error);
                else Console.Error.WriteLine(ex.Message);
                return 1;
            }
        }
        private static string Argument(string[] args, string name) {
            int at = Array.IndexOf(args, name);
            return at >= 0 && at + 1 < args.Length ? args[at + 1] : null;
        }
        internal static string Quote(string value) {
            var s = new StringBuilder("\""); int slashes = 0;
            foreach (char c in value) {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') s.Append('\\', slashes * 2 + 1).Append(c);
                else s.Append('\\', slashes).Append(c);
                slashes = 0;
            }
            return s.Append('\\', slashes * 2).Append('"').ToString();
        }
    }

    internal static class Payload {
        internal static void Extract(string root) {
            root = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            Directory.CreateDirectory(root);
            using (Stream input = Assembly.GetExecutingAssembly().GetManifestResourceStream("SpeedMod.Payload.zip")) {
                if (input == null) throw new IOException("설치 데이터가 없습니다.");
                using (var archive = new ZipArchive(input, ZipArchiveMode.Read)) {
                    foreach (ZipArchiveEntry entry in archive.Entries) {
                        string relative = entry.FullName.Replace('/', Path.DirectorySeparatorChar);
                        string destination = Path.GetFullPath(Path.Combine(root, relative));
                        if (Path.IsPathRooted(relative) || !destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                            throw new IOException("잘못된 설치 경로입니다.");
                        if (entry.Name.Length == 0) { Directory.CreateDirectory(destination); continue; }
                        Directory.CreateDirectory(Path.GetDirectoryName(destination));
                        using (Stream source = entry.Open())
                        using (Stream target = new FileStream(destination, FileMode.CreateNew, FileAccess.Write)) source.CopyTo(target);
                    }
                }
            }
        }
    }

    internal sealed class InstallResult {
        internal int ExitCode;
        internal string Json;
        internal string LogPath;
        internal Dictionary<string, object> Values {
            get { return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(Json); }
        }
    }

    internal static class Runner {
        internal static InstallResult Run(string game, bool check, bool offline, string dependencies, Action<string> log, bool remove = false, bool restored = false) {
            string tempRoot = Path.GetFullPath(Path.GetTempPath());
            string stage = Path.Combine(tempRoot, "SpeedModSetup-" + Guid.NewGuid().ToString("N"));
            string logPath = stage + ".log";
            var output = new StringBuilder();
            try {
                Payload.Extract(stage);
                string report = Path.Combine(stage, "result.json");
                string script = Path.Combine(stage, "backend", "Install-SpeedMod.ps1");
                string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),
                    "System32", "WindowsPowerShell", "v1.0", "powershell.exe");
                var start = new ProcessStartInfo(powershell);
                start.Arguments = "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + Program.Quote(script) +
                    " -Game " + Program.Quote(Path.GetFullPath(game)) + " -Package " + Program.Quote(stage) + " -Report " + Program.Quote(report);
                if (check) start.Arguments += " -CheckOnly";
                if (remove) start.Arguments += " -Remove";
                if (restored) start.Arguments += " -ItemsRestored";
                if (offline) start.Arguments += " -Offline";
                if (!String.IsNullOrEmpty(dependencies)) start.Arguments += " -DependencyDirectory " + Program.Quote(dependencies);
                start.WorkingDirectory = stage; start.UseShellExecute = false; start.CreateNoWindow = true;
                start.RedirectStandardOutput = true; start.RedirectStandardError = true;
                start.StandardOutputEncoding = Encoding.UTF8; start.StandardErrorEncoding = Encoding.UTF8;
                using (var process = new Process()) {
                    process.StartInfo = start;
                    DataReceivedEventHandler received = delegate(object sender, DataReceivedEventArgs e) {
                        if (e.Data == null) return;
                        lock (output) output.AppendLine(e.Data);
                        if (log != null) log(e.Data);
                    };
                    process.OutputDataReceived += received; process.ErrorDataReceived += received;
                    process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine(); process.WaitForExit();
                    string json = File.Exists(report) ? File.ReadAllText(report, Encoding.UTF8) :
                        new JavaScriptSerializer().Serialize(new { success = false, error = "설치 작업이 완료되지 않았습니다. 로그: " + logPath });
                    return new InstallResult { ExitCode = File.Exists(report) ? process.ExitCode : 1, Json = json, LogPath = logPath };
                }
            } finally {
                File.WriteAllText(logPath, output.ToString(), new UTF8Encoding(false));
                // Only this invocation's private temporary directory may be recursively removed.
                string resolved = Path.GetFullPath(stage);
                if (resolved.StartsWith(tempRoot.TrimEnd('\\') + "\\SpeedModSetup-", StringComparison.OrdinalIgnoreCase) && Directory.Exists(resolved)) {
                    try { Directory.Delete(resolved, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
                }
            }
        }
    }

    internal static class Discovery {
        internal static string FindGame() {
            string beside = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            if (File.Exists(Path.Combine(beside, "enshrouded.exe"))) return beside;
            var steamRoots = new List<string>();
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")) {
                if (key != null && key.GetValue("SteamPath") != null) steamRoots.Add(key.GetValue("SteamPath").ToString().Replace('/', '\\'));
            }
            steamRoots.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"));
            foreach (string steam in steamRoots) {
                var libraries = new List<string> { steam };
                string file = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
                if (File.Exists(file)) {
                    foreach (Match m in Regex.Matches(File.ReadAllText(file), "\\\"path\\\"\\s*\\\"([^\\\"]+)\\\""))
                        libraries.Add(m.Groups[1].Value.Replace("\\\\", "\\"));
                }
                foreach (string library in libraries) {
                    string candidate = Path.Combine(library, "steamapps", "common", "Enshrouded");
                    if (File.Exists(Path.Combine(candidate, "enshrouded.exe"))) return candidate;
                }
            }
            return "";
        }
    }

    internal sealed class SetupForm : Form {
        private readonly TextBox folder = new TextBox();
        private readonly TextBox details = new TextBox();
        private readonly Label status = new Label();
        private readonly ProgressBar progress = new ProgressBar();
        private readonly Button install = new Button();
        private readonly Button remove = new Button();
        private readonly Button browse = new Button();
        private readonly Button play = new Button();
        private readonly Button elevate = new Button();
        private bool busy;
        internal SetupForm(string game) {
            Text = "SpeedMod 설치 / 제거 · " + Program.Version;
            Font = new Font("맑은 고딕", 9F); StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(760, 440); MinimumSize = new Size(780, 480);
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(20), ColumnCount = 1, RowCount = 8 };
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 22));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            layout.Controls.Add(new Label { Text = "신발 랜덤 질주속도 모드", Font = new Font(Font.FontFamily, 16, FontStyle.Bold), AutoSize = true }, 0, 0);
            layout.Controls.Add(new Label { Text = "신발 레벨과 등급에 따라 추가 질주속도를 부여하는 개발 버전입니다.\n설정과 저장 파일을 백업하고, 필요한 구성요소를 자동으로 설치합니다.", Dock = DockStyle.Fill }, 0, 1);
            layout.Controls.Add(new Label { Text = "Enshrouded 설치 폴더  ·  지원 게임 빌드 1283531", Dock = DockStyle.Fill }, 0, 2);
            var pathRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2 };
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 84));
            folder.Text = game ?? ""; folder.Dock = DockStyle.Fill;
            browse.Text = "찾아보기"; browse.Dock = DockStyle.Fill;
            browse.Click += delegate { using (var dialog = new FolderBrowserDialog { Description = "enshrouded.exe가 있는 폴더를 선택하세요.", SelectedPath = folder.Text }) if (dialog.ShowDialog(this) == DialogResult.OK) folder.Text = dialog.SelectedPath; };
            pathRow.Controls.Add(folder, 0, 0); pathRow.Controls.Add(browse, 1, 0); layout.Controls.Add(pathRow, 0, 3);
            status.Text = "게임을 종료한 뒤 설치해 주세요. 처음 설치할 때 인터넷 연결이 필요합니다.";
            status.Dock = DockStyle.Fill; layout.Controls.Add(status, 0, 4);
            progress.Dock = DockStyle.Fill; layout.Controls.Add(progress, 0, 5);
            details.Multiline = true; details.ReadOnly = true; details.ScrollBars = ScrollBars.Vertical; details.Dock = DockStyle.Fill;
            details.BackColor = SystemColors.Window; layout.Controls.Add(details, 0, 6);
            var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
            install.Text = "설치 / 업데이트"; install.AutoSize = true; install.Click += InstallClicked;
            remove.Text = "SpeedMod만 제거"; remove.AutoSize = true; remove.Click += RemoveClicked;
            play.Text = "게임 실행"; play.AutoSize = true; play.Enabled = false;
            play.Click += delegate { Process.Start(new ProcessStartInfo(Path.Combine(folder.Text, "enshrouded.exe")) { WorkingDirectory = folder.Text, UseShellExecute = true }); };
            elevate.Text = "관리자 권한으로 다시 실행"; elevate.AutoSize = true; elevate.Visible = false;
            elevate.Click += delegate {
                try { Process.Start(new ProcessStartInfo(Assembly.GetExecutingAssembly().Location, "--game " + Program.Quote(folder.Text)) { UseShellExecute = true, Verb = "runas" }); Close(); }
                catch (System.ComponentModel.Win32Exception ex) { status.Text = ex.Message; }
            };
            actions.Controls.Add(install); actions.Controls.Add(remove); actions.Controls.Add(play); actions.Controls.Add(elevate); layout.Controls.Add(actions, 0, 7);
            Controls.Add(layout);
            FormClosing += delegate(object sender, FormClosingEventArgs e) {
                if (busy) { e.Cancel = true; status.Text = "설치가 진행 중입니다. 완료되면 창을 닫아 주세요."; }
            };
        }
        private void Receive(string line) {
            if (IsDisposed || !IsHandleCreated) return;
            BeginInvoke((Action)delegate {
                string clean = Regex.Replace(line, "\u001b\\[[0-9;]*m", "");
                Match m = Regex.Match(clean, "^@@progress:([0-9]+)\\|(.*)$");
                if (m.Success) { progress.Value = Math.Max(0, Math.Min(100, Int32.Parse(m.Groups[1].Value))); status.Text = m.Groups[2].Value; }
                else if (clean.Length > 0) details.AppendText(clean + Environment.NewLine);
            });
        }
        private async void InstallClicked(object sender, EventArgs e) {
            await RunOperation(false);
        }
        private async void RemoveClicked(object sender, EventArgs e) {
            if (String.IsNullOrWhiteSpace(folder.Text)) { status.Text = "게임 설치 폴더를 선택해 주세요."; return; }
            using (var dialog = new RemovalDialog()) {
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
            }
            await RunOperation(true);
        }
        private async Task RunOperation(bool removing) {
            if (String.IsNullOrWhiteSpace(folder.Text)) { status.Text = "게임 설치 폴더를 선택해 주세요."; return; }
            busy = true; install.Enabled = false; remove.Enabled = false; browse.Enabled = false; folder.ReadOnly = true; play.Enabled = false; elevate.Visible = false;
            details.Clear(); progress.Value = 0;
            try {
                string selected = folder.Text;
                InstallResult result = await Task.Run(() => Runner.Run(selected, false, false, null, Receive, removing, removing));
                var values = result.Values;
                if (result.ExitCode == 0 && values.ContainsKey("success") && Object.Equals(values["success"], true)) {
                    progress.Value = 100; status.Text = removing ? "SpeedMod만 제거했습니다. EML·EMF와 다른 모드는 유지했습니다." : "설치가 완료됐습니다. 첫 실행은 패치 확인으로 시간이 걸릴 수 있습니다."; play.Enabled = true;
                    if (values.ContainsKey("backup")) details.AppendText("백업: " + values["backup"] + Environment.NewLine);
                    if (!removing) details.AppendText("제거 전에는 원본 신발로 복구·저장해야 합니다. 'SpeedMod만 제거'에서 절차를 확인하세요." + Environment.NewLine);
                } else {
                    status.Text = removing ? "제거하지 못했습니다. 아래 내용을 확인해 주세요." : "설치하지 못했습니다. 아래 내용을 확인해 주세요.";
                    if (values.ContainsKey("error")) details.AppendText(values["error"] + Environment.NewLine);
                    details.AppendText("설치 로그: " + result.LogPath + Environment.NewLine);
                    elevate.Visible = values.ContainsKey("needs_elevation") && Object.Equals(values["needs_elevation"], true);
                }
            } catch (Exception ex) { status.Text = removing ? "제거하지 못했습니다." : "설치하지 못했습니다."; details.AppendText(ex.Message + Environment.NewLine); }
            finally { busy = false; install.Enabled = true; remove.Enabled = true; browse.Enabled = true; folder.ReadOnly = false; }
        }
    }

    internal sealed class RemovalDialog : Form {
        internal RemovalDialog() {
            Text = "SpeedMod 제거 전 신발 복구"; Font = new Font("맑은 고딕", 9F);
            ClientSize = new Size(650, 430); FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent; MaximizeBox = false; MinimizeBox = false;
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(20), ColumnCount = 1, RowCount = 4 };
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
            layout.Controls.Add(new Label { Dock = DockStyle.Fill, Text =
                "1. 모드를 유지한 상태로 호스트 세션을 실행합니다.\n" +
                "2. EMF 콘솔에서 아래 명령을 실행하고 복구 기록을 확인합니다.\n" +
                "3. 모든 캐릭터·월드·창고를 복구하고 저장한 뒤 게임을 종료합니다.\n\n" +
                "복구 명령은 현재 로드된 인벤토리만 처리합니다.\n오프라인 캐릭터·미로드 창고는 자동 복구되지 않습니다.\n재시작하면 신규 추첨이 다시 켜지므로 복구 명령을 다시 실행하세요.\n\n" +
                "복구하지 않은 신발은 데이터 제거 후 유실될 수 있습니다.\nEML·EMF와 다른 모드는 유지하고, 나머지 EML 모드를 다시 적용합니다." }, 0, 0);
            layout.Controls.Add(new TextBox { Text = "speedmod_boots.restore", ReadOnly = true, Dock = DockStyle.Fill }, 0, 1);
            var restored = new CheckBox { Text = "모든 보관 위치의 신발을 원본으로 복구·저장했고 게임을 종료했습니다.", Dock = DockStyle.Fill };
            layout.Controls.Add(restored, 0, 2);
            var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
            var cancel = new Button { Text = "취소", DialogResult = DialogResult.Cancel, AutoSize = true };
            var confirm = new Button { Text = "SpeedMod만 제거", DialogResult = DialogResult.OK, Enabled = false, AutoSize = true };
            restored.CheckedChanged += delegate { confirm.Enabled = restored.Checked; };
            actions.Controls.Add(cancel); actions.Controls.Add(confirm); layout.Controls.Add(actions, 0, 3);
            CancelButton = cancel; Controls.Add(layout);
        }
    }
}
