# Side section of the display tail at the bottom edge (lens y, z: z=0 board top). FPC centreline path.
import math
T_FLEX=0.10; T_STIFF=0.30
GLASS_TFT=(-0.9,-1.6); GLASS_CF=(-1.6,-2.33); POL=(-2.33,-2.53); LEDGE=2.5
BOARD=(0.8,(-0.8,0.0)); WALL=-1.4; BACK=3.55
J3_MOUTH=0.8; SLOT=(0.5,1.0); SLOT_DEPTH=3.15; STIFF=4.6; TAIL=13.5
def path_S(L=TAIL, Rg=1.3, Rq=0.5, Rf=0.5, yd=-0.15):
    """glass edge -> quarter up (Rg) -> wall leg -> arch to yd -> down -> quarter to -y -> 180 down -> stiffener into J3"""
    zs=sum(SLOT)/2; ye=J3_MOUTH+SLOT_DEPTH; y0s=ye-STIFF            # stiffener from y0s to ye at z zs
    zl=zs+2*Rf; zq=zl+Rq; Ra=(yd-(-Rg))/2
    fixed=math.pi/2*Rg+math.pi*Ra+math.pi/2*Rq+(yd-Rq-y0s if yd-Rq>y0s else 0)+math.pi*Rf+STIFF
    # vertical runs: wall leg from z=-1.65+Rg to za, descending from za to zq
    za=(L-fixed+(-1.65+Rg)+zq)/2
    return dict(za=za,Ra=Ra,zl=zl,zq=zq,y0s=y0s,ye=ye,zs=zs,Rg=Rg,Rq=Rq,Rf=Rf,yd=yd,top=za+Ra+T_FLEX/2,wallface=-Rg-T_FLEX/2,turnface=y0s-Rf-T_FLEX/2,Lmin=fixed+2*zq-(-1.65+Rg)-zq)
for L in (12.8,13.5,14.2):
    p=path_S(L); print(L,{k:round(v,3) for k,v in p.items()})
