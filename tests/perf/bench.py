import sys, time
def slurp(p):
    with open(p,'rb') as f: return f.read()
mode=sys.argv[1]; K=5
def t(): return time.perf_counter()
if mode=="find":
    data=slurp(sys.argv[2]); pat=sys.argv[3].encode()
    lines=data.split(b'\n')                         # pre-split (the "parse"), outside timing
    for it in range(K):                             # idiomatic: C-backed substring count
        s=t(); h=data.count(pat); print(f"find.count iter{it} {(t()-s)*1e3:7.0f} ms  hits={h}")
    for it in range(3):                             # explicit loop, same idea as C/Java
        s=t(); h=sum(1 for ln in lines if pat in ln); print(f"find.loop  iter{it} {(t()-s)*1e3:7.0f} ms  hits={h}")
else:
    A=set(slurp(sys.argv[2]).split()); B=set(slurp(sys.argv[3]).split())   # sets, outside timing
    LA=list(map(int,slurp(sys.argv[2]).split())); LB=list(map(int,slurp(sys.argv[3]).split()))  # sorted int lists
    for it in range(K):                             # idiomatic: C-backed set intersection
        s=t(); c=len(A&B); print(f"and.set   iter{it} {(t()-s)*1e3:7.1f} ms  common={c} |A|={len(A)} |B|={len(B)}")
    for it in range(3):                             # explicit two-pointer, same algorithm as C/Java
        s=t(); i=j=c=0; na=len(LA); nb=len(LB)
        while i<na and j<nb:
            x=LA[i]; y=LB[j]
            if x==y: c+=1; i+=1; j+=1
            elif x<y: i+=1
            else: j+=1
        print(f"and.2ptr  iter{it} {(t()-s)*1e3:7.0f} ms  common={c}")
