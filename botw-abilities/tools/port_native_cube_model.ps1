# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
# Loaded by the shared native model loader after Toolbox initialization.
Add-Type -ReferencedAssemblies (Join-Path $libDir 'Newtonsoft.Json.dll') -TypeDefinition @"
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Newtonsoft.Json.Linq;
public static class NativeCubeModel {
 static object Get(object o,string n) {
  var f=o.GetType().GetField(n); return f!=null?f.GetValue(o):o.GetType().GetProperty(n).GetValue(o,null);
 }
 static void Set(object o,string n,object v) {
  var f=o.GetType().GetField(n); if(f!=null)f.SetValue(o,v);else o.GetType().GetProperty(n).SetValue(o,v,null);
 }
 static object Vec(Type type,float[] values) {
  var v=Activator.CreateInstance(type);for(int i=0;i<values.Length;i++)Set(v,"XYZW"[i].ToString(),values[i]);return v;
 }
 static object Model(object root) {return ((IList)Get(Get(root,"resFile"),"Models"))[0];}
 static float[] Values(JToken vertex,string name) {
  string key=name=="_p0"?"pos":name=="_n0"?"nrm":name=="_u0"?"uv":name=="_t0"?"tan":name=="_b0"?"bitan":null;
  if(key!=null)return vertex[key].Select(x=>(float)x).ToArray();
  if(name=="_i0")return new float[]{0,0,0,0};
  if(name=="_w0")return new float[]{1,0,0,0};
  throw new Exception("Unmapped native cube vertex attribute "+name);
 }
 public static void Run(object root,string mesh) {
  var doc=JObject.Parse(File.ReadAllText(mesh))["models"][0];
  var model=Model(root);var res=Get(root,"resFile");
  var shapes=(IList)Get(model,"Shapes");var buffers=(IList)Get(model,"VertexBuffers");
  if(doc["shapes"].Count()!=2 || shapes.Count!=5)throw new Exception("Unexpected cube/template shapes");
  var helperType=res.GetType().Assembly.GetType("Syroot.NintenTools.NSW.Bfres.Helpers.VertexBufferHelper",true);
  for(int i=0;i<2;i++) {
   var input=doc["shapes"][i];var vertices=input["vertices"];var shape=shapes[i];
   if((int)input["material"]!=i || (int)input["bone"]!=0)throw new Exception("Unexpected cube bindings");
   var helper=Activator.CreateInstance(helperType,new object[]{buffers[i],null});
   foreach(object attr in (IEnumerable)Get(helper,"Attributes")) {
    var type=Get(attr,"Data").GetType().GetElementType();var data=Array.CreateInstance(type,vertices.Count());
    for(int v=0;v<data.Length;v++)data.SetValue(Vec(type,Values(vertices[v],(string)Get(attr,"Name"))),v);
    Set(attr,"Data",data);
   }
   buffers[i]=helperType.GetMethod("ToVertexBuffer",Type.EmptyTypes).Invoke(helper,null);
   Set(buffers[i],"VertexSkinCount",(byte)1);Set(shape,"VertexSkinCount",(byte)1);
   Set(shape,"BoneIndex",(ushort)0);Set(shape,"MaterialIndex",(ushort)i);
   var meshes=(IList)Get(shape,"Meshes");while(meshes.Count>1)meshes.RemoveAt(meshes.Count-1);
   var indices=input["indices"].Select(x=>(uint)x).ToList();var mesh0=meshes[0];
   mesh0.GetType().GetMethod("SetIndices").Invoke(mesh0,new object[]{indices,null});
   Set(mesh0,"FirstVertex",(uint)0);
   var sub=(IList)Get(mesh0,"SubMeshes");while(sub.Count>1)sub.RemoveAt(sub.Count-1);
   Set(sub[0],"Offset",(uint)0);Set(sub[0],"Count",(uint)indices.Count);
   var min=Enumerable.Range(0,3).Select(a=>vertices.Min(v=>(float)v["pos"][a])).ToArray();
   var max=Enumerable.Range(0,3).Select(a=>vertices.Max(v=>(float)v["pos"][a])).ToArray();
   var bounds=(IList)Get(shape,"SubMeshBoundings");
   for(int b=0;b<bounds.Count;b++) {
    var box=bounds[b];
    Set(box,"Center",Vec(Get(box,"Center").GetType(),Enumerable.Range(0,3).Select(a=>(min[a]+max[a])*.5f).ToArray()));
    Set(box,"Extent",Vec(Get(box,"Extent").GetType(),Enumerable.Range(0,3).Select(a=>(max[a]-min[a])*.5f).ToArray()));
    bounds[b]=box;
   }
   var radii=(IList)Get(shape,"RadiusArray");for(int b=0;b<radii.Count;b++)radii[b]=1.0f;
  }
  var dict=Get(model,"ShapeDict");dict.GetType().GetMethod("Clear").Invoke(dict,null);
  while(shapes.Count>2)shapes.RemoveAt(shapes.Count-1);
  while(buffers.Count>2)buffers.RemoveAt(buffers.Count-1);
  foreach(object shape in shapes)dict.GetType().GetMethod("Add",new[]{typeof(string)}).Invoke(dict,new[]{Get(shape,"Name")});
  var skeleton=Get(model,"Skeleton");var bones=(IList)Get(skeleton,"Bones");
  if(!(bool)Get(bones[0],"TransformIdentity"))throw new Exception("Cube root must be identity");
  var matrix=(IList)Get(skeleton,"MatrixToBoneList");for(int i=0;i<matrix.Count;i++)matrix[i]=(ushort)0;
  foreach(object material in (IEnumerable)Get(model,"Materials")) {
   var refs=(IList)Get(material,"TextureRefs");
   for(int i=0;i<refs.Count;i++)refs[i]=((string)refs[i]).Replace("RemoteBombBall_","TotkNativeCube_");
  }
  Set(res,"Name","TotkNativeCube.SpObj_TimerBomb_A_01");
 }
 public static void Save(object root,string path) {Get(root,"resFile").GetType().GetMethod("Save",new[]{typeof(string)}).Invoke(Get(root,"resFile"),new object[]{path});}
 public static void Verify(object root,string mesh) {
  var source=JObject.Parse(File.ReadAllText(mesh))["models"][0];var model=Model(root);
  var shapes=(IList)Get(model,"Shapes");var buffers=(IList)Get(model,"VertexBuffers");
  if(shapes.Count!=2 || buffers.Count!=2 || ((IList)Get(model,"Materials")).Count!=2)throw new Exception("Cube tables changed");
  var helperType=Get(root,"resFile").GetType().Assembly.GetType("Syroot.NintenTools.NSW.Bfres.Helpers.VertexBufferHelper",true);
  for(int i=0;i<2;i++) {
   var input=source["shapes"][i];var shape=shapes[i];
   var native=((IList)Get(shape,"Meshes"))[0];
   if(!((IEnumerable)native.GetType().GetMethod("GetIndices").Invoke(native,null)).Cast<uint>().SequenceEqual(input["indices"].Select(x=>(uint)x)))throw new Exception("Cube topology changed");
   var helper=Activator.CreateInstance(helperType,new object[]{buffers[i],null});
   foreach(object attr in (IEnumerable)Get(helper,"Attributes")) {
    string name=(string)Get(attr,"Name");var data=(IList)Get(attr,"Data");
    if(data.Count!=input["vertices"].Count())throw new Exception("Cube vertex count changed");
    for(int v=0;v<data.Count;v++) {
     var expected=Values(input["vertices"][v],name);
     for(int a=0;a<expected.Length;a++)if(Math.Abs(Convert.ToSingle(Get(data[v],"XYZW"[a].ToString()))-expected[a])>(name=="_p0"?.00001f:.012f))throw new Exception("Cube vertex attribute changed: "+name);
    }
   }
   foreach(object box in (IEnumerable)Get(shape,"SubMeshBoundings"))for(int a=0;a<3;a++) {
    float min=input["vertices"].Min(v=>(float)v["pos"][a]),max=input["vertices"].Max(v=>(float)v["pos"][a]);
    if(Math.Abs(Convert.ToSingle(Get(Get(box,"Center"),"XYZ"[a].ToString()))-(min+max)*.5f)>.00001f || Math.Abs(Convert.ToSingle(Get(Get(box,"Extent"),"XYZ"[a].ToString()))-(max-min)*.5f)>.00001f)throw new Exception("Cube bounds changed");
   }
  }
  Console.WriteLine("Native cube round trip: original 1140 vertices, 2970 indices, all attributes and bounds verified");
 }
}
"@
[NativeCubeModel]::Run($bfres,$ReferenceMesh)
$cubeOutput=Join-Path $OutputDir 'TotkNativeCube.SpObj_TimerBomb_A_01.bfres'
[NativeCubeModel]::Save($bfres,$cubeOutput)
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $shadowHelpers 'toolbox_compress_bfres_mc.ps1') -InputBfres $cubeOutput -OutputBfresMc ($cubeOutput+'.mc') -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath
if ($LASTEXITCODE -ne 0) { throw 'Native cube model compression failed' }
$cubeRoundTrip=Join-Path $OutputDir 'round-trip.bfres'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $shadowHelpers 'toolbox_decompress_bfres_mc.ps1') -InputBfresMc ($cubeOutput+'.mc') -OutputBfres $cubeRoundTrip -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath
if ($LASTEXITCODE -ne 0) { throw 'Native cube round trip decode failed' }
$cubeModel=[Activator]::CreateInstance($bfresType,$true)
$cubeModel.FilePath=$cubeRoundTrip; $cubeModel.FileName=[IO.Path]::GetFileName($cubeRoundTrip)
$cubeModel.IFileInfo=[Toolbox.Library.IFileInfo]::new(); $cubeModel.IFileInfo.FileIsCompressed=$false
$stream=[IO.File]::OpenRead($cubeRoundTrip)
try {$cubeModel.Load($stream)} finally {$stream.Close()}
[NativeCubeModel]::Verify($cubeModel,$ReferenceMesh)
$textureFolder=$cubeModel.Nodes | Where-Object Text -eq 'TexToGo'
$checkedTextures=0
foreach ($texture in $textureFolder.Nodes) {
  if ($texture.Text -notlike 'TotkNativeCube_*') { continue }
  $suffix=$texture.Text.Split('_')[-1]
  for ($mip=0; $mip -lt $texture.MipCount; $mip++) {
    $encoded=[IO.File]::ReadAllBytes((Join-Path $TotkRomfs "encoded/$suffix-$mip.astc"))
    $actual=$texture.GetImageData(0,$mip,0)
    $expected=[byte[]]$encoded[16..($encoded.Length-1)]
    if ([Convert]::ToBase64String($actual) -ne [Convert]::ToBase64String($expected)) {
      throw "Native cube texture failed independent Toolbox readback: $suffix mip $mip"
    }
  }
  $checkedTextures++
}
if ($checkedTextures -ne 4) { throw 'Native cube texture references are incomplete' }
Write-Output 'Native cube textures: four maps and all mips match independent Toolbox readback'
