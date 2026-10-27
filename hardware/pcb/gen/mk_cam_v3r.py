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
# ---------------- camera v3q: FINAL module 8 x 8 mm, 6.0 mm thick incl. lens (stiffener 0.4 + body 2.9 + barrel 2.7),
# on its 0.12 flex on the board top; flex over J2 and folded back into its top-edge mouth (loop unchanged)
H2=4.0
CY=23.12   # v3r: lens centred on the case (STEP y 22.32); was 21.41
Y=lambda yl: yl-CY; dxj=53.8-54.65; T=0.12
z=0.0; parts=[]
parts.append((B(-3.5,3.5,-H2,H2,z,z+T),FLEX)); z+=T
parts.append((B(-H2,H2,-H2,H2,z,z+0.4),STIFF)); z+=0.4
parts.append((B(-H2,H2,-H2,H2,z,z+2.9).edges('|Z').fillet(0.3),BLACK)); z+=2.9
bar=cq.Workplane('XY').circle(3.2).extrude(2.7).translate((0,0,z)); bar=bar.faces('>Z').workplane().hole(4.2,0.4)
parts.append((bar,BLACK)); parts.append((cq.Workplane('XY').circle(2.1).extrude(0.05).translate((0,0,z+2.25)),LENS))
import math
def seg(x0,x1,ya,za,yb,zb):
    L=((yb-ya)**2+(zb-za)**2)**0.5; a=math.degrees(math.atan2(zb-za,yb-ya))
    s=cq.Workplane('XY').box(x1-x0,L,T).translate(((x0+x1)/2,L/2,0)).rotate((0,0,0),(1,0,0),a).translate((0,ya,za))
    parts.append((s,FLEX))
w=3.5
seg(-w,w,Y(CY+H2),0.06,Y(CY+H2+0.44),1.35)
seg(-w,w,Y(CY+H2+0.44),1.35,Y(33.0),1.35)
seg(-6.0+dxj,6.0+dxj,Y(33.0),1.35,Y(36.6),1.35)
seg(-6.0+dxj,6.0+dxj,Y(36.6),1.35,Y(37.4),2.45)
seg(-6.0+dxj,6.0+dxj,Y(37.4),2.45,Y(43.1),2.45)
r0,r1=0.77,0.89
ring=cq.Workplane('YZ').circle(r1).circle(r0).extrude(12.0).translate((-6.0+dxj,Y(43.1),1.62))
ring=ring.intersect(B(-7+dxj,7+dxj,Y(43.1),Y(43.1)+1.0,0,3)); parts.append((ring,FLEX))
seg(-6.0+dxj,6.0+dxj,Y(43.1),0.79,Y(40.0),0.79)
save('MECH_OV2640_CAM_v3r',parts)
