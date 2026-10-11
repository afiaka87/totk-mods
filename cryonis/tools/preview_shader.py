# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Render the actual GLSL with private original geometry and independent water/depth fixtures."""
from pathlib import Path
import argparse
import importlib.util
import json
import moderngl
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser()
parser.add_argument("--reference",type=Path,required=True)
parser.add_argument("--output",type=Path,required=True)
parser.add_argument("--baseline-shaders",type=Path,help="Optional retained water shaders for the rejected-candidate regression")
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location("compiler",ROOT/"tools/compile_shaders.py")
compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
ctx=moderngl.create_standalone_context(require=450)
# Native pass explicitly disables culling (its default is back-face culling).
ctx.disable(moderngl.CULL_FACE)
def shader(name):return compiler.expanded_source(ROOT/"shaders"/name)
def native_fragment(source):return source.replace('#version 450','#version 450\nlayout(origin_upper_left) in vec4 gl_FragCoord;',1)
water=ctx.program(vertex_shader=shader("water.vert"),fragment_shader=native_fragment(shader("water.frag")))
ice=ctx.program(vertex_shader=shader("ice.vert"),fragment_shader=native_fragment(shader("ice.frag")))
effects=ctx.program(vertex_shader=shader("effects.vert"),fragment_shader=native_fragment(shader("effects.frag")))
hud_fragment=shader("hud.frag").replace('#version 450','#version 450\nlayout(origin_upper_left) in vec4 gl_FragCoord;',1)
hud=ctx.program(vertex_shader=shader("hud.vert"),fragment_shader=hud_fragment)
mesh=json.loads((args.reference/"original-mesh.json").read_text(encoding="utf-8-sig"))
mesh_data=[]
for v in mesh["vertices"]:
    bones=v["bones"]+[0]*(2-len(v["bones"]))
    weights=v["weights"]+[0]*(2-len(v["weights"]))
    mesh_data.extend([v["pos"]+[v["uv"][0]],v["nrm"]+[v["uv"][1]],v["tan"],v["bitan"],bones+weights])
mesh_ubo=ctx.buffer(np.array(mesh_data,dtype="f4").tobytes());mesh_ubo.bind_to_uniform_block(1)
indices=ctx.buffer(np.array(mesh["indices"],dtype="u2").tobytes())
ice_vao=ctx.vertex_array(ice,[],indices,index_element_size=2)
water_vao=ctx.vertex_array(water,[])
baseline_vao=None
if args.baseline_shaders:
    baseline=ctx.program(vertex_shader=compiler.expanded_source(args.baseline_shaders/'water.vert'),
        fragment_shader=native_fragment(compiler.expanded_source(args.baseline_shaders/'water.frag')))
    baseline['sceneDepth'].value=0;baseline['originalCaustic'].value=3
    baseline_vao=ctx.vertex_array(baseline,[])
effects_vao=ctx.vertex_array(effects,[]);hud_vao=ctx.vertex_array(hud,[])
for i,name in enumerate(("Obj_IcemakerBlock_A_Emm","Obj_IcemakerBlock_A_Nrm","IcemakerEffect_Fx")):
    image=Image.open(args.reference/(name+".png")).convert("RGBA")
    texture=ctx.texture(image.size,4,image.tobytes());texture.filter=(moderngl.LINEAR,moderngl.LINEAR);texture.use(i+1)
    # Retain texture objects for the duration of the graphics context.
    globals()["texture_"+str(i)]=texture
for name,unit in (("sceneDepth",0),("originalPattern",1),("originalIce",2),("originalCaustic",3)):
    if name in ice:ice[name].value=unit
water["sceneDepth"].value=0;water["originalCaustic"].value=3
w,h=1024,768
target=ctx.texture((w,h),4,dtype="f4");fbo=ctx.framebuffer([target])
ubo=ctx.buffer(reserve=17*16);ubo.bind_to_uniform_block(0)
grid_ubo=ctx.buffer(reserve=4096*4);grid_ubo.bind_to_uniform_block(2)
cos,sin=np.float32(.980580676),np.float32(.196116135)
eye=np.array([0,4,10],dtype="f4")
a=np.zeros((17,4),dtype="f4")
a[0]=[w,h,.1,1000]
a[1]=[1,0,0,eye[0]];a[2]=[0,cos,sin,eye[1]];a[3]=[0,-sin,cos,eye[2]]
a[4]=[1.45,1.45*w/h,0,0]
a[5]=[0,0,0,0];a[6]=[90,100,0,0]
a[8:15,:2]=1;a[15]=[0,0,375,64]
a[16]=[1,0,.9,0]
y,x=np.mgrid[0:h,0:w]
view_ray=np.stack(((2*(x+.5)/w-1)/a[4,0],(2*(y+.5)/h-1)/a[4,1],-np.ones((h,w))),axis=-1)
basis=a[1:4,:3]
world_ray=np.einsum("ij,hwj->hwi",basis,view_ray)

def depth_texture(depth,near=.1,far=1000):
    # Native fragment coordinates and depth storage both start at the top.
    pixels=np.flipud((depth-near)/(far-near)).astype('f4')
    texture=ctx.texture((w,h),1,pixels.tobytes(),dtype='f4');texture.use(0)
    return texture
def scene(occluded=False,data=None):
    camera_eye=eye if data is None else data[1:4,3]
    rays=world_ray if data is None else np.einsum('ij,hwj->hwi',data[1:4,:3],view_ray)
    ground=np.divide(-2-camera_eye[1],rays[:,:,1],out=np.full((h,w),1000.),where=rays[:,:,1]<-.0001)
    ground=np.where(ground>0,ground,1000.)
    world=camera_eye+rays*ground[:,:,None]
    bg=np.empty((h,w,4),dtype="f4");bg[:]=[.035,.1,.14,1]
    grid=(np.mod(np.floor(world[:,:,0])+np.floor(world[:,:,2]),2)*.02+.06)[:,:,None]*np.array([.8,1,1.1])
    bg[:,:,:3]=np.where((ground<1000)[:,:,None],grid,bg[:,:,:3])
    blocker=(x>w*.47)&(x<w*.53)&(y>h*.3)&(y<h*.68) if occluded else np.zeros((h,w),bool)
    depth=np.where(blocker,2,ground)
    bg[blocker]=[.35,.17,.1,1]
    return bg,depth,blocker
grid=np.zeros((64,64),dtype="f4")
grid[:,40:]=20000;grid_ubo.write(grid.tobytes())
curves=[json.loads(line[6:]) for line in (args.reference/"animation-curves.txt").read_text().splitlines() if line.startswith("CURVE ")][:2]
skeleton=json.loads((args.reference/"original-skeleton.json").read_text(encoding="utf-8-sig"))
def pose(frame):
    def value(curve):
        keys=np.array(curve['Keys']).reshape(-1,4);frames=curve['Frames'];i=min(np.searchsorted(frames,frame,side='right')-1,len(frames)-1)
        t=(frame-frames[i])/(frames[i+1]-frames[i]) if i+1<len(frames) else 0
        return float(np.polynomial.polynomial.polyval(t,keys[i])*curve['Scale']+curve['Offset']['Single'])
    scales=np.array([value(curves[0]),value(curves[1]),value(curves[0])]);models=[np.eye(4)];rows=[]
    # Independent full hierarchy/inverse-bind calculation, rather than the native shortcut.
    for i,bone in enumerate(skeleton['Bones'][1:],1):
        local=np.eye(4);local[:3,:3]=np.diag(scales/(scales if i>1 else 1));local[:3,3]=[float(n) for n in bone['Position'].split(';')]
        model=models[bone['ParentIndex']]@local;models.append(model)
        bind_y=sum(float(skeleton['Bones'][j]['Position'].split(';')[1]) for j in range(i+1))
        inverse=np.eye(4);inverse[1,3]=-bind_y;skin=model@inverse
        rows.append([skin[0,0],skin[1,1],skin[1,3],0])
    return np.array([[1,1,0,0]]+rows,dtype='f4')
rendered={}
def render(name,preview=False,water_on=False,height=4,occluded=False,breakage=False,selected=False,yaw=0,scale=.9,elapsed=90,camera=None):
    data=a.copy()
    if camera is not None:
        camera_eye=np.array(camera,dtype='f4');back=camera_eye-np.array([0,1.6,0],dtype='f4');back/=np.linalg.norm(back)
        right=np.cross([0,1,0],back);right/=np.linalg.norm(right);up=np.cross(back,right)
        data[1:4,:3]=np.stack((right,up,back),axis=1);data[1:4,3]=camera_eye
    bg,depth,blocker=scene(occluded,data)
    texture=depth_texture(depth)
    target.write(bg.tobytes());fbo.use();ctx.viewport=(0,0,w,h)
    ctx.enable(moderngl.BLEND);ctx.blend_func=(moderngl.ONE,moderngl.ONE_MINUS_SRC_ALPHA,moderngl.ZERO,moderngl.ONE)
    if water_on:
        marked=data.copy();marked[7]=[0,0,0,1]
        ubo.write(marked.tobytes());water_vao.render(moderngl.TRIANGLES,vertices=63*63*6)
        marked[15,3]=0
        ubo.write(marked.tobytes());water_vao.render(moderngl.TRIANGLES,vertices=6)
    u=data.copy();u[5,3]=float(preview);u[6,3]=float(selected);u[8:15,2]=height-4
    u[6,0]=elapsed
    u[16]=[np.cos(yaw),np.sin(yaw),scale,0];u[5,1]=-.18
    if not preview:
        frame=height*25
        u[8:15]=pose(frame)
        start,end=pose(0)[1,1],pose(100)[1,1]
        progress=np.clip((u[9,1]-start)/(end-start),0,1)
        u[5,1]-=.4696*(1-progress)
    ubo.write(u.tobytes())
    if not breakage:ice_vao.render(moderngl.TRIANGLES,vertices=len(mesh["indices"]))
    if not preview:
        top=max(0,-.6496+4.0696*progress)
        f=data.copy();f[16]=u[16];f[7]=[8 if breakage else height*25,0,top,2 if breakage else 0];f[6,1]=height*25
        ubo.write(f.tobytes());effects_vao.render(moderngl.TRIANGLES,vertices=48*6)
        if height<4 and not breakage:f[7,3]=1;ubo.write(f.tobytes());effects_vao.render(moderngl.TRIANGLES,vertices=48*6)
    pixels=np.frombuffer(fbo.read(components=4,dtype="f4"),dtype="f4").reshape(h,w,4)
    assert np.isfinite(pixels).all(),name
    if occluded:assert np.array_equal(pixels[blocker],bg[blocker]),"foreground box must occlude every effect"
    changed=np.linalg.norm(pixels[:,:,:3]-bg[:,:,:3],axis=-1)>.02
    assert changed.sum()>(40 if breakage else 500),"effect missing: "+name
    encoded=np.uint8(np.clip(np.flipud(pixels[:,:,:3]),0,1)**(1/2.2)*255)
    Image.fromarray(encoded).save(args.output/(name+".png"))
    texture.release()
    rendered[name]=pixels.copy()
    return int(changed.sum())
counts={}
for name,preview,on,height,occluded in (("selection",True,True,2.5,False),("constructed",False,False,4,False),
                                        ("growing",False,False,2,False),("occluded",True,True,2.5,True)):
    counts[name]=render(name,preview,on,height,occluded)
counts['shatter']=render('shatter',False,False,4,False,True)
counts['selected-pillar']=render('selected-pillar',selected=True)
counts['selected-pillar-occluded']=render('selected-pillar-occluded',occluded=True,selected=True)
counts['hover-cleared']=render('hover-cleared')
counts['preview-turned']=render('preview-turned',preview=True,height=4,yaw=np.pi/4)
counts['tower-turned']=render('tower-turned',yaw=np.pi/4)
counts['tower-turned-occluded']=render('tower-turned-occluded',yaw=np.pi/4,occluded=True)
counts['original-size-baseline']=render('original-size-baseline',preview=True,height=4,scale=1)
counts['90-percent-preview']=render('90-percent-preview',preview=True,height=4)
bg,_,_=scene()
def horizontal_span(name):
    changed=np.linalg.norm(rendered[name][:,:,:3]-bg[:,:,:3],axis=-1)>.02
    columns=np.nonzero(changed)[1]
    return columns.max()-columns.min()+1
ratio=horizontal_span('90-percent-preview')/horizontal_span('original-size-baseline')
assert .85<ratio<.95,('actual mesh must shrink to roughly 90 percent in projection',ratio)
assert horizontal_span('preview-turned')>horizontal_span('90-percent-preview'),'45 degree view must show the rotated square width'
assert np.array_equal(rendered['constructed'],rendered['hover-cleared']),'clearing hover must restore the exact accepted ice shading'
hover_delta=np.linalg.norm(rendered['selected-pillar'][:,:,:3]-rendered['constructed'][:,:,:3],axis=-1)
assert (hover_delta>.06).sum()>3000,'selected pillar must visibly differ from normal ice'
bg,_,_=scene()
untouched=np.all(rendered['constructed']==bg,axis=-1)
assert np.array_equal(rendered['selected-pillar'][untouched],bg[untouched]),'hover glow must not paint a box outside the pillar'

counts['interior-time-zero']=render('interior-time-zero',elapsed=0)
counts['interior-time-three-seconds']=render('interior-time-three-seconds',elapsed=90)
assert np.array_equal(rendered["interior-time-zero"],rendered["interior-time-three-seconds"]),"settled ice must have no moving interior"
for angle,camera in (("above",[4,9,8]),("distant",[6,8,20])):
    zero="interior-"+angle+"-zero";later="interior-"+angle+"-three-seconds"
    counts[zero]=render(zero,elapsed=0,camera=camera);counts[later]=render(later,elapsed=90,camera=camera)
    assert np.array_equal(rendered[zero],rendered[later]),"settled ice must remain static at every fixture angle"

def submerged_ice_depth(name,foreground=False,preview=False,ripple=False):
    bg,_,_=scene()
    water_y=.006*np.sin(x*.13)*np.cos(y*.11) if ripple else np.zeros((h,w))
    wet_depth=np.divide(water_y-eye[1],world_ray[:,:,1],out=np.full((h,w),1000.),where=world_ray[:,:,1]<-.0001)
    wet_depth=np.where(wet_depth>0,wet_depth,1000.)
    # Independent front-plane geometry identifies submerged pixels, without
    # reproducing the shader's water-depth exemption.
    front_t=(1.35-eye[2])/world_ray[:,:,2]
    front=eye+world_ray*front_t[:,:,None]
    strip=(front[:,:,1]<-.045)&(front[:,:,1]>-.135)&(abs(front[:,:,0])<1.05)&(front_t>0)
    blocker=(x>w*.48)&(x<w*.52)&(y>h*.15)&(y<h*.65) if foreground else np.zeros((h,w),bool)
    if foreground:assert (strip&blocker).sum()>200,'foreground fixture must actually cross submerged ice'
    bg[blocker]=[.35,.17,.1,1]
    data=a.copy();data[5]=[0,-.18,0,float(preview)];data[8:15]=pose(100);data[16]=[1,0,.9,0]
    def draw(depth):
        texture=depth_texture(depth);target.write(bg.tobytes());fbo.use();ubo.write(data.tobytes())
        ice_vao.render(moderngl.TRIANGLES,vertices=len(mesh['indices']))
        pixels=np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4).copy()
        texture.release();return pixels
    clear=draw(np.full((h,w),1000.))
    pixels=draw(np.where(blocker,2,wet_depth))
    eligible=strip&~blocker&(np.linalg.norm(clear[:,:,:3]-bg[:,:,:3],axis=-1)>.02)
    assert eligible.sum()>300,'fixture must exercise a visible underwater strip'
    visible=np.linalg.norm(pixels[:,:,:3]-bg[:,:,:3],axis=-1)>.02
    fraction=visible[eligible].mean()
    assert fraction>.99,('water must reveal the submerged geometry',name,fraction)
    assert np.array_equal(pixels[blocker],bg[blocker]),'closer foreground must still hide submerged ice'
    Image.fromarray(np.uint8(np.clip(np.flipud(pixels[:,:,:3]),0,1)**(1/2.2)*255)).save(args.output/(name+'.png'))
    return dict(submerged_pixels=int(eligible.sum()),visible_fraction=float(fraction),foreground_pixels_painted=0)

counts['submerged-solid']=submerged_ice_depth('submerged-solid')
counts['submerged-preview']=submerged_ice_depth('submerged-preview',preview=True)
counts['submerged-ripple']=submerged_ice_depth('submerged-ripple',ripple=True)
counts['submerged-foreground']=submerged_ice_depth('submerged-foreground',foreground=True)

def marker_without_grid():
    bg,depth,_=scene();target.write(bg.tobytes());fbo.use()
    texture=depth_texture(depth)
    grid_ubo.write(np.full((64,64),20000,dtype='f4').tobytes())
    data=a.copy();data[7]=[0,0,0,1];data[15,3]=0
    # Reproduce the native default that previously hid the downward-wound quad.
    ctx.enable(moderngl.CULL_FACE);ctx.front_face='ccw';ctx.cull_face='back'
    ubo.write(data.tobytes());water_vao.render(moderngl.TRIANGLES,vertices=6)
    rejected=np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4)
    assert np.array_equal(rejected,bg),'legacy back-face state must reproduce the missing marker'
    ctx.disable(moderngl.CULL_FACE)
    ubo.write(data.tobytes());water_vao.render(moderngl.TRIANGLES,vertices=6)
    pixels=np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4)
    changed=np.linalg.norm(pixels[:,:,:3]-bg[:,:,:3],axis=-1)>.02
    assert 30<changed.sum()<1500,'surface marker must survive an empty coarse water grid'
    # Independent projection of the water-surface center; no pillar-side transform.
    view=basis.T@(-eye+np.array([0,.035,0]));point=np.array([a[4,0]*view[0],a[4,1]*view[1]])/(-view[2])
    expected=(point*.5+.5)*np.array([w,h]);ys,xs=np.where(changed)
    assert np.linalg.norm(np.array([xs.mean(),ys.mean()])-expected)<3,'marker must center on the water surface'
    assert xs.max()-xs.min()>2*(ys.max()-ys.min()),'surface glyph must foreshorten with the water plane'
    encoded=np.uint8(np.clip(np.flipud(pixels[:,:,:3]),0,1)**(1/2.2)*255)
    Image.fromarray(encoded).save(args.output/'surface-marker-empty-grid.png');texture.release()
    return int(changed.sum())
counts['surface-marker-empty-grid']=marker_without_grid()

def shoreline_and_distance(name,far_view=False):
    # Independent analytic coast and opaque-depth mask. Land/rocks occlude
    # water at sub-cell precision; the CPU samples only the wet grid corners.
    data=a.copy();camera_eye=np.array([0,80,200] if far_view else [0,4,10],dtype='f4')
    # Deliberately retain an obsolete wide grid in the distant case: the
    # fragment cutoff must reject false planes even before a grid refresh.
    reach=1064 if far_view else 375
    data[1:4,3]=camera_eye;data[15]=[0,0,reach,64]
    rays=np.einsum('ij,hwj->hwi',data[1:4,:3],view_ray)
    distance=np.divide(-camera_eye[1],rays[:,:,1],out=np.full((h,w),1000.),where=rays[:,:,1]<-.0001)
    world=camera_eye+rays*distance[:,:,None]
    coast=lambda z:4+2.8*np.sin(z*.35)
    wet=(world[:,:,0]<coast(world[:,:,2]))&(distance<999)
    # A small dry island deliberately falls inside a wholly wet sample cell.
    island=(world[:,:,0]**2+(world[:,:,2]+3)**2)<.6**2
    wet&=~island
    # Dry bank is .35 m above the water; a fixed view-depth offset would shrink to zero
    # world height at distance and blur the surface separation with land coverage.
    bank_delta=.35/np.maximum(-rays[:,:,1],.0001)
    depth=np.where(distance<999,distance+np.where(wet,2.,-bank_delta),1000.)
    bg=np.empty((h,w,4),dtype='f4');bg[:]=[.27,.19,.09,1];bg[wet]=[.04,.12,.15,1]
    t=np.arange(64,dtype='f4')-31.5
    coordinates=2*t+np.sign(t)*(reach-63)*(np.maximum(np.abs(t)-15.5,0)/16)**4
    xx,zz=np.meshgrid(coordinates,coordinates)
    samples=np.where((xx<coast(zz))&((xx**2+(zz+3)**2)>=.6**2),0,20000).astype('f4')
    grid_ubo.write(samples.tobytes())
    texture=depth_texture(depth)
    target.write(bg.tobytes());fbo.use();ubo.write(data.tobytes());water_vao.render(moderngl.TRIANGLES,vertices=63*63*6)
    pixels=np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4)
    changed=np.linalg.norm(pixels[:,:,:3]-bg[:,:,:3],axis=-1)>.02
    assert np.isfinite(pixels).all(),name
    dry_changed=changed&~wet
    assert not dry_changed.any(),('water overlay painted dry land',int(dry_changed.sum()),
        [float(distance[dry_changed].min()),float(distance[dry_changed].max())],
        [float(world[:,:,0][dry_changed].min()),float(world[:,:,0][dry_changed].max())])
    if far_view:
        region=wet&(world[:,:,2]<-400)&(world[:,:,2]>-700)&(world[:,:,0]<-10)
        assert region.sum()>1000,'distant fixture must exercise the old wide grid'
        assert not changed[region].any(),'distant water planes must be cut off even with stale wide samples'
        river=wet&(np.linalg.norm(world,axis=-1)>100)&(np.linalg.norm(world,axis=-1)<250)&(world[:,:,0]<-10)
        assert river.sum()>1000,'fixture must exercise mid-river water beyond the old cutoff'
        assert changed[river].mean()>.98,'mid-river highlighting must remain fully visible before the new fade'
    else:
        region=wet&(world[:,:,0]>coast(world[:,:,2])-1.25)&(world[:,:,2]>-12)&(world[:,:,2]<5)
        assert region.sum()>1000,'shoreline fixture must exercise mixed wet/dry grid cells'
        assert changed[region].mean()>.98,'wet shore strip must not disappear with its dry grid corners'
        assert island.sum()>50,'depth-only island must be visible'
    Image.fromarray(np.uint8(np.clip(np.flipud(pixels[:,:,:3]),0,1)**(1/2.2)*255)).save(args.output/(name+'.png'))
    texture.release()
    return {'wet_pixels':int(region.sum()),'highlight_fraction':float(changed[region].mean()),'dry_pixels_painted':int(changed[~wet].sum())}
counts['curved-shore']=shoreline_and_distance('curved-shore')
counts['distant-cutoff']=shoreline_and_distance('distant-cutoff',True)

def water_surface_depth(name,ripple=False):
    # Reproduce the missing case: scene depth may be the water itself, not its
    # submerged floor. Use the reported 25 km far plane and nonzero world origin.
    data=a.copy();data[0,3]=25000;data[1:4,3]=[218,119.004196,1550]
    data[15]=[218,1540,375,64]
    water_y=np.float32(115.004196)
    data[5]=[218,water_y,1540,0] # Actual Link position drives the distance fade.
    rays=np.einsum('ij,hwj->hwi',data[1:4,:3],view_ray)
    distance=np.divide(float(water_y-data[2,3]),rays[:,:,1],out=np.full((h,w),25000.),where=rays[:,:,1]<-.0001)
    world=data[1:4,3]+rays*distance[:,:,None]
    displacement=.008*np.sin(world[:,:,0]*.8)*np.cos(world[:,:,2]*.6) if ripple else np.zeros((h,w))
    surface_depth=distance-displacement/np.maximum(-rays[:,:,1],.0001)
    blocker=(x>w*.43)&(x<w*.56)&(y>h*.25)&(y<h*.69)
    depth=np.where(blocker,2.,np.minimum(surface_depth,25000.))
    texture=depth_texture(depth,far=25000)
    grid_ubo.write(np.full((64,64),water_y,dtype='f4').tobytes())
    bg=np.empty((h,w,4),dtype='f4');bg[:]=[.04,.12,.15,1];bg[blocker]=[.28,.2,.1,1]
    radius=np.linalg.norm(world-data[5,:3],axis=-1)
    region=(distance>8)&(radius<55)&~blocker
    assert region.sum()>100000,'coplanar fixture must expose a broad visible water surface'
    def draw(vao):
        target.write(bg.tobytes());fbo.use();ubo.write(data.tobytes());vao.render(moderngl.TRIANGLES,vertices=63*63*6)
        pixels=np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4)
        assert np.isfinite(pixels).all(),name
        changed=np.linalg.norm(pixels[:,:,:3]-bg[:,:,:3],axis=-1)>.02
        assert not changed[blocker].any(),'water must remain hidden behind foreground geometry'
        return pixels,float(changed[region].mean())
    old_fraction=None
    if baseline_vao:
        _,old_fraction=draw(baseline_vao)
        assert old_fraction<.8,'retained v0.1.4 shader must reproduce broad water loss'
    pixels,fraction=draw(water_vao)
    assert fraction>.995,('water surface must not occlude its own highlight',fraction)
    # Cutoff measures the rendered highlight, .025 m above native water. At
    # grazing angles that offset moves the ray intersection by several metres.
    overlay_distance=np.divide(float(water_y+.025-data[2,3]),rays[:,:,1],out=np.full((h,w),25000.),where=rays[:,:,1]<-.0001)
    overlay_world=data[1:4,3]+rays*overlay_distance[:,:,None]
    distant=(np.linalg.norm(overlay_world-data[5,:3],axis=-1)>376)&~blocker
    assert distant.sum()>10000,'fixture must include water beyond the cutoff'
    assert np.array_equal(pixels[distant],bg[distant]),'far water must remain unpainted'
    # Bright native reflection versus its absence, on the same water pixels.
    # This measures actual compositing, rather than checking an alpha literal.
    reflection=region&(x>w*.2)&(x<w*.8)
    assert reflection.sum()>10000,'fixture must expose a broad nearby reflection'
    bg[reflection,:3]+=.3
    with_reflection,_=draw(water_vao)
    transmission=(with_reflection[reflection,:3]-pixels[reflection,:3])/.3
    assert transmission.min()>.71 and transmission.max()<.73,'native reflections must retain about 72 percent of their contrast'
    Image.fromarray(np.uint8(np.clip(np.flipud(pixels[:,:,:3]),0,1)**(1/2.2)*255)).save(args.output/(name+'.png'))
    texture.release()
    return {'wet_pixels':int(region.sum()),'highlight_fraction':fraction,'rejected_candidate_fraction':old_fraction,'foreground_pixels_painted':0,'reflection_transmission':float(transmission.mean()),'distant_pixels_unpainted':int(distant.sum())}
counts['coplanar-water']=water_surface_depth('coplanar-water')
counts['rippled-water']=water_surface_depth('rippled-water',True)

def mixed_water_levels():
    bg,depth,_=scene();texture=depth_texture(depth)
    def draw(samples):
        grid_ubo.write(samples.astype('f4').tobytes());target.write(bg.tobytes());fbo.use();ubo.write(a.tobytes())
        water_vao.render(moderngl.TRIANGLES,vertices=63*63*6)
        return np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4).copy()
    flat=draw(np.zeros((64,64)))
    gy,gx=np.mgrid[0:64,0:64]
    mixed=draw(((gx+gy)%2)*2.)
    assert (np.linalg.norm(flat[:,:,:3]-bg[:,:,:3],axis=-1)>.02).sum()>100000,'same-height control must show water'
    assert np.array_equal(mixed,bg),'different water levels cannot create bridging planes'
    texture.release()
    return dict(same_height_visible=True,mixed_height_pixels_painted=0)
counts['separate-water-levels']=mixed_water_levels()

def floating_plane_cutoff():
    # A false elevated plane whose depth test would pass against the sky.
    data=a.copy();data[1:4,3]=[0,80,200];data[15]=[0,0,1064,64]
    texture=depth_texture(np.full((h,w),1000.))
    grid_ubo.write(np.full((64,64),30,dtype='f4').tobytes())
    bg=np.empty((h,w,4),dtype='f4');bg[:]=[.04,.12,.15,1]
    def draw(link):
        data[5,:3]=link;target.write(bg.tobytes());fbo.use();ubo.write(data.tobytes())
        water_vao.render(moderngl.TRIANGLES,vertices=63*63*6)
        return np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4).copy()
    control=draw([0,30,150])
    distant=draw([0,0,0])
    assert (np.linalg.norm(control[:,:,:3]-bg[:,:,:3],axis=-1)>.02).sum()>1000,'fixture must actually render the false elevated plane in its control'
    rays=np.einsum('ij,hwj->hwi',data[1:4,:3],view_ray)
    depth=np.divide(30-data[2,3],rays[:,:,1],out=np.full((h,w),1000.),where=rays[:,:,1]<-.0001)
    plane=data[1:4,3]+rays*depth[:,:,None]
    outside=(np.linalg.norm(plane,axis=-1)>376)&(depth>0)&(depth<999)
    assert outside.sum()>1000,'fixture must include distant floating coverage beyond the expanded cutoff'
    assert np.array_equal(distant[outside],bg[outside]),'cutoff must reject distant floating planes even when sky depth passes'
    texture.release()
    return dict(control_plane_visible=True,distant_floating_pixels_painted=0)
counts['floating-plane-cutoff']=floating_plane_cutoff()
def render_hud(name,aiming,cooldown):
    bg,_,_=scene();target.write(bg.tobytes());fbo.use()
    data=a.copy();data[5,3]=float(aiming);data[6,2]=cooldown
    ubo.write(data.tobytes());hud_vao.render(moderngl.TRIANGLES,vertices=3)
    pixels=np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4)
    assert np.isfinite(pixels).all(),name
    changed=np.linalg.norm(pixels[:,:,:3]-bg[:,:,:3],axis=-1)>.02
    if aiming or cooldown:assert 100<changed.sum()<10000,name
    else:assert np.array_equal(pixels,bg),'inactive HUD must leave the framebuffer unchanged'
    if aiming:
        px=int(w*.595);py=int(h*140/720)
        assert changed[py-10:py+10,px-10:px+10].sum()>250,'A must be near the lower right with native fragment origin'
        # A's dark apex is above its crossbar in bottom-origin framebuffer storage.
        apex=pixels[py+6,px,:3].mean();left=pixels[py+6,px-5,:3].mean()
        assert apex<left*.5,'A must point upward'
    encoded=np.uint8(np.clip(np.flipud(pixels[:,:,:3]),0,1)**(1/2.2)*255)
    Image.fromarray(encoded).save(args.output/(name+'.png'))
    return int(changed.sum())
for name,aiming,cooldown in [('hud-aim',True,0),('hud-cooldown',False,25),('hud-empty',False,0)]:
    counts[name]=render_hud(name,aiming,cooldown)
(args.output/"fixture-results.json").write_text(json.dumps({"counts":counts,"mesh_vertices":587,"indices":2124,"foreground_occlusion":"pass"},indent=2)+"\n")
print(json.dumps(counts))
