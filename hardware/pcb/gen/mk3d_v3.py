# v3 mechanical models. KiCad model coords: X = footprint X, Y = -footprint Y (lens y up), Z = 0 at board top.
# Board 0.8 mm: bottom at -0.8, 0.1 mm tape, display back at -0.9.
import numpy as np, cadquery as cq
from cadquery import Color
from PIL import Image
from shapely.geometry import box as sbox
from shapely.ops import unary_union
OUT='/home/claude/v3/models/'
GLASS=Color(0.55,0.6,0.62); BEZEL=Color(0.30,0.31,0.31); LW=Color(0.80,0.81,0.78); LB=Color(0.11,0.12,0.13)
FLEX=Color(0.85,0.5,0.1); CELL=Color(0.78,0.79,0.81); PCM=Color(0.15,0.15,0.15); BLACK=Color(0.07,0.07,0.07)
STIFF=Color(0.2,0.45,0.25); LENS=Color(0.12,0.15,0.3); TIN=Color(0.7,0.7,0.72); WHITE=Color(0.92,0.92,0.9)
def B(x0,x1,y0,y1,z0,z1): return cq.Workplane('XY').box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0))
def save(name,parts):
    a=cq.Assembly(name=name)
    for i,(sh,c) in enumerate(parts): a.add(sh,name=f'{name}_{i}',color=c)
    a.save(OUT+name+'.step'); print('wrote',name)
# ---------------- display (origin = glass centre, lens (31.4, 21.41))
ZB,ZF=-0.9,-2.53; GX,GY=31.4,21.41; AX,AY=29.4,17.64; P=0.147
parts=[(B(-31.4,31.4,-21.41,21.41,ZF,ZB),GLASS),
       (B(-31.4,31.4,-21.41,21.41,ZF-0.012,ZF).cut(B(-AX,AX,-AY,AY,ZF-0.02,ZF+0.01)),BEZEL),
       (B(-AX,AX,-AY,AY,ZF-0.012,ZF),LW)]
img=np.array(Image.open('/home/claude/models/splash_400x240.png')); rects=[]
for v in range(240):
    row=~img[v]; u=0
    while u<400:
        if row[u]:
            u0=u
            while u<400 and row[u]: u+=1
            rects.append(sbox(AX-u*P,AY-(v+1)*P,AX-u0*P,AY-v*P))
        else: u+=1
g=unary_union(rects).simplify(1e-6); polys=list(g.geoms) if g.geom_type=='MultiPolygon' else [g]
def wire(cs): return cq.Wire.makePolygon([cq.Vector(x,y,0) for x,y in list(cs)[:-1]],close=True)
sol=[cq.Solid.extrudeLinear(cq.Face.makeFromWires(wire(p.exterior.coords),[wire(i.coords) for i in p.interiors]),cq.Vector(0,0,0.006)).translate((0,0,ZF-0.02)) for p in polys]
parts.append((cq.Workplane().add(cq.Compound.makeCompound(sol)),LB))
# FPC: leaves the glass bottom edge mid-thickness, folds around the board edge (lens y 0.8) onto the board top into J3
w=2.75; zg=(ZF+ZB)/2; zt=0.06; yc=-21.41-0.1; r=(zt-zg)/2; zc=(zt+zg)/2
parts.append((B(-w,w,yc,-21.41+0.3,zg-0.06,zg+0.06),FLEX))
ring=cq.Workplane('YZ').center(yc,zc).circle(r+0.06).circle(r-0.06).extrude(2*w).translate((-w,0,0))
ring=ring.intersect(B(-w-1,w+1,yc-r-1,yc,zc-r-1,zc+r+1)); parts.append((ring,FLEX))
parts.append((B(-w,w,yc,-21.41+2.9,zt-0.06,zt+0.06),FLEX))
save('MECH_LS027B7DH01_v3',parts)
# ---------------- cell 403030 (4.0 x 30 x 30) on the display back, PCM end toward the bottom (J5)
save('MECH_LP403030',[(B(-15,15,-15,15,-0.8,3.2).edges('|Y').fillet(0.8),CELL),(B(-14,14,-15,-13.2,-0.6,3.0),PCM)])
# ---------------- camera, sunk in its cutout; FPC up onto the board and into J2 (mouth at lens y 37.17, J2 x 53.8)
cx=0; z=-0.8; parts=[]
parts.append((B(-3.5,3.5,-4.25,4.25,z,z+0.12),FLEX)); z+=0.12
parts.append((B(-4.25,4.25,-4.25,4.25,z,z+0.4),STIFF)); z+=0.4
parts.append((B(-4.25,4.25,-4.25,4.25,z,z+2.9).edges('|Z').fillet(0.3),BLACK)); z+=2.9
bar=cq.Workplane('XY').circle(3.2).extrude(2.6).translate((0,0,z)); bar=bar.faces('>Z').workplane().hole(4.2,0.4)
parts.append((bar,BLACK)); parts.append((cq.Workplane('XY').circle(2.1).extrude(0.05).translate((0,0,z+2.15)),LENS))
# flex: out of the module top edge (y 4.25) rising to the board top at the cutout edge (y 4.55), then to J2
ymouth=37.17-21.41; dxj=53.8-54.65
parts.append((B(-3.5,3.5,4.25,4.9,-0.8,-0.68),FLEX))
ramp=cq.Workplane('XY').box(7.0,1.6,0.12).rotate((0,0,0),(1,0,0),35).translate((0,5.4,-0.3)); parts.append((ramp,FLEX))
parts.append((B(-3.5,3.5,5.9,10.0,0.12,0.24),FLEX))
parts.append((B(-6.0+dxj,6.0+dxj,10.0,ymouth+2.2,0.12,0.24),FLEX))
save('MECH_OV2640_CAM_v3',parts)
# ---------------- camera v3m: module on the board top, flex over J2 and folded back into its top-edge mouth
Y=lambda yl: yl-21.41; dxj=53.8-54.65; T=0.12
z=0.0; parts=[]
parts.append((B(-3.5,3.5,-4.25,4.25,z,z+T),FLEX)); z+=T
parts.append((B(-4.25,4.25,-4.25,4.25,z,z+0.4),STIFF)); z+=0.4
parts.append((B(-4.25,4.25,-4.25,4.25,z,z+2.9).edges('|Z').fillet(0.3),BLACK)); z+=2.9
bar=cq.Workplane('XY').circle(3.2).extrude(2.6).translate((0,0,z)); bar=bar.faces('>Z').workplane().hole(4.2,0.4)
parts.append((bar,BLACK)); parts.append((cq.Workplane('XY').circle(2.1).extrude(0.05).translate((0,0,z+2.15)),LENS))
import math
def seg(x0,x1,ya,za,yb,zb):
    L=((yb-ya)**2+(zb-za)**2)**0.5; a=math.degrees(math.atan2(zb-za,yb-ya))
    s=cq.Workplane('XY').box(x1-x0,L,T).translate(((x0+x1)/2,L/2,0)).rotate((0,0,0),(1,0,0),a).translate((0,ya,za))
    parts.append((s,FLEX))
w=3.5
seg(-w,w,Y(25.66),0.06,Y(26.1),1.35)
seg(-w,w,Y(26.1),1.35,Y(33.0),1.35)
seg(-6.0+dxj,6.0+dxj,Y(33.0),1.35,Y(36.6),1.35)
seg(-6.0+dxj,6.0+dxj,Y(36.6),1.35,Y(37.4),2.45)
seg(-6.0+dxj,6.0+dxj,Y(37.4),2.45,Y(43.1),2.45)
r0,r1=0.77,0.89
ring=cq.Workplane('YZ').circle(r1).circle(r0).extrude(12.0).translate((-6.0+dxj,Y(43.1),1.62))
ring=ring.intersect(B(-7+dxj,7+dxj,Y(43.1),Y(43.1)+1.0,0,3)); parts.append((ring,FLEX))
seg(-6.0+dxj,6.0+dxj,Y(43.1),0.79,Y(40.0),0.79)
save('MECH_OV2640_CAM_v3m',parts)
# ---------------- Omron D2MQ-1-TR lying flat, plunger toward +Y (footprint -Y). Datasheet (Omron en-d2mq):
# body 8.2 x 6.5 (6.08 on the far side of the step) x 2.7, pin plunger d0.8 at x=-2.0 (in line with the left
# mounting hole), hole centres 1.6 from the terminal end, FP <= hole + 6.1, terminals 2.5 pitch, tails 1.2 behind the body.
bod=B(-4.1,4.1,1.2,7.28,0,2.7).union(B(-4.1,-1.05,7.0,7.7,0,2.7)).edges('|Z').fillet(0.2)
for hx in (-2.0,2.0): bod=bod.cut(cq.Workplane('XY').center(hx,2.8).circle(0.8).extrude(3).translate((0,0,-0.1)))
parts=[(bod,BLACK),(cq.Workplane('XZ').center(-2.0,1.35).circle(0.4).extrude(-1.2).translate((0,7.7,0)),WHITE)]
for x in (-2.5,0,2.5):
    parts.append((B(x-0.4,x+0.4,-0.3,1.2,1.05,1.65),TIN)); parts.append((B(x-0.4,x+0.4,-0.3,0.3,-1.5,1.65),TIN))
    parts.append((B(x-0.65,x+0.65,-0.3,0.3,-1.2,-0.8),TIN))
save('SW_Omron_D2MQ-1-TR',parts)
