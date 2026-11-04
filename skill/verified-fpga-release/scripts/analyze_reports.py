from pathlib import Path
import argparse,re,json
p=argparse.ArgumentParser();p.add_argument('utilization',type=Path);p.add_argument('timing',type=Path);a=p.parse_args();u=a.utilization.read_text(errors='replace');lines=a.timing.read_text(errors='replace').splitlines();v={}
for name,label in [('lut','Slice LUTs'),('ff','Slice Registers'),('bram36_equiv','Block RAM Tile'),('dsp','DSPs')]:
 m=re.search(r'^\|\s*'+re.escape(label)+r'\s*\|\s*([\d,.]+)',u,re.M)
 if not m:raise SystemExit('Missing resource: '+label)
 v[name]=float(m.group(1).replace(',',''))
for i,l in enumerate(lines):
 if 'WNS(ns)' in l and 'TNS(ns)' in l and 'WHS(ns)' in l and 'Clock' not in l:
  for row in lines[i+1:i+8]:
   nums=row.split()
   if len(nums)>=8 and re.fullmatch(r'-?\d+\.\d+',nums[0]):v.update(wns_ns=float(nums[0]),tns_ns=float(nums[1]),whs_ns=float(nums[4]),ths_ns=float(nums[5]));break
  if 'wns_ns' in v:break
if 'wns_ns' not in v:raise SystemExit('Missing overall timing summary')
v['declared_constraints_pass']=v['wns_ns']>=0 and v['whs_ns']>=0 and v['tns_ns']==0 and v['ths_ns']==0
v['scope']='Declared constraints only; does not certify CDC, external IO or end-to-end system latency.'
print(json.dumps(v,indent=2))
