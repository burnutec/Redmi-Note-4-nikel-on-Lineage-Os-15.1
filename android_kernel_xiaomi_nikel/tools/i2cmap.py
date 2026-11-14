import re,sys
L=open(sys.argv[1]).read().split('\n')
alias={}
for l in L:
    m=re.match(r'\s*(i2c\d+) = "(.*?)";',l)
    if m: alias[m.group(2).split('/')[-1]]=m.group(1)
i=0
while i<len(L):
    m=re.match(r'^(\t+)(i2c@[0-9a-f]+) \{',L[i])
    if m:
        d=len(m.group(1)); j=i+1; kids=[]; cur=None; st=None
        while not re.match(r'^\t{%d}\};'%d,L[j]):
            km=re.match(r'^\t{%d}(\S+@[0-9a-f]+) \{'%(d+1),L[j])
            if km: cur=[km.group(1),'','']; kids.append(cur)
            elif cur and re.match(r'^\t{%d}compatible'%(d+2),L[j]): cur[1]=L[j].split('=',1)[1].strip(' ;')
            elif cur and re.match(r'^\t{%d}status'%(d+2),L[j]): cur[2]=L[j].split('=',1)[1].strip(' ;"')
            elif re.match(r'^\t{%d}status'%(d+1),L[j]): st=L[j].split('=',1)[1].strip(' ;"')
            j+=1
        print(f"{alias.get(m.group(2),'?'):6} {m.group(2):18} bus:{st}")
        for k in kids: print(f"        {k[0]:30} {k[1][:60]:60} {k[2]}")
        i=j
    i+=1
