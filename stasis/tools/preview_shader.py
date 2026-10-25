"""SPDX-License-Identifier: MIT. Copyright (c) Clay Mullis.
Render the integrated native multipass GLSL against independent scene/mask fixtures.
No game assets. The GL framebuffer origin is adapted explicitly; native bindings,
geometry, blend equations, ordinary draw counts and filtered bloom match the adapter.
"""
from pathlib import Path
import argparse,hashlib,json,importlib.util
import moderngl
import numpy as np
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=root/'build'/'shader-preview-native')
parser.add_argument('--width',type=int,default=960)
parser.add_argument('--height',type=int,default=640)
parser.add_argument('--mask-scale',type=float,default=1)
parser.add_argument('--native-origin',action='store_true')
parser.add_argument('--geometry-baseline',type=Path,help='Optional expanded vertex shaders from a retained build to compare on GPU')
args=parser.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('compiler',root/'tools/compile_shaders.py')
compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
ctx=moderngl.create_standalone_context(require=450)
def shader(name):
    source=compiler.expanded_source(root/'shaders'/name)
    return source if args.native_origin else source.replace('#version 450','#version 450\n#define STASIS_PREVIEW')
programs={}
for name,vertex,fragment in [('surface','stasis.vert','stasis.frag'),('burst','stasis.vert','native-burst.frag'),
    ('blur','stasis.vert','native-blur.frag'),('downsample','stasis.vert','native-downsample.frag'),('composite','stasis.vert','native-composite.frag')]+[
    (name,'native-'+name+'.vert','native-'+name+'.frag') for name in ['chain','arrow','particles']]:
    program=ctx.program(vertex_shader=shader(vertex),fragment_shader=shader(fragment))
    programs[name]=ctx.vertex_array(program,[])
w,h=args.width,args.height
gw,gh=800,450
sizes=[(w,h),(gw,gh),((gw+1)//2,(gh+1)//2),((gw+1)//2,(gh+1)//2),((gw+3)//4,(gh+3)//4),((gw+3)//4,(gh+3)//4)]
textures=[ctx.texture(size,4,dtype='f2') for size in sizes]
mrt=ctx.framebuffer([textures[0],ctx.texture((w,h),4,dtype='f2')])
glowMrt=ctx.framebuffer([ctx.texture((gw,gh),4,dtype='f2'),textures[1]])
fbos={i:ctx.framebuffer([t]) for i,t in enumerate(textures)}
finaltex=ctx.texture((w,h),4,dtype='f4');final=ctx.framebuffer([finaltex])
ubo=ctx.buffer(reserve=153*16);ubo.bind_to_uniform_block(0)
y,x=np.mgrid[0:h,0:w]
px,py=1.05,1.575
ray=np.stack(((2*(x+.5)/w-1)/px,(1-2*(y+.5)/h)/py,-np.ones((h,w))),axis=-1)
centre=np.array([-.25,.15,-6.0]);radii=np.array([1.3,1.3,1.3])
def project(pos):
    z=-pos[2];return np.array([(px*pos[0]/z*.5+.5)*w,(.5-py*pos[1]/z*.5)*h,z,1.0],dtype='f4')
def fixtures(irregular=False,occluded=False,empty=False,preview_pair=False,overlap=False):
    global expected_hover,expected_preview
    scaled=ray/radii;origin=-centre/radii
    a=(scaled**2).sum(-1);b=2*(scaled*origin).sum(-1);c=(origin**2).sum()-1
    disc=b*b-4*a*c
    nearest=(-b-np.sqrt(np.maximum(disc,0)))/(2*a)
    mask=(disc>0)&(nearest>0)
    selected_mask=mask.copy();selected_nearest=nearest.copy()
    if preview_pair:
        other_origin=-(centre+np.array([1.7,0,.2] if overlap else [3.2,0,0]))/radii
        other_b=2*(scaled*other_origin).sum(-1);other_c=(other_origin**2).sum()-1
        other_disc=other_b*other_b-4*a*other_c
        other_nearest=(-other_b-np.sqrt(np.maximum(other_disc,0)))/(2*a)
        other_mask=(other_disc>0)&(other_nearest>0)
        use_other=other_mask&(~mask|(other_nearest<nearest))
        nearest=np.where(use_other,other_nearest,nearest);mask|=other_mask
    if irregular:
        cut=~((x>w*.5)&(y<h*.41));mask&=cut;selected_mask&=cut
    floor=np.divide(-1.5,ray[:,:,1],out=np.full((h,w),20.0),where=ray[:,:,1]<-.0001)
    depth=np.minimum(floor,20)
    mask&=nearest<depth
    depth=np.where(mask,nearest,depth)
    hidden=mask&(x>w*.40)&(x<w*.53)&(y>h*.33)&(y<h*.58) if occluded else np.zeros_like(mask)
    depth=np.where(hidden,2.0,depth)
    world=ray*depth[:,:,None]
    normal=(world-centre)/radii;normal/=np.maximum(np.linalg.norm(normal,axis=-1,keepdims=True),.001)
    light=np.clip((normal*np.array([-.5,.7,.5])).sum(-1),0,1)
    bg=np.zeros((h,w,3))+[.035,.11,.15]
    floorcolor=(np.mod(np.floor(world[:,:,0]*2)+np.floor(world[:,:,2]*2),2)*.035+.11)[:,:,None]*np.array([.7,1,1.05])
    bg=np.where((floor<20)[:,:,None],floorcolor,bg)
    material=np.array([.18,.14,.09])*(.45+.8*light[:,:,None])
    bg=np.where(mask[:,:,None],material,bg)
    depthtex=ctx.texture((w,h),1,((depth-.1)/999.9).astype('f4').tobytes(),dtype='f4');depthtex.use(0)
    mw,mh=max(1,int(w*args.mask_scale)),max(1,int(h*args.mask_scale))
    rows=np.minimum(((np.arange(mh)+.5)*h/mh).astype(int),h-1)
    cols=np.minimum(((np.arange(mw)+.5)*w/mw).astype(int),w-1)
    # Independently ray traced all-model and selected-only depth passes, zero clear.
    raw=np.where(mask&(not empty),(nearest-.1)/999.9,0)
    maskDepth=ctx.texture((mw,mh),1,raw[np.ix_(rows,cols)].astype('f4').tobytes(),dtype='f4');maskDepth.use(1)
    selected_raw=np.where(selected_mask&(not empty),(selected_nearest-.1)/999.9,0)
    hoverDepth=ctx.texture((mw,mh),1,selected_raw[np.ix_(rows,cols)].astype('f4').tobytes(),dtype='f4');hoverDepth.use(5)
    expected_hover=mask&~hidden&selected_mask&(nearest==selected_nearest)
    expected_preview=mask&~hidden&~expected_hover
    return mask&~hidden,bg,world,(depthtex,maskDepth,hoverDepth)
def uniforms(frozen=False,charged=False):
    a=np.zeros((153,4),dtype='f4');a[0]=[w,h,.1,1000]
    a[52,:2]=[max(1,int(w*args.mask_scale)),max(1,int(h*args.mask_scale))]
    a[52,2]=1
    a[1]=project(centre);a[1,3]=w*px*.5/6*.85
    a[55,:2]=[radii[0]*w*px*.5/6,radii[1]*h*py*.5/6]
    a[4]=[0,int(frozen),.25,int(True)];a[5]=[.7,1.7,0,0];a[53]=[1.7,-1,0,0]
    a[6]=[1,.60,.03,0];a[7]=[210,0,w*px*.5/6,0]
    a[8]=[1,0,0,0];a[9]=[0,1,0,0];a[10]=[0,0,1,0];a[11]=[px,py,0,0]
    a[12]=[*list(centre-radii),1];a[13]=[*list(centre+radii),1]
    a[30,3]=-centre[2]-radii[2]
    if frozen:
        for i,d in enumerate([[1,.18,.2],[-1,-.18,-.2],[.2,1,.7],[-.2,-1,-.7],[.2,-.65,1],[-.2,.65,-1]]):
            d=np.array(d);d=d/np.linalg.norm(d)*12
            if centre[2]+d[2]>-.2:d*=(-.2-centre[2])/d[2]
            a[46+i]=project(centre+d)
            a[46+i,3]=np.linalg.norm(d)
    if charged:
        begin=centre
        direction=np.array([.65,.258819,-.7]);direction/=np.linalg.norm(direction)
        sx,sz=(.32+.24*a[4,2])/.33875746,(.90+5.3*a[4,2])/2.9643395
        end=begin+direction*2.9643395*sz
        right=np.array([direction[2],0,-direction[0]]);right/=np.linalg.norm(right)
        up=np.cross(direction,right)
        a[14]=[*begin,sx];a[15]=[*right,sz];a[16]=[*up,0];a[17]=[*direction,0]
        a[2]=project(begin);a[2,3]=.33875746*sx*w*px*.5/6
        a[3]=project(end)
    return a

def read(fbo):
    return np.flipud(np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(h,w,4)).copy()
def bloom_passes(a):
    ctx.disable(moderngl.BLEND)
    halfW,halfH=sizes[2];wideW,wideH=sizes[4]
    passes=[(1,2,(gw,gh,2,0),'downsample'),(2,3,(halfW,halfH,gw/w,0),'blur'),(3,2,(halfW,halfH,0,gh/h),'blur'),
        (2,4,(halfW,halfH,2,0),'downsample'),(4,5,(wideW,wideH,gw/w,0),'blur'),(5,4,(wideW,wideH,0,gh/h),'blur'),
        (4,5,(wideW,wideH,gw/w,0),'blur'),(5,4,(wideW,wideH,0,gh/h),'blur')]
    for src,dst,parameters,program in passes:
        textures[src].use(2);fbos[dst].use();ctx.viewport=(0,0,*sizes[dst])
        copy=a.copy();copy[54]=parameters;ubo.write(copy.tobytes());programs[program].render(vertices=3)
def render(a,bloom=True,chains=True,arrow=True,burst=True,particles=True):
    drawObjects(a,chains,arrow,burst,particles)
    direct=read(fbos[0])
    glowMrt.use();ctx.viewport=(0,0,gw,gh);glowMrt.clear()
    drawObjects(a,chains,arrow,burst,particles,begin=False)
    bloom_passes(a)
    if not bloom:
        for i in [2,4]:fbos[i].use();fbos[i].clear()
    final.use();ctx.viewport=(0,0,w,h);final.clear()
    textures[0].use(2);textures[2].use(3);textures[4].use(4)
    composite=a.copy();composite[54]=[gw,gh,2,0];ubo.write(composite.tobytes())
    programs['composite'].render(vertices=3)
    image=read(final)+direct
    assert np.isfinite(image).all() and image.min()>=-.0001 and image.max()<32
    assert image[:,:,3].max()<=1.001
    return image,direct
def drawObjects(a,chains,arrow,burst,particles,begin=True):
    if begin: mrt.use();ctx.viewport=(0,0,w,h);mrt.clear()
    ctx.enable(moderngl.BLEND)
    ctx.blend_func=(moderngl.ONE,moderngl.ONE_MINUS_SRC_ALPHA,moderngl.ONE,moderngl.ONE_MINUS_SRC_ALPHA)
    ubo.write(a.tobytes());programs['surface'].render(vertices=3)
    if chains and (a[4,1]>.5 or a[5,3]>.24):programs['chain'].render(vertices=48*10*6*6*46)
    if a[4,1]>.5:
        if arrow and a[3,3]>.5:programs['arrow'].render(vertices=6)
        if particles and 0<=a[53,0]<1:programs['particles'].render(vertices=6*48)
        if burst:programs['burst'].render(vertices=3)
    if particles and a[53,2]>0:
        copy=a.copy();copy[53,3]=1;ubo.write(copy.tobytes())
        programs['particles'].render(vertices=6*int(a[53,2])*7)
checks=[];panels=[]
def save(name,image,bg):
    composite=np.maximum(image[:,:,:3]+bg*(1-image[:,:,3:4]),0)
    # Apply display processing once, after compositing with the synthetic HDR scene.
    composite=(composite*(2.51*composite+.03))/(composite*(2.43*composite+.59)+.14)
    composite=np.power(np.clip(composite,0,1),1/2.2)
    panel=Image.fromarray((composite*255).astype('uint8'))
    ImageDraw.Draw(panel).text((16,16),name+' / native GLSL, synthetic scene',fill=(220,235,240))
    panel.save(out/(name+'.png'));panels.append(panel)
# Two independent ray-traced surfaces share the native mask. Only the left one is selected.
mask,bg,world,borrowed=fixtures(preview_pair=True)
a=uniforms();a[4,0]=1;a[55,3]=1
image,direct=render(a,chains=False,arrow=False,burst=False,particles=False)
left=mask&(world[:,:,0]<.5)&(np.abs(world[:,:,1]-.15)<.5)
right=mask&(world[:,:,0]>2.3)&(np.abs(world[:,:,1]-.15)<.5)
hover_rgb=np.median(direct[left,:3],axis=0);preview_rgb=np.median(direct[right,:3],axis=0)
assert left.sum()>500 and right.sum()>500
assert hover_rgb[1]>hover_rgb[0] and preview_rgb[0]>preview_rgb[1]
assert hover_rgb[1]>preview_rgb[1]*3
save('aim-hover-and-preview',image,bg)
checks.append({'hover_rgb':hover_rgb.tolist(),'preview_rgb':preview_rgb.tolist(),
               'independent_visible_surfaces':2})
for texture in borrowed:texture.release()
# Overlap puts a neighboring surface inside the selected object's physics box. Shrinking
# that box also leaves most selected geometry outside it. Neither may affect the tint.
mask,bg,world,borrowed=fixtures(preview_pair=True,overlap=True)
a=uniforms();a[4,0]=1;a[55,3]=1
a[12,:3]=centre-.15;a[13,:3]=centre+.15
image,direct=render(a,chains=False,arrow=False,burst=False,particles=False)
def interior(m):
    return m&np.roll(m,4,0)&np.roll(m,-4,0)&np.roll(m,4,1)&np.roll(m,-4,1)
selected=interior(expected_hover);neighbor=interior(expected_preview)
assert selected.sum()>500 and neighbor.sum()>500
assert np.all(direct[selected,1]>direct[selected,0])
assert np.all(direct[neighbor,0]>direct[neighbor,1])
save('hover-full-model-overlapping-neighbor',image,bg)
checks.append({'overlap_selected_pixels':int(selected.sum()),'overlap_neighbor_pixels':int(neighbor.sum()),
               'physics_bounds_do_not_clip_or_spill_hover':True})
for texture in borrowed:texture.release()
for irregular in [False,True]:
    mask,bg,world,borrowed=fixtures(irregular)
    for name,frozen,charged in [('selected',False,False),('charged',True,True)]:
        a=uniforms(frozen,charged);image,direct=render(a)
        assert np.count_nonzero(mask&(np.max(direct[:,:,:3],axis=-1)>.55))>200
        if not frozen:
            probe=(~mask)&(y>h*.73)&(x>w*.30)&(x<w*.65)
            assert np.abs(direct[probe]).max()==0
        save(name+('-cut' if irregular else '-sphere'),image,bg)
    a=uniforms(True,True);a[53,0]=.13;image,_=render(a);save('activation'+('-cut' if irregular else '-sphere'),image,bg)
    a[53,0]=1;one,_=render(a);a[53,0]=2;two,_=render(a);assert np.array_equal(one,two)
    # Later hits must not emit object rays; the arrow's independent hit flash
    # is excluded from this comparison.
    a[5,2]=0;late,_=render(a,arrow=False)
    a[5,2]=1;lateHit,_=render(a,arrow=False);assert np.array_equal(late,lateHit)
    a=uniforms(True,True);on,_=render(a);off,_=render(a,bloom=False)
    changed=np.count_nonzero(np.max(np.abs(on-off),axis=-1)>.001);assert changed>1000
    checks.append({'fixture':'cut' if irregular else 'sphere','bloom_changed_pixels':int(changed),'activation_cutoff_equal':True,'later_hit_rays_zero':True})
    for t in borrowed:t.release()
for occluded,empty in [(True,False),(False,True)]:
    mask,bg,world,borrowed=fixtures(occluded=occluded,empty=empty)
    a=uniforms();a[12,:3]=centre-.15;a[13,:3]=centre+.15
    image,direct=render(a)
    probe=(x>w*.43)&(x<w*.50)&(y>h*.38)&(y<h*.53)
    assert np.abs(direct[probe]).max()==0
    if not empty:assert np.count_nonzero(mask&(np.max(direct[:,:,:3],axis=-1)>.55))>200
    else:
        probe=(x>w*.3)&(x<w*.7)&(y>h*.25)&(y<h*.75)
        assert np.abs(direct[probe]).max()==0
    checks.append({'fixture':'occluded_small_bounds' if occluded else 'empty_mask','direct_foreground_pixels':0})
    for t in borrowed:t.release()
mask,bg,world,borrowed=fixtures()
a=uniforms(True,False);a[53,0]=2
baseline,_=render(a,chains=False)
count=0
for lane in range(6):
    mrt.use();mrt.clear();ubo.write(a.tobytes());ctx.viewport=(0,0,w,h)
    ctx.enable(moderngl.BLEND)
    # Exercise the same shader and vertex ranges as the native ordinary draw.
    programs['chain'].render(vertices=2880*46,first=lane*2880*46)
    pixels=np.count_nonzero(np.max(read(fbos[0])[:,:,:3],axis=-1)>.05)
    assert pixels>50;count+=1
checks.append({'visible_chain_lanes':count})
a=uniforms();a[4]=0;a[13,3]=0;a[53]=[-1,1.0,30,1]
for i in range(30):a[57+i]=[*list(centre+np.array([i/30*2,i/30*.4,-i/30])),i/30]
image,direct=render(a);assert direct[:,:,:3].max()>.1;save('release-trail',image,bg)
a[53,1]=5;expired,_=render(a);probe=(x>w*.3)&(x<w*.8)&(y>h*.1)&(y<h*.8);assert np.abs(expired[probe]).max()==0
checks.append({'actual_supplied_path_particles':True,'expired_trail_zero':True})
for t in borrowed:t.release()

# Poison every private image before a normal frame; full coverage must remove all stale contents.
mask,bg,world,borrowed=fixtures()
a=uniforms(True,True)
reference,_=render(a)
for texture in textures[1:]:
    poison=np.zeros((texture.height,texture.width,4),dtype='f2')
    poison[:,:,:3]=[24,3,19]
    texture.write(poison.tobytes())
poisoned,_=render(a)
assert np.array_equal(reference,poisoned)
checks.append({'poisoned_private_storage_unchanged':True,'full_private_coverage':[gw,gh]})
for t in borrowed:t.release()

# A single filtered texel must form a smooth mound, not separated box lobes.
impulse=np.zeros((gh,gw,4),dtype='f2');cx,cy=(gw//8)*4,(gh//8)*4
impulse[cy:cy+4,cx:cx+4,:3]=1;textures[1].write(impulse.tobytes())
bloom_passes(uniforms())
wideW,wideH=sizes[4]
kernel=np.frombuffer(fbos[4].read(components=4,dtype='f4'),dtype='f4').reshape(wideH,wideW,4)[:,:,0]
ky,kx=np.unravel_index(kernel.argmax(),kernel.shape)
for line in (kernel[ky,kx:kx+13],kernel[ky,kx-12:kx+1][::-1],kernel[ky:ky+13,kx],kernel[ky-12:ky+1,kx][::-1]):
    assert np.all(np.diff(line)<=.000002)
assert abs(kernel.sum()-1)<.004
checks.append({'filtered_impulse_monotonic':True,'filtered_impulse_energy':float(kernel.sum())})

# An object like the reported sled must not inherit a spherical activation crown.
savedRadii=radii.copy();radii=np.array([2.17,.325,1.607])
mask,bg,world,borrowed=fixtures();a=uniforms(True);a[53,0]=.13
image,direct=render(a,chains=False,arrow=False,particles=False)
_,without=render(a,chains=False,arrow=False,particles=False,burst=False)
flash=np.max(np.abs(direct-without)[:,:,:3],axis=-1)>.05
assert np.count_nonzero(flash)>100
assert np.max(np.abs((y+.5-a[1,1])[flash]))<a[55,1]*1.72+2
save('activation-flat-sled',image,bg)
checks.append({'flat_object_flash_half_height':float(np.max(np.abs((y+.5-a[1,1])[flash]))),
               'flat_object_half_height':float(a[55,1])})
# Seven and eight hits in a test session produced .583/.667 charge, so test that range.
ratios=[]
for charge in (0,.333333,.583333,.666667,1):
    a=uniforms(True);a[4,2]=charge
    image,direct=render(a,chains=False,arrow=False,particles=False,burst=False)
    red=direct[:,:,:3][mask].sum(axis=0);ratios.append(float(red[1]/red[0]))
    save('flat-charge-'+str(round(charge*100)),image,bg)
assert np.all(np.diff(ratios)<0) and ratios[3]<.15*ratios[0]
checks.append({'surface_green_to_red_by_charge':ratios})
for t in borrowed:t.release()
radii=savedRadii

mask,bg,world,borrowed=fixtures();a=uniforms(True);a[53,0]=2
baseline,_=render(a,burst=False,particles=False,arrow=False)
# Exercise the large real-world coordinates absent from the original gallery.
offset=np.array([1817,219,1001],dtype='f4');moved=a.copy()
moved[8:11,3]=offset;moved[12:14,:3]+=offset
shifted,_=render(moved,burst=False,particles=False,arrow=False)
assert np.mean(np.abs(shifted-baseline))<.002
checks.append({'translated_world_mean_error':float(np.mean(np.abs(shifted-baseline)))})
# Release retains copied bounds for the links as they separate and fade away.
a[4]=0;a[13,3]=0;a[5,3]=.7
image,direct=render(a,particles=False);assert np.count_nonzero(direct[:,:,0]>.1)>500
save('chain-release-breakup',image,bg)
a[5,3]=.23;_,gone=render(a,particles=False)
probe=(x>w*.2)&(x<w*.8)&(y>h*.2)&(y<h*.8);assert np.max(np.abs(gone[probe]))==0
checks.append({'release_chain_breakup_visible':True,'expired_chains_zero':True})
for t in borrowed:t.release()
if args.geometry_baseline:
    # Compare every link vertex and trail particle with the retained integer-index version;
    # the native compiler guard rejects its IMAD instructions separately.
    a=uniforms(True);a[53]=[.20,1,96,0]
    for i in range(96):a[57+i]=[*list(centre+np.array([i*.05,i*.01,-i*.10])),i*.01]
    ubo.write(a.tobytes())
    for name,varyings,count,components in [('chain',['normal','world','fade'],2880*46*6,7),
                                          ('particles',['particleUv','age','energy','seed','world'],96*7*6,8)]:
        outputs=[]
        for source in [(args.geometry_baseline/(name+'.vert')).read_text(),shader('native-'+name+'.vert')]:
            program=ctx.program(vertex_shader=source,varyings=varyings)
            vao=ctx.vertex_array(program,[]);buffer=ctx.buffer(reserve=count*components*4)
            cases=[]
            for mode in ([0,1] if name=='particles' else [0]):
                a[53,3]=mode;ubo.write(a.tobytes())
                vao.transform(buffer,mode=moderngl.POINTS,vertices=count)
                cases.append(np.frombuffer(buffer.read(),dtype='f4').copy())
            outputs.append(np.concatenate(cases));buffer.release();vao.release();program.release()
        error=float(np.max(np.abs(outputs[0]-outputs[1])))
        assert np.isfinite(outputs[1]).all() and error<.0001
        vertices=outputs[1].reshape(-1,components)
        assert np.ptp(vertices[:,-4:-1] if name=='chain' else vertices[:,-3:],axis=0).max()>1
        if name=='chain':assert np.allclose(np.linalg.norm(vertices[:,:3],axis=1),1,atol=.0001)
        checks.append({'geometry':name,'compared_vertices':count,'reference_max_error':error})
# Native bloom receives the unchanged HDR surface, with HUD delayed until after bloom.
mask,bg,world,borrowed=fixtures(occluded=True)
split_errors=[]
for frozen in (False,True):
    a=uniforms(frozen);a[4,0]=1
    drawObjects(a,False,False,False,False);complete=read(fbos[0])
    scene=a.copy();scene[55,3]=1
    drawObjects(scene,False,False,False,False);world_only=read(fbos[0])
    hud=a.copy();hud[55,3]=2
    ubo.write(hud.tobytes());programs['surface'].render(vertices=3)
    joined=read(fbos[0]);error=float(np.max(np.abs(complete-joined)))
    assert error<.004 and np.max(world_only[:,:,:3])>1
    hud_region=(x<w*.17)&(y<h*.22)
    assert np.max(np.abs(world_only[hud_region]))==0
    drawObjects(hud,False,False,False,False);hud_only=read(fbos[0])
    assert np.max(np.abs(hud_only[mask]))==0
    split_errors.append(error)
for t in borrowed:t.release()
checks.append({'native_scene_hud_split_errors':split_errors,'hdr_emission_preserved':True,
               'native_game_bloom_not_simulated':True})
sheet=Image.new('RGB',(w*2,h*((len(panels)+1)//2)))
for i,panel in enumerate(panels):sheet.paste(panel,((i%2)*w,(i//2)*h))
sheet.save(out/'native-preview.png')
receipt={'renderer':ctx.info['GL_RENDERER'],'dimensions':[w,h],'glow_dimensions':[gw,gh],'mask_scale':args.mask_scale,
    'native_origin_sampling':args.native_origin,
    'stages':{n:hashlib.sha256(compiler.expanded_source(root/'shaders'/f).encode()).hexdigest() for n,s,f in compiler.STAGES},
    'checks':checks,'native_runtime_observed':False}
(out/'preview-checks.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'checks':checks,'output':str(out)}))
