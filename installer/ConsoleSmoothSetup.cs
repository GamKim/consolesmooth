// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;

[assembly: AssemblyTitle("ConsoleSmooth Frame Generation")]
[assembly: AssemblyProduct("ConsoleSmooth")]
[assembly: AssemblyDescription("Frame generation plugin for Streaming Center")]
[assembly: AssemblyVersion("1.1.2.0")]
[assembly: AssemblyFileVersion("1.1.2.0")]
[assembly: AssemblyInformationalVersion("1.1.2")]

static class Setup {
 // 1.9.3: English unless Streaming Center runs in Korean (HKCU\Software\AVerMedia\StreamingCenter\Global AppLanguage);
 // without that value the Windows UI language decides.
 static bool? english;
 static string T(string ko,string en){
  if(english==null){bool? korean=null;
   try{using(var k=Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\AVerMedia\StreamingCenter\Global")){var v=k==null?null:k.GetValue("AppLanguage") as string;if(!string.IsNullOrEmpty(v))korean=v.StartsWith("ko",StringComparison.OrdinalIgnoreCase);}}catch{}
   if(korean==null)korean=System.Globalization.CultureInfo.CurrentUICulture.TwoLetterISOLanguageName=="ko";
   english=!korean.Value;}
  return english.Value?en:ko;}
 const string ReleaseVersion="1.1.2"; /* Build-Installer.ps1 stamps $version here and in the assembly attributes */
 const string Product="ConsoleSmoothNative-"+ReleaseVersion, HostHash="CADCCC27D66F3350BC092E29BBAD88582F4110E1EDF87193342C183D3CB15CC1";
 static string Self=Assembly.GetExecutingAssembly().Location;
 static string Hash(string file){using(var sha=SHA256.Create())using(var s=File.OpenRead(file))return BitConverter.ToString(sha.ComputeHash(s)).Replace("-","");}
 static string Child(string root,string relative){
  if(Path.IsPathRooted(relative))throw new InvalidDataException(T("절대 경로가 포함되어 있습니다.","Contains an absolute path."));
  string r=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
  string p=Path.GetFullPath(Path.Combine(r,relative));
  if(!p.StartsWith(r,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException(T("설치 폴더 밖의 경로입니다.","Path is outside the install folder."));
  return p;
 }
 static void NoLinks(string p){
  for(string q=Path.GetFullPath(p);!String.IsNullOrEmpty(q);q=Path.GetDirectoryName(q))
   if((File.Exists(q)||Directory.Exists(q))&&(File.GetAttributes(q)&FileAttributes.ReparsePoint)!=0)throw new IOException(T("링크 폴더에는 설치하지 않습니다: ","Will not install into a linked folder: ")+q);
 }
 static string Root(string p){p=Path.GetFullPath(p).TrimEnd(Path.DirectorySeparatorChar);if(p.Length<8||p==Path.GetPathRoot(p).TrimEnd(Path.DirectorySeparatorChar))throw new IOException(T("설치 폴더가 올바르지 않습니다.","The install folder is not valid."));NoLinks(p);return p;}
 static string FindSource(){foreach(var baseDir in new[]{Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)}){string p=Path.Combine(baseDir,"AVerMedia","AVerMedia Streaming Center");if(File.Exists(Path.Combine(p,"StreamingCenter.exe")))return p;}return "";}
 // 1.8.2 (11-09 user decision): other Streaming Center versions are allowed too. 1.8.22.2 (SHA-256 above) is the tested
 // build; any other StreamingCenter.exe must carry a valid Authenticode signature from AVerMedia. The plugin checks the
 // same rule when it loads (native_guard.h).
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] class TrustFile{public uint cbStruct=(uint)Marshal.SizeOf(typeof(TrustFile));public string path;public IntPtr file=IntPtr.Zero,subject=IntPtr.Zero;}
 [StructLayout(LayoutKind.Sequential)] class TrustData{public uint cbStruct=(uint)Marshal.SizeOf(typeof(TrustData));public IntPtr policy=IntPtr.Zero,sip=IntPtr.Zero;public uint ui=2,revocation=0,union=1;public IntPtr file;public uint state=0;public IntPtr stateData=IntPtr.Zero,url=IntPtr.Zero;public uint provFlags=0x1000,context=0;public IntPtr settings=IntPtr.Zero;}
 [DllImport("wintrust.dll",ExactSpelling=true,CharSet=CharSet.Unicode)] static extern int WinVerifyTrust(IntPtr hwnd,[MarshalAs(UnmanagedType.LPStruct)] Guid action,IntPtr data);
 static bool SignedByAVerMedia(string exe){
  IntPtr filePtr=IntPtr.Zero,dataPtr=IntPtr.Zero;var fi=new TrustFile{path=exe};var wd=new TrustData();
  try{filePtr=Marshal.AllocHGlobal(Marshal.SizeOf(fi));Marshal.StructureToPtr(fi,filePtr,false);wd.file=filePtr;dataPtr=Marshal.AllocHGlobal(Marshal.SizeOf(wd));Marshal.StructureToPtr(wd,dataPtr,false);
   if(WinVerifyTrust(new IntPtr(-1),new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE"),dataPtr)!=0)return false;
   var cert=new X509Certificate2(X509Certificate.CreateFromSignedFile(exe));return cert.GetNameInfo(X509NameType.SimpleName,false).StartsWith("AVerMedia",StringComparison.OrdinalIgnoreCase);}
  catch{return false;}
  finally{if(dataPtr!=IntPtr.Zero){Marshal.DestroyStructure(dataPtr,typeof(TrustData));Marshal.FreeHGlobal(dataPtr);}if(filePtr!=IntPtr.Zero){Marshal.DestroyStructure(filePtr,typeof(TrustFile));Marshal.FreeHGlobal(filePtr);}}
 }
 static bool Tested(string source){string exe=Path.Combine(source,"StreamingCenter.exe");return File.Exists(exe)&&Hash(exe)==HostHash;}
 static void CheckSource(string source){NoLinks(source);string exe=Path.Combine(source,"StreamingCenter.exe");if(!File.Exists(exe)||(Hash(exe)!=HostHash&&!SignedByAVerMedia(exe)))throw new IOException(T("공식 AVerMedia Streaming Center 설치 폴더가 필요합니다. 공식 앱을 먼저 설치하세요.","The official AVerMedia Streaming Center install folder is required. Install the official app first."));}
 // 1.0.1: 1.9.19.1 (StreamingCenter.exe SHA-256 below) checked 11-12 with a GC573.
 const string Host19191Hash="EF88FE1D8FA6C43C13B887E7290E415541DBE24962B7F88D60DAB280C5F48C5D";
 static string HostLabel(string source){try{if(source.Length==0||Tested(source))return "AVerMedia Streaming Center 1.8.22.2";string exe=Path.Combine(source,"StreamingCenter.exe");if(File.Exists(exe)&&Hash(exe)==Host19191Hash)return "AVerMedia Streaming Center 1.9.19.1";return T("AVerMedia Streaming Center · 확인하지 않은 버전","AVerMedia Streaming Center · untested version");}catch{return "AVerMedia Streaming Center";}}
 static void Idle(string source){
  string host=Path.Combine(Root(source),"StreamingCenter.exe");
  foreach(var process in Process.GetProcessesByName("StreamingCenter"))using(process){
   string executable;try{executable=process.MainModule.FileName;}catch{throw new IOException(T("실행 중인 Streaming Center의 경로를 확인할 수 없습니다. 앱을 닫아주세요.","Cannot read the path of the running Streaming Center. Please close the app."));}
   if(String.Equals(Path.GetFullPath(executable),host,StringComparison.OrdinalIgnoreCase))throw new IOException(T("설치 또는 제거 전에 Streaming Center를 닫아주세요. ","Close Streaming Center before installing or uninstalling. "));
  }
 }
 static string ResourceText(string name){using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream(name))using(var r=new StreamReader(s,Encoding.UTF8))return r.ReadToEnd();}
 static Dictionary<string,string> Manifest(){return ResourceText("payload.sha256").Split(new[]{'\r','\n'},StringSplitOptions.RemoveEmptyEntries).ToDictionary(s=>s.Substring(65),s=>s.Substring(0,64),StringComparer.OrdinalIgnoreCase);}
 static void Unpack(string root){
  using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))using(var zip=new ZipArchive(s,ZipArchiveMode.Read)){
   var manifest=Manifest();
   foreach(var entry in zip.Entries){if(entry.FullName.EndsWith("/"))continue;if(!manifest.ContainsKey(entry.FullName))throw new InvalidDataException(T("알 수 없는 설치 파일","Unknown install file"));string p=Child(root,entry.FullName);NoLinks(p);Directory.CreateDirectory(Path.GetDirectoryName(p));using(var a=entry.Open())using(var b=File.Create(p))a.CopyTo(b);if(Hash(p)!=manifest[entry.FullName])throw new InvalidDataException(T("설치 파일 검증 실패: ","Install file check failed: ")+entry.FullName);}
   foreach(var pair in manifest)if(!File.Exists(Child(root,pair.Key)))throw new InvalidDataException(T("설치 파일 누락: ","Install file missing: ")+pair.Key);
  }
 }
 static void Verify(string root){
  root=Root(root);if(!File.Exists(Child(root,"product.txt"))||File.ReadAllText(Child(root,"product.txt"))!=Product)throw new IOException(T("이 설치 프로그램으로 만든 폴더가 아닙니다.","This folder was not made by this installer."));
  CheckSource(File.ReadAllText(Child(root,"source.txt"),Encoding.UTF8));
  foreach(var entry in Manifest()){string p=Child(root,entry.Key);NoLinks(p);if(!File.Exists(p)||Hash(p)!=entry.Value)throw new IOException(T("파일 검증 실패. 원본 실행을 이용하거나 제거 후 다시 설치하세요: ","File check failed. Use the original app or uninstall and install again: ")+entry.Key);}
 }
 static void SelfTest(){
  string root=Path.Combine(Path.GetTempPath(),"ConsoleSmooth-boundary");bool rejected=false;try{Child(root,"../outside");}catch(InvalidDataException){rejected=true;}if(!rejected)throw new Exception("Traversal guard failed");
  var names=Manifest();using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))using(var z=new ZipArchive(s,ZipArchiveMode.Read))foreach(var e in z.Entries){if(e.FullName.EndsWith("/"))continue;Child(root,e.FullName);using(var sha=SHA256.Create())using(var input=e.Open()){string h=BitConverter.ToString(sha.ComputeHash(input)).Replace("-","");if(!names.ContainsKey(e.FullName)||names[e.FullName]!=h)throw new Exception("Payload integrity failed");}}
 }

 static string NativeRoot(string source){return Child(Root(source),"Plugin/ConsoleSmooth");}
 // Any folder this installer family created (product.txt "ConsoleSmoothNative-<version>") may be updated. 1.8.2: the list of
 // known versions stopped at 1.6.5, so updating over 1.6.6-1.8.1 was refused as T("다른 파일이 있는 플러그인 폴더","Plugin folder has other files").
 static bool Owned(string root){string p=Child(root,"product.txt");NoLinks(p);if(!File.Exists(p))return false;string t=File.ReadAllText(p,Encoding.UTF8).Trim('\uFEFF',' ','\r','\n');
  if(!t.StartsWith("ConsoleSmoothNative-",StringComparison.Ordinal))return false;string v=t.Substring(20);return v.Length>0&&v.All(c=>char.IsDigit(c)||c=='.');}
 static void CleanStage(string stage){
  NoLinks(stage);if(!Directory.Exists(stage))return;
  foreach(string f in Directory.GetFiles(stage,"*",SearchOption.AllDirectories)){NoLinks(f);File.Delete(f);}
  foreach(string d in Directory.GetDirectories(stage,"*",SearchOption.AllDirectories).OrderByDescending(s=>s.Length)){NoLinks(d);Directory.Delete(d,false);}
  Directory.Delete(stage,false);
 }
 static void Install(string source){
  source=Root(source);CheckSource(source);Idle(source);string root=NativeRoot(source);
  if(Directory.Exists(root)&&!Owned(root))throw new IOException(T("다른 파일이 있는 플러그인 폴더입니다. 덮어쓰지 않았습니다.","The plugin folder has other files. Nothing was overwritten."));
  var manifest=Manifest();var previous=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
  if(Owned(root)){
   foreach(string line in File.ReadAllLines(Child(root,"installed-files.tsv"),Encoding.UTF8)){
    if(line.Length<66)throw new InvalidDataException(T("잘못된 설치 기록","Invalid install record"));string rel=line.Substring(65);Child(root,rel);previous[rel]=line.Substring(0,64);
   }
  }
  foreach(var pair in manifest){string f=Child(root,pair.Key);NoLinks(f);string prior;
   if(File.Exists(f)&&(!previous.TryGetValue(pair.Key,out prior)||Hash(f)!=prior))throw new IOException(T("변경된 파일은 덮어쓰지 않습니다: ","Changed files are not overwritten: ")+pair.Key);
  }
  Directory.CreateDirectory(root);string stage=Child(root,".staging-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(stage);
  var committed=new List<string>();var retired=new List<string>();
  try{
   Unpack(stage);File.WriteAllText(Child(stage,"product.txt"),Product,Encoding.UTF8);File.WriteAllText(Child(stage,"source.txt"),source,Encoding.UTF8);
   File.WriteAllLines(Child(stage,"installed-files.tsv"),manifest.Select(k=>k.Value+"\t"+k.Key),Encoding.UTF8);
   var names=manifest.Keys.Concat(new[]{"product.txt","source.txt","installed-files.tsv"}).ToArray();
   foreach(string rel in names){string dest=Child(root,rel),incoming=Child(stage,rel);NoLinks(dest);Directory.CreateDirectory(Path.GetDirectoryName(dest));
    string backup=Child(stage,".backup/"+rel);Directory.CreateDirectory(Path.GetDirectoryName(backup));
    if(File.Exists(dest))File.Replace(incoming,dest,backup);else File.Move(incoming,dest);committed.Add(rel);
   }
   // Retire files an earlier version installed that this payload no longer ships
   // (RIFE 4.25 in 1.4.0, IFRNet/CAIN in 1.4.3), and only if still unmodified.
   foreach(string rel in previous.Keys.Where(k=>!manifest.ContainsKey(k)).ToArray()){
    string prior,dest=Child(root,rel);NoLinks(dest);if(!previous.TryGetValue(rel,out prior)||!File.Exists(dest)||Hash(dest)!=prior)continue;
    string backup=Child(stage,".retired/"+rel);Directory.CreateDirectory(Path.GetDirectoryName(backup));File.Move(dest,backup);retired.Add(rel);
   }
   Verify(root);
  }catch{
   foreach(string rel in retired.AsEnumerable().Reverse()){string backup=Child(stage,".retired/"+rel);if(File.Exists(backup))File.Move(backup,Child(root,rel));}
   foreach(string rel in committed.AsEnumerable().Reverse()){string dest=Child(root,rel),backup=Child(stage,".backup/"+rel);if(File.Exists(dest))File.Delete(dest);if(File.Exists(backup))File.Move(backup,dest);}
   throw;
  }finally{CleanStage(stage);}
  // Drop folders the retired files leave empty; anything the user added keeps its folder.
  foreach(string dir in retired.Select(rel=>Path.GetDirectoryName(Child(root,rel))).Distinct(StringComparer.OrdinalIgnoreCase).OrderByDescending(d=>d.Length))
   for(string d=dir;d.Length>root.Length&&Directory.Exists(d)&&!Directory.EnumerateFileSystemEntries(d).Any();d=Path.GetDirectoryName(d)){NoLinks(d);Directory.Delete(d,false);}
 }
 static int Remove(string root){
  root=Root(root);if(!Owned(root))throw new IOException(T("이 설치 프로그램의 플러그인 폴더가 아닙니다.","This is not this installer's plugin folder."));
  string source=File.ReadAllText(Child(root,"source.txt"),Encoding.UTF8);
  if(!String.Equals(root,NativeRoot(source),StringComparison.OrdinalIgnoreCase))throw new IOException(T("제거 경로 불일치","Uninstall path mismatch"));
  Idle(source);
  int kept=0;foreach(var pair in Manifest()){string p=Child(root,pair.Key);NoLinks(p);if(!File.Exists(p)||String.Equals(p,Self,StringComparison.OrdinalIgnoreCase))continue;if(Hash(p)!=pair.Value){kept++;continue;}File.Delete(p);}
  if(File.Exists(Child(root,"gstconsolesmoothprobe.dll")))throw new IOException(T("변경된 플러그인 DLL을 보존했습니다. 자동 로딩은 아직 제거되지 않았습니다.","The changed plugin DLL was kept. Auto-loading has not been removed yet."));
  return kept;
 }

 // 1.0.1: Streaming Center 1.9.19.1 (GStreamer 1.28) no longer enumerates device providers, so the plugin is started
 // through GStreamer's own tracer list instead: "consolesmooth" in the user's GST_TRACERS. Only Streaming Center's
 // GStreamer has the plugin; any other GStreamer program ignores the unknown name. Existing entries are kept.
 const string TracerName="consolesmooth";
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr h,int msg,IntPtr w,string l,int flags,int timeout,out IntPtr result);
 static void Broadcast(){IntPtr r;SendMessageTimeout((IntPtr)0xffff,0x001A,IntPtr.Zero,"Environment",2,3000,out r);}
 static void SetTracer(bool on){
  try{
   using(var k=Microsoft.Win32.Registry.CurrentUser.CreateSubKey("Environment")){
    string cur=(k.GetValue("GST_TRACERS",null,Microsoft.Win32.RegistryValueOptions.DoNotExpandEnvironmentNames) as string)??"";
    var parts=cur.Split(new[]{';'},StringSplitOptions.RemoveEmptyEntries).Select(x=>x.Trim()).Where(x=>x.Length>0).ToList();
    bool has=parts.Any(x=>String.Equals(x,TracerName,StringComparison.OrdinalIgnoreCase));
    if(on==has)return;
    if(on)parts.Add(TracerName);else parts.RemoveAll(x=>String.Equals(x,TracerName,StringComparison.OrdinalIgnoreCase));
    if(parts.Count==0)k.DeleteValue("GST_TRACERS",false);else k.SetValue("GST_TRACERS",String.Join(";",parts),Microsoft.Win32.RegistryValueKind.String);
   }
   Broadcast();
  }catch(Exception ex){if(on)throw new IOException(T("자동 시작 설정(GST_TRACERS)을 쓰지 못했습니다: ","Could not write the auto-start setting (GST_TRACERS): ")+ex.Message);}
 }
 static void Operation(string source){
  CheckSource(source);
  var identity=System.Security.Principal.WindowsIdentity.GetCurrent();
  bool admin=new System.Security.Principal.WindowsPrincipal(identity).IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);
  if(admin){Install(source);SetTracer(true);return;}
  var info=new ProcessStartInfo(Self){UseShellExecute=true,Verb="runas",Arguments="--install-source \""+Root(source)+"\""};
  using(var process=Process.Start(info)){process.WaitForExit();if(process.ExitCode!=0)throw new IOException(T("작업을 완료하지 못했습니다. 관리자 창의 오류를 확인하세요.","Could not finish. Check the error in the administrator window."));}
  SetTracer(true); // the signed-in user, not the elevated helper
 }
 [STAThread] static int Main(string[] args){
  try{
#if UNINSTALLER
   string ownRoot=Path.GetDirectoryName(Self);
   if(args.Length==1&&args[0]=="--remove"){Remove(ownRoot);SetTracer(false);return 0;}
   if(args.Length==1&&args[0]=="--test-remove"){Remove(ownRoot);return 0;}
   if(args.Length>0)throw new ArgumentException(T("지원하지 않는 실행 옵션","Unsupported option"));
   Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);ShowUninstall(ownRoot);return 0;
#else
   if(args.Length>0&&args[0]=="--self-test"){SelfTest();return 0;}
   if(args.Length==2&&(args[0]=="--test-install"||args[0]=="--install-source")){Install(args[1]);return 0;}
   if(args.Length==2&&args[0]=="--verify"){Verify(args[1]);return 0;}
   if(args.Length==2&&args[0]=="--test-remove"){Remove(NativeRoot(args[1]));return 0;}
   if(args.Length==1&&args[0]=="--preview-complete"){Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Show(true);return 0;}
   if(args.Length>0)throw new ArgumentException(T("지원하지 않는 실행 옵션","Unsupported option"));
   Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Show();return 0;
#endif
  }catch(Exception ex){if(args.Length>0&&!args[0].EndsWith("-source")){Console.Error.WriteLine(ex.ToString());return 1;}MessageBox.Show(ex.Message,"ConsoleSmooth",MessageBoxButtons.OK,MessageBoxIcon.Warning);return 1;}
 }
 static Color Bg=Color.FromArgb(38,38,38), Card=Color.FromArgb(45,45,45), Red=Color.FromArgb(230,0,45), Muted=Color.FromArgb(174,174,174);
 // Layout coordinates are pixels. Point fonts grew with Windows DPI while these
 // dynamically created labels kept their pixel bounds, clipping Korean glyphs.
 static Font UiFont(float points){return new Font("Malgun Gothic",points*96f/72f,FontStyle.Regular,GraphicsUnit.Pixel);}
 static Label LabelAt(string text,int x,int y,int w,int h,float size,Color color){return new Label{Text=text,Location=new Point(x,y),Size=new Size(w,h),Font=UiFont(size),ForeColor=color,BackColor=Color.Transparent};}
 static Button ButtonAt(string text,int x,int y,int w,bool accent){var b=new Button{Text=text,Location=new Point(x,y),Size=new Size(w,46),BackColor=accent?Red:Color.FromArgb(61,61,61),ForeColor=Color.White,FlatStyle=FlatStyle.Flat,Font=UiFont(11),Cursor=Cursors.Hand};b.FlatAppearance.BorderSize=0;b.FlatAppearance.MouseOverBackColor=accent?Color.FromArgb(248,20,65):Color.FromArgb(77,77,77);return b;}
 sealed class CardPanel:Panel{
  public CardPanel(){DoubleBuffered=true;BackColor=Bg;}
  protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;using(var p=new GraphicsPath()){float d=22,r=Width-1,b=Height-1;p.AddArc(0,0,d,d,180,90);p.AddArc(r-d,0,d,d,270,90);p.AddArc(r-d,b-d,d,d,0,90);p.AddArc(0,b-d,d,d,90,90);p.CloseFigure();using(var brush=new SolidBrush(Card))e.Graphics.FillPath(brush,p);}}
 }
 sealed class FrameMark:Control{
  public FrameMark(){Size=new Size(50,50);DoubleBuffered=true;}
  protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;float k=Math.Min(Width,Height)/50f;e.Graphics.ScaleTransform(k,k);using(var pen=new Pen(Red,3)){pen.LineJoin=LineJoin.Round;e.Graphics.DrawRectangle(pen,5,7,29,28);e.Graphics.DrawRectangle(pen,15,17,29,28);e.Graphics.DrawLines(pen,new[]{new Point(25,24),new Point(33,31),new Point(25,38)});}}
 }
 [DllImport("user32.dll")]static extern bool ReleaseCapture();
 [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,IntPtr l);
 static void Show(bool previewComplete=false){
  string source=FindSource();string root=source.Length>0?NativeRoot(source):Path.Combine(Path.GetTempPath(),"ConsoleSmooth-not-installed");
  bool installed=source.Length>0&&Owned(root)&&File.Exists(Child(root,"gstconsolesmoothprobe.dll"));
  var form=new Form{Text=T("ConsoleSmooth · 플러그인 설치","ConsoleSmooth · Install plugin"),ClientSize=new Size(900,660),StartPosition=FormStartPosition.CenterScreen,BackColor=Bg,ForeColor=Color.FromArgb(232,232,232),Font=UiFont(11),FormBorderStyle=FormBorderStyle.None,AutoScaleMode=AutoScaleMode.None,MinimumSize=new Size(900,660)};
  var header=new Panel{Location=new Point(1,1),Size=new Size(898,65),BackColor=Color.FromArgb(31,31,31)};
  var title=LabelAt(T("플러그인 설치","Install plugin"),26,16,650,36,18,Color.White);header.Controls.Add(title);
  MouseEventHandler drag=delegate(object sender,MouseEventArgs e){if(e.Button==MouseButtons.Left){ReleaseCapture();SendMessage(form.Handle,0xA1,new IntPtr(2),IntPtr.Zero);}};header.MouseDown+=drag;title.MouseDown+=drag;
  var xButton=ButtonAt("×",843,12,42,false);xButton.Size=new Size(42,40);xButton.BackColor=header.BackColor;xButton.AccessibleName=T("닫기","Close");xButton.Click+=delegate{form.Close();};header.Controls.Add(xButton);
  var nav=new Panel{Location=new Point(1,66),Size=new Size(219,593),BackColor=Color.FromArgb(31,31,31)};
  var selected=new Panel{Location=new Point(0,0),Size=new Size(219,64),BackColor=Color.FromArgb(61,61,61)};selected.Controls.Add(LabelAt(T("프레임 생성","Frame generation"),26,16,175,35,14,Color.White));nav.Controls.Add(selected);
  nav.Controls.Add(LabelAt("ConsoleSmooth",26,505,180,28,11,Muted));nav.Controls.Add(LabelAt(ReleaseVersion,26,535,180,26,10,Muted));
  var main=new Panel{Location=new Point(220,66),Size=new Size(679,593),BackColor=Bg};
  var top=new CardPanel{Location=new Point(22,20),Size=new Size(634,106)};
  top.Controls.Add(new FrameMark{Location=new Point(22,26)});top.Controls.Add(LabelAt(T("프레임 생성","Frame generation"),88,20,510,42,18,Color.White));top.Controls.Add(LabelAt(T("Streaming Center용 플러그인","Plugin for Streaming Center"),90,64,510,28,11,Muted));
  var appCard=new CardPanel{Location=new Point(22,142),Size=new Size(634,147)};
  appCard.Controls.Add(LabelAt(T("연결할 앱","App to connect"),24,16,450,30,15,Color.White));
  var found=LabelAt(HostLabel(source),24,55,450,28,12,Color.White);appCard.Controls.Add(found);
  var sourceLabel=LabelAt(source.Length>0?source:T("공식 앱을 먼저 설치하세요.","Install the official app first."),24,91,480,42,9,Muted);appCard.Controls.Add(sourceLabel);
  var browse=ButtonAt(T("변경","Change"),520,84,90,false);browse.Height=38;appCard.Controls.Add(browse);
  var config=new CardPanel{Location=new Point(22,305),Size=new Size(634,142)};
  config.Controls.Add(LabelAt(T("설치 구성","Components"),24,16,450,30,15,Color.White));config.Controls.Add(LabelAt(T("프레임 생성 모듈 · 모델 6종 · 라이선스","Frame generation module · 6 models · licenses"),24,55,580,28,11,Color.White));
  var destination=LabelAt(T("플러그인 위치: ","Plugin location: ")+(source.Length>0?root:T("공식 앱 선택 후 결정","Set after choosing the official app")),24,92,580,42,9,Muted);config.Controls.Add(destination);
  var status=LabelAt(T("기존 Streaming Center에 프레임 생성을 추가합니다.","Adds frame generation to your Streaming Center."),23,462,630,36,10,Muted);
  var warning=LabelAt(T("AVerMedia와 무관한 비공식 플러그인입니다.","Unofficial plugin, not affiliated with AVerMedia."),23,500,630,27,9,Muted);
  var install=ButtonAt(installed?T("업데이트","Update"):T("설치","Install"),282,538,200,true);
  var close=ButtonAt(T("닫기","Close"),498,538,158,false);close.Click+=delegate{form.Close();};
  browse.Click+=delegate{using(var picker=new FolderBrowserDialog{Description=T("공식 Streaming Center 설치 폴더","Official Streaming Center install folder")}){if(picker.ShowDialog(form)!=DialogResult.OK)return;try{CheckSource(picker.SelectedPath);source=picker.SelectedPath;root=NativeRoot(source);sourceLabel.Text=source;found.Text=HostLabel(source);destination.Text=T("플러그인 위치: ","Plugin location: ")+root;status.Text=T("설치할 앱을 선택했습니다.","App to install into is selected.");install.Text=Owned(root)?T("업데이트","Update"):T("설치","Install");}catch(Exception ex){status.Text=ex.Message;}}};
  bool busy=false,completed=false;form.FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy)e.Cancel=true;};
  Action finish=delegate{completed=true;title.Text=T("설치 완료","Installed");status.Text=T("기존 Streaming Center 아이콘으로 실행하세요.","Start it with your usual Streaming Center icon.");warning.Text=T("제거: 플러그인 폴더의 Uninstall.exe","Uninstall: Uninstall.exe in the plugin folder");install.Text=T("완료","Done");install.Bounds=close.Bounds;close.Visible=false;browse.Enabled=false;form.CancelButton=install;};
  install.Click+=async delegate{
   if(completed){form.Close();return;}
   busy=true;install.Enabled=browse.Enabled=close.Enabled=xButton.Enabled=false;status.Text=T("설치 중…","Installing…");
   try{string selectedSource=source;await Task.Run(()=>Operation(selectedSource));root=NativeRoot(selectedSource);finish();}
   catch(Exception ex){status.Text=ex.Message;}
   finally{busy=false;install.Enabled=close.Enabled=xButton.Enabled=true;browse.Enabled=!completed;}
  };
  main.Controls.AddRange(new Control[]{top,appCard,config,status,warning,install,close});form.Controls.AddRange(new Control[]{header,nav,main});
  form.Paint+=delegate(object sender,PaintEventArgs e){using(var pen=new Pen(Color.FromArgb(126,126,126)))e.Graphics.DrawRectangle(pen,0,0,form.ClientSize.Width-1,form.ClientSize.Height-1);};
  using(var tip=new ToolTip()){tip.SetToolTip(sourceLabel,source);tip.SetToolTip(destination,root);form.Icon=Icon.ExtractAssociatedIcon(Self);form.AcceptButton=install;form.CancelButton=close;if(previewComplete)finish();Application.Run(form);}
 }
 static void ShowUninstall(string root){
  if(!Owned(root))throw new IOException(T("설치된 플러그인 폴더 안에서 Uninstall.exe를 실행하세요.","Run Uninstall.exe inside the installed plugin folder."));
  var form=new Form{Text=T("ConsoleSmooth · 플러그인 제거","ConsoleSmooth · Uninstall plugin"),ClientSize=new Size(650,320),StartPosition=FormStartPosition.CenterScreen,BackColor=Bg,ForeColor=Color.White,Font=UiFont(11),FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false,MinimizeBox=false,AutoScaleMode=AutoScaleMode.None};
  form.Controls.Add(new FrameMark{Location=new Point(25,25)});form.Controls.Add(LabelAt(T("프레임 생성 제거","Remove frame generation"),90,30,510,42,18,Color.White));
  var status=LabelAt(T("Streaming Center를 닫고 제거를 누르세요.\r\n원본 앱과 사용자 설정은 유지됩니다.","Close Streaming Center and press Uninstall.\r\nThe original app and your settings are kept."),26,100,596,95,11,Muted);
  var remove=ButtonAt(T("제거","Uninstall"),280,245,160,true);var close=ButtonAt(T("닫기","Close"),460,245,160,false);close.Click+=delegate{form.Close();};
  bool busy=false,done=false;form.FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy)e.Cancel=true;};
  remove.Click+=async delegate{
   if(done){form.Close();return;}busy=true;remove.Enabled=close.Enabled=false;status.Text=T("제거 중…","Uninstalling…");
   try{await Task.Run(()=>{
    var identity=System.Security.Principal.WindowsIdentity.GetCurrent();
    if(new System.Security.Principal.WindowsPrincipal(identity).IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator)){Remove(root);return;}
    using(var process=Process.Start(new ProcessStartInfo(Self){UseShellExecute=true,Verb="runas",Arguments="--remove"})){process.WaitForExit();if(process.ExitCode!=0)throw new IOException(T("제거를 완료하지 못했습니다. 앱 종료와 파일 상태를 확인하세요.","Could not finish uninstalling. Check that the app is closed and the files."));}
   });SetTracer(false);done=true;status.Text=T("제거가 완료되었습니다.\r\n설정과 변경한 파일은 보존했습니다.\r\n남은 제거 프로그램과 폴더는 직접 삭제할 수 있습니다.","Uninstall finished.\r\nSettings and changed files were kept.\r\nYou can delete the remaining uninstaller and folder yourself.");remove.Text=T("완료","Done");remove.Bounds=close.Bounds;close.Visible=false;form.CancelButton=remove;}
   catch(Exception ex){status.Text=ex.Message;}
   finally{busy=false;remove.Enabled=close.Enabled=true;}
  };
  form.Controls.AddRange(new Control[]{status,remove,close});form.AcceptButton=remove;form.CancelButton=close;Application.Run(form);
 }
}

