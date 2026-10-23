# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
param(
  [Parameter(Mandatory = $true)][string]$InputBfresMc,
  [Parameter(Mandatory = $true)][string]$ToolboxDir,
  [Parameter(Mandatory = $true)][string]$ToolingHelpers,
  [Parameter(Mandatory = $true)][string]$TotkRomfs,
  [string]$UnsafeCompatPath = "",
  [Parameter(Mandatory = $true)][string]$OutputDir,
  [Parameter(Mandatory = $true)][string]$ReferenceMesh,
  [Parameter(Mandatory = $true)][string]$AnimationBfres,
  [switch]$Cube
)

$ErrorActionPreference = "Stop"
$shadowHelpers = (Resolve-Path -LiteralPath $ToolingHelpers).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$InputBfres = Join-Path $OutputDir 'donor-expanded.bfres'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $shadowHelpers 'toolbox_decompress_bfres_mc.ps1') -InputBfresMc $InputBfresMc -OutputBfres $InputBfres -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath
if ($LASTEXITCODE -ne 0) { throw 'Native sphere model expansion failed' }

$libDir = Join-Path $ToolboxDir "Lib"
$pluginDir = Join-Path $libDir "Plugins"

function Get-AssemblyVersion {
  param([string]$Path)
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
  try {
    $version = [Reflection.AssemblyName]::GetAssemblyName($Path).Version
    if ($version) { return $version.ToString() }
  } catch {}
  return $null
}

[AppDomain]::CurrentDomain.add_AssemblyResolve({
  param($sender, $args)
  $simple = ($args.Name -split ",")[0]
  $assemblyName = [Reflection.AssemblyName]$args.Name
  $requestedVersion = if ($assemblyName.Version) { $assemblyName.Version.ToString() } else { "" }
  if ($simple -eq "System.Runtime.CompilerServices.Unsafe" -and $UnsafeCompatPath -and (Test-Path -LiteralPath $UnsafeCompatPath)) {
    if ($requestedVersion -and (Get-AssemblyVersion $UnsafeCompatPath) -ne $requestedVersion) { return $null }
    return [Reflection.Assembly]::LoadFrom($UnsafeCompatPath)
  }
  $name = $simple + ".dll"
  foreach ($dir in @($ToolboxDir, $libDir, $pluginDir)) {
    $candidate = Join-Path $dir $name
    if (Test-Path -LiteralPath $candidate) {
      if ($simple -eq "System.Runtime.CompilerServices.Unsafe" -and $requestedVersion -and (Get-AssemblyVersion $candidate) -ne $requestedVersion) { continue }
      return [Reflection.Assembly]::LoadFrom($candidate)
    }
  }
  return $null
}) | Out-Null

Set-Location -LiteralPath $ToolboxDir
$env:PATH = (Join-Path $ToolboxDir "x64") + ";" + $ToolboxDir + ";" + $env:PATH

$loadList = @(
  (Join-Path $ToolboxDir "Toolbox.Library.dll"),
  (Join-Path $ToolboxDir "Gl_EditorFramework.dll"),
  (Join-Path $ToolboxDir "Toolbox.exe"),
  (Join-Path $libDir "OpenTK.dll"),
  (Join-Path $libDir "OpenTK.GLControl.dll"),
  (Join-Path $libDir "Newtonsoft.Json.dll"),
  (Join-Path $libDir "Syroot.Maths.dll"),
  (Join-Path $libDir "Syroot.BinaryData.dll"),
  (Join-Path $libDir "Syroot.NintenTools.Bfres.dll"),
  (Join-Path $libDir "Syroot.NintenTools.NSW.Bfres.dll"),
  (Join-Path $libDir "Syroot.NintenTools.NSW.Bntx.dll"),
  (Join-Path $pluginDir "System.Buffers.dll"),
  (Join-Path $pluginDir "System.Memory.dll"),
  (Join-Path $pluginDir "System.Numerics.Vectors.dll"),
  (Join-Path $pluginDir "System.Threading.Tasks.Extensions.dll"),
  (Join-Path $pluginDir "ZstdSharp.dll"),
  (Join-Path $pluginDir "FirstPlugin.Plg.dll")
)
if ($UnsafeCompatPath) { $loadList += $UnsafeCompatPath }

foreach ($path in $loadList) {
  if (Test-Path -LiteralPath $path) {
    [Reflection.Assembly]::LoadFrom($path) | Out-Null
  }
}

function Set-StaticField($typeName, $fieldName, $value) {
  $type = $null
  foreach ($asm in [AppDomain]::CurrentDomain.GetAssemblies()) {
    $type = $asm.GetType($typeName, $false)
    if ($null -ne $type) { break }
  }
  if ($null -eq $type) { throw "Missing type $typeName" }
  $field = $type.GetField($fieldName, [Reflection.BindingFlags]"Public,NonPublic,Static")
  if ($null -eq $field) { throw "Missing field $typeName.$fieldName" }
  $field.SetValue($null, $value)
}

Set-StaticField "Toolbox.Library.Runtime" "ExecutableDir" $ToolboxDir
Set-StaticField "Toolbox.Library.Runtime" "TotkGamePath" $TotkRomfs
Set-StaticField "Toolbox.Library.Runtime" "DisplayViewport" $false
Set-StaticField "Toolbox.Library.Runtime" "UseOpenGL" $false
Set-StaticField "Toolbox.Library.Runtime" "OpenTKInitialized" $true
Set-StaticField "Toolbox.Library.Runtime" "AddFilesToActiveObjectEditor" $false

$objectEditorType = $null
foreach ($asm in [AppDomain]::CurrentDomain.GetAssemblies()) {
  $objectEditorType = $asm.GetType("Toolbox.Library.Forms.ObjectEditor", $false)
  if ($null -ne $objectEditorType) { break }
}
$objectEditor = [Activator]::CreateInstance($objectEditorType, $true)
Set-StaticField "Toolbox.Library.Runtime" "MainForm" $objectEditor

$bfresType = $null
foreach ($asm in [AppDomain]::CurrentDomain.GetAssemblies()) {
  $bfresType = $asm.GetType("FirstPlugin.BFRES", $false)
  if ($null -ne $bfresType) { break }
}
if ($null -eq $bfresType) { throw "Missing FirstPlugin.BFRES" }

$bfres = [Activator]::CreateInstance($bfresType, $true)
$bfres.FilePath = $InputBfres
$bfres.FileName = [IO.Path]::GetFileName($InputBfres)
$bfres.IFileInfo = [Toolbox.Library.IFileInfo]::new()
$bfres.IFileInfo.FileIsCompressed = $false

$input = [IO.File]::OpenRead($InputBfres)
try {
  $bfres.Load($input)
} finally {
  $input.Close()
}




if ($Cube) {
  . (Join-Path $PSScriptRoot 'port_native_cube_model.ps1')
  return
}

Add-Type -TypeDefinition @"
using System;
using System.Collections;
using System.Reflection;
public static class NativeSphereModel {
 static object Get(object o,string n) {
  if(o==null)return null;
  var f=o.GetType().GetField(n); if(f!=null)return f.GetValue(o);
  var p=o.GetType().GetProperty(n); return p==null?null:p.GetValue(o,null);
 }
 static void Set(object o,string n,object v) {
  var f=o.GetType().GetField(n); if(f!=null){f.SetValue(o,v);return;}
  var p=o.GetType().GetProperty(n); if(p==null)throw new Exception("Missing " + o.GetType()+"."+n); p.SetValue(o,v,null);
 }
 static object ScaleVector(object v,float s,bool w) {
  foreach(char c in (w?"W":"XYZ"))Set(v,c.ToString(),Convert.ToSingle(Get(v,c.ToString()))*s);
  return v;
 }
 public static void Save(object root,string path) { var res=Get(root,"resFile");res.GetType().GetMethod("Save",new[]{typeof(string)}).Invoke(res,new object[]{path}); }

 public static void ScaleAnimation(object root,float scale) {
  foreach(object animation in (IEnumerable)Get(Get(root,"resFile"),"SkeletalAnims"))
   foreach(object bone in (IEnumerable)Get(animation,"BoneAnims")) {
    var data=Get(bone,"BaseData");Set(data,"Translate",ScaleVector(Get(data,"Translate"),scale,false));Set(bone,"BaseData",data);
    foreach(object curve in (IEnumerable)Get(bone,"Curves")) {
     if(Convert.ToUInt32(Get(curve,"AnimDataOffset"))!=20 || Get(bone,"FlagsCurve").ToString()!="TranslateY")
      throw new Exception("Unexpected native sphere animation channel");
     Set(curve,"Scale",Convert.ToSingle(Get(curve,"Scale"))*scale);
     Set(curve,"Delta",Convert.ToSingle(Get(curve,"Delta"))*scale);
     var offset=Get(curve,"Offset");Set(offset,"Single",Convert.ToSingle(Get(offset,"Single"))*scale);Set(curve,"Offset",offset);
    }
   }
 }
 static void EqualSequence(object before,object after) {
  var a=(IList)before;var b=(IList)after;
  if(a.Count!=b.Count)throw new Exception("Animation sequence length changed");
  var aa=a.GetEnumerator();var bb=b.GetEnumerator();
  while(aa.MoveNext())if(!bb.MoveNext() || !aa.Current.Equals(bb.Current))throw new Exception("Animation keys changed");
 }
 public static void ValidateAnimation(object beforeRoot,object afterRoot,float scale) {
  var aa=(IList)Get(Get(beforeRoot,"resFile"),"SkeletalAnims");var bb=(IList)Get(Get(afterRoot,"resFile"),"SkeletalAnims");
  if(aa.Count!=1 || bb.Count!=1)throw new Exception("Native skeletal animation count changed");
  foreach(string key in new[]{"Name","FrameCount","FlagsAnimSettings","FlagsScale","FlagsRotate"})if(!Get(aa[0],key).Equals(Get(bb[0],key)))throw new Exception("Native animation timing changed");
  var ab=(IList)Get(aa[0],"BoneAnims");var bbs=(IList)Get(bb[0],"BoneAnims");
  if(ab.Count!=bbs.Count)throw new Exception("Native animated skeleton changed");
  for(int i=0;i<ab.Count;i++) {
   var a=ab[i];var b=bbs[i];var ad=Get(a,"BaseData");var bd=Get(b,"BaseData");
   CheckVector(Get(ad,"Translate"),Get(bd,"Translate"),scale,.00001f);
   foreach(string key in new[]{"Scale","Rotate"})if(!Get(ad,key).Equals(Get(bd,key)))throw new Exception("Native rotation or scale animation changed");
   var ac=(IList)Get(a,"Curves");var bc=(IList)Get(b,"Curves");
   if(ac.Count!=bc.Count)throw new Exception("Native animation curves changed");
   for(int j=0;j<ac.Count;j++) {
    foreach(string key in new[]{"AnimDataOffset","StartFrame","EndFrame"})if(!Get(ac[j],key).Equals(Get(bc[j],key)))throw new Exception("Native curve timing changed");
    if(Math.Abs(Convert.ToSingle(Get(ac[j],"Scale"))*scale-Convert.ToSingle(Get(bc[j],"Scale")))>.0000001f ||
       Math.Abs(Convert.ToSingle(Get(ac[j],"Delta"))*scale-Convert.ToSingle(Get(bc[j],"Delta")))>.000001f ||
       Math.Abs(Convert.ToSingle(Get(Get(ac[j],"Offset"),"Single"))*scale-Convert.ToSingle(Get(Get(bc[j],"Offset"),"Single")))>.00001f)
     throw new Exception("Native translation animation did not scale");
    EqualSequence(Get(ac[j],"Frames"),Get(bc[j],"Frames"));EqualSequence(Get(ac[j],"Keys"),Get(bc[j],"Keys"));
   }
  }
  Console.WriteLine("Native animation round trip: timing, keys, rotation and scale preserved; translation resized");
 }

 static void CheckVector(object before,object after,float scale,float epsilon) {
  foreach(char c in "XYZ")if(Math.Abs(Convert.ToSingle(Get(before,c.ToString()))*scale-Convert.ToSingle(Get(after,c.ToString())))>epsilon)
   throw new Exception("Native sphere coordinate round trip failed: "+c);
 }
 public static void Validate(object beforeRoot,object afterRoot,float scale) {
  var before=Get(beforeRoot,"resFile");var after=Get(afterRoot,"resFile");
  if((string)Get(after,"Name")!="TotkNativeSphere.SpObj_TimerBomb_A_01")throw new Exception("Native model namespace lost");
  var oldModel=((IList)Get(before,"Models"))[0];var newModel=((IList)Get(after,"Models"))[0];
  var oldShapes=(IList)Get(oldModel,"Shapes");var newShapes=(IList)Get(newModel,"Shapes");
  if(oldShapes.Count!=5 || newShapes.Count!=oldShapes.Count)throw new Exception("Native sphere shapes changed");
  var helper=before.GetType().Assembly.GetType("Syroot.NintenTools.NSW.Bfres.Helpers.VertexBufferHelper",true);
  var oldBuffers=(IList)Get(oldModel,"VertexBuffers");var newBuffers=(IList)Get(newModel,"VertexBuffers");
  if(oldBuffers.Count!=newBuffers.Count)throw new Exception("Native vertex buffers changed");
  for(int i=0;i<oldBuffers.Count;i++) {
   var a=Activator.CreateInstance(helper,new object[]{oldBuffers[i],null});
   var b=Activator.CreateInstance(helper,new object[]{newBuffers[i],null});
   var aa=(IList)Get(a,"Attributes");var bb=(IList)Get(b,"Attributes");
   if(aa.Count!=bb.Count)throw new Exception("Native vertex attributes changed");
   for(int j=0;j<aa.Count;j++) {
    var name=(string)Get(aa[j],"Name");if(name!=(string)Get(bb[j],"Name"))throw new Exception("Vertex attribute identity changed");
    var oldData=(IList)Get(aa[j],"Data");var newData=(IList)Get(bb[j],"Data");
    if(oldData.Count!=newData.Count)throw new Exception("Vertex count changed");
    for(int k=0;k<oldData.Count;k++)CheckVector(oldData[k],newData[k],name=="_p0"?scale:1f,.0006f);
   }
  }
  for(int i=0;i<oldShapes.Count;i++) {
   var a=oldShapes[i];var b=newShapes[i];
   foreach(string key in new[]{"MaterialIndex","BoneIndex","VertexSkinCount","VertexBufferIndex"})if(!Get(a,key).Equals(Get(b,key)))throw new Exception("Native shape binding changed");
   var ab=(IList)Get(a,"SubMeshBoundings");var bb=(IList)Get(b,"SubMeshBoundings");
   if(ab.Count!=bb.Count)throw new Exception("Native culling records changed");
   for(int j=0;j<ab.Count;j++)foreach(string key in new[]{"Center","Extent"})CheckVector(Get(ab[j],key),Get(bb[j],key),scale,.00001f);
   var ar=(IList)Get(a,"RadiusArray");var br=(IList)Get(b,"RadiusArray");
   if(ar.Count!=br.Count)throw new Exception("Native radius records changed");
   for(int j=0;j<ar.Count;j++)if(Convert.ToSingle(br[j])+.00001f<Convert.ToSingle(ar[j])*scale || Convert.ToSingle(br[j])>Convert.ToSingle(ar[j])+.00001f)throw new Exception("Native culling radius is not conservative");
  }
  var oldBones=(IList)Get(Get(oldModel,"Skeleton"),"Bones");var newBones=(IList)Get(Get(newModel,"Skeleton"),"Bones");
  if(oldBones.Count!=newBones.Count)throw new Exception("Native skeleton changed");
  for(int i=0;i<oldBones.Count;i++)CheckVector(Get(oldBones[i],"Position"),Get(newBones[i],"Position"),scale,.00001f);
  Console.WriteLine("Native sphere round trip: 5 shapes, all vertex attributes, bindings, bones and serialized bounds verified");
 }
 public static float Run(object root,float targetWidth) {
  var wrapper=((IList)Get(((IList)Get(root,"Nodes"))[0],"Nodes"))[0];
  var body=((IList)Get(wrapper,"shapes"))[0];
  float min=float.PositiveInfinity,max=float.NegativeInfinity;
  foreach(object vertex in (IEnumerable)Get(body,"vertices")) { float x=Convert.ToSingle(Get(Get(vertex,"pos"),"X")); min=Math.Min(min,x);max=Math.Max(max,x); }
  float scale=targetWidth/(max-min);
  if(!(scale>0 && scale<1))throw new Exception("Invalid sphere scale");

  var res=Get(root,"resFile"); var model=((IList)Get(res,"Models"))[0];
  var helperType=res.GetType().Assembly.GetType("Syroot.NintenTools.NSW.Bfres.Helpers.VertexBufferHelper",true);
  var buffers=(IList)Get(model,"VertexBuffers");
  for(int i=0;i<buffers.Count;i++) {
   var helper=Activator.CreateInstance(helperType,new object[]{buffers[i],null});
   bool found=false;
   foreach(object attribute in (IEnumerable)Get(helper,"Attributes"))if((string)Get(attribute,"Name")=="_p0") {
    var values=(IList)Get(attribute,"Data");
    for(int j=0;j<values.Count;j++)values[j]=ScaleVector(values[j],scale,false);
    found=true;
   }
   if(!found)throw new Exception("Native sphere position attribute missing");
   buffers[i]=helperType.GetMethod("ToVertexBuffer",Type.EmptyTypes).Invoke(helper,null);
  }
  foreach(object shape in (IEnumerable)Get(model,"Shapes")) {
   // MeshCodec retains conservative source sphere radii; serialized boxes scale exactly.
   var boxes=(IList)Get(shape,"SubMeshBoundings");
   for(int i=0;i<boxes.Count;i++) {
    var box=boxes[i];
    Set(box,"Center",ScaleVector(Get(box,"Center"),scale,false));
    Set(box,"Extent",ScaleVector(Get(box,"Extent"),scale,false));
    boxes[i]=box;
   }
  }
  Set(res,"Name","TotkNativeSphere.SpObj_TimerBomb_A_01");
  var skeleton=Get(model,"Skeleton");
  foreach(object bone in (IEnumerable)Get(skeleton,"Bones")) {
   var name=Get(bone,"Position")!=null?"Position":"Translation";
   Set(bone,name,ScaleVector(Get(bone,name),scale,false));
  }
  var inverse=(IList)Get(skeleton,"InverseModelMatrices");
  for(int i=0;inverse!=null && i<inverse.Count;i++) {
   var matrix=inverse[i];
   if(Get(matrix,"Row3")!=null)Set(matrix,"Row3",ScaleVector(Get(matrix,"Row3"),scale,false));
   else if(Get(Get(matrix,"Row0"),"W")!=null)foreach(string row in new[]{"Row0","Row1","Row2"})Set(matrix,row,ScaleVector(Get(matrix,row),scale,true));
   else throw new Exception("Unknown inverse-bind matrix type "+matrix.GetType());
   inverse[i]=matrix;
  }
  Console.WriteLine("Native sphere scaled " + scale + "; inverse bind matrices="+(inverse==null?0:inverse.Count));
  return scale;
 }
}
"@
$reference = Get-Content -LiteralPath $ReferenceMesh -Raw | ConvertFrom-Json
$body = $reference.models[0].shapes | Where-Object { $_.material -eq 0 }
$range = $body.vertices | ForEach-Object { $_.pos[0] } | Measure-Object -Minimum -Maximum
$scale = [NativeSphereModel]::Run($bfres,[single]($range.Maximum-$range.Minimum))
@{scale=[double]$scale;target_width=($range.Maximum-$range.Minimum)} | ConvertTo-Json | Set-Content -Encoding ASCII (Join-Path $OutputDir 'scale.json')
$output = Join-Path $OutputDir 'TotkNativeSphere.SpObj_TimerBomb_A_01.bfres'

[NativeSphereModel]::Save($bfres,$output)
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $shadowHelpers 'toolbox_compress_bfres_mc.ps1') -InputBfres $output -OutputBfresMc ($output+'.mc') -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath
if ($LASTEXITCODE -ne 0) { throw 'Native sphere model compression failed' }

$roundTrip=Join-Path $OutputDir 'round-trip.bfres'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $shadowHelpers 'toolbox_decompress_bfres_mc.ps1') -InputBfresMc ($output+'.mc') -OutputBfres $roundTrip -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath
if ($LASTEXITCODE -ne 0) { throw 'Native sphere round trip decode failed' }
function Read-Model($path) {
 $model=[Activator]::CreateInstance($bfresType,$true)
 $model.FilePath=$path
 $model.FileName=[IO.Path]::GetFileName($path)
 $model.IFileInfo=[Toolbox.Library.IFileInfo]::new()
 $model.IFileInfo.FileIsCompressed=$false
 $stream=[IO.File]::OpenRead($path)
 try {$model.Load($stream)} finally {$stream.Close()}
 return $model
}
$original=Read-Model $InputBfres
$reopened=Read-Model $roundTrip
[NativeSphereModel]::Validate($original,$reopened,[single]$scale)
$animation=Read-Model $AnimationBfres
[NativeSphereModel]::ScaleAnimation($animation,[single]$scale)
$animationOutput=Join-Path $OutputDir 'TotkNativeSphere.anim.bfres'
[NativeSphereModel]::Save($animation,$animationOutput)
$sourceAnimation=Read-Model $AnimationBfres
$reopenedAnimation=Read-Model $animationOutput
[NativeSphereModel]::ValidateAnimation($sourceAnimation,$reopenedAnimation,[single]$scale)
