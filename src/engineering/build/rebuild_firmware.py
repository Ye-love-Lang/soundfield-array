"""Rebuild current ARM applications into an isolated output directory; never flash."""
from pathlib import Path
import argparse,json,subprocess,concurrent.futures,hashlib,sys,time
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--tool-root',default='D:/AMD_DesignTools/2026.1');p.add_argument('--target',choices=['app','fsbl','all'],default='all');p.add_argument('--jobs',type=int,default=4);a=p.parse_args()
tool=Path(a.tool_root);gcc=tool/'gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
if not gcc.is_file():raise SystemExit('ARM compiler not found: '+str(gcc))
OUT=ROOT/'build/rebuilt';OUT.mkdir(parents=True,exist_ok=True)
log=[];commands=[]
def execute(cmd):
 t=time.monotonic();r=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,encoding='utf8',errors='replace')
 return {'command':[str(x) for x in cmd],'exit_code':r.returncode,'seconds':time.monotonic()-t,'stdout':r.stdout,'stderr':r.stderr}
def run_group(cmds):
 with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(a.jobs,8))) as ex:
  for r in ex.map(execute,cmds):
   log.append(r);commands.append(r['command'])
   if r['exit_code']:raise RuntimeError(r['stderr'] or r['stdout'])
def native_path(s):return str(s).replace('\\','/')
def rewrite(s,outdir):
 s=native_path(s)
 old='D:/FPGA_Learning/Work_project/soundfield_8mic'
 s=s.replace(old,ROOT.as_posix())
 s=s.replace('D:/AMD_DesignTools/2026.1',tool.as_posix())
 q='C:/Users/lanmy/Documents/Codex/2026-10-13/w/qspi_boot_0212'
 s=s.replace(q+'/src',(ROOT/'development/qspi_boot_0212/src').as_posix())
 s=s.replace(q+'/build',outdir.as_posix())
 return s
try:
 if a.target in ['app','all']:
  src=ROOT/'development/directional_path_0212/fw';out=OUT/'app';out.mkdir(exist_ok=True)
  bsp=ROOT/'Vitis/soundfield_platform/export/soundfield_platform/sw/standalone_ps7_cortexa9_0'
  manifest=json.loads((ROOT/'development/directional_path_0212/build/build_manifest.json').read_text(encoding='utf8'))
  link=next(c for c in manifest['commands'] if '-Wl,--start-group' in c and any('build_fast' in x for x in c))
  names=[Path(native_path(x)).stem for x in link if x.endswith('.o')]
  flags=['-DSDT','-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=hard','-O2','-g','-Wall','-Wextra','-U__clang__','-fno-tree-loop-distribute-patterns','-I'+src.as_posix(),'-I'+(bsp/'include').as_posix(),'-specs='+(bsp/'Xilinx.spec').as_posix(),'-fstack-usage']
  cmds=[]
  for n in names:
   f=src/(n+'.c')
   if not f.is_file():raise FileNotFoundError(f)
   cmds.append([gcc,*flags,'-c',f,'-o',out/(n+'.o')])
  run_group(cmds)
  run_group([[gcc,*flags,*[out/(n+'.o') for n in names],'-o',out/'soundfield.elf','-T',src/'lscript.ld','-L'+(bsp/'lib').as_posix(),'-Wl,-Map='+(out/'soundfield.map').as_posix(),'-Wl,--start-group','-lxilstandalone','-lxiltimer','-llwip220','-lxil','-lgcc','-lc','-lm','-Wl,--end-group']])
 if a.target in ['fsbl','all']:
  out=OUT/'fsbl';out.mkdir(exist_ok=True)
  m=json.loads((ROOT/'development/qspi_boot_0212/build/commands.json').read_text(encoding='utf8'))
  comp={}
  for c in m:
   if '-c' in c and c[0].endswith('gcc.exe'):
    cmd=[rewrite(x,out) for x in c];comp[cmd[cmd.index('-c')+1]]=cmd
  run_group(list(comp.values()))
  links=[c for c in m if '-c' not in c and any(x.endswith('fsbl_qspi.elf') for x in c)]
  if not links:raise RuntimeError('FSBL link command unavailable')
  run_group([[rewrite(x,out) for x in links[-1]]])
except Exception as e:
 (OUT/'rebuild_log.json').write_text(json.dumps(log,indent=2,ensure_ascii=False),encoding='utf8');raise
outputs=[]
for f in OUT.rglob('*.elf'):outputs.append({'path':f.relative_to(ROOT).as_posix(),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
result={'target':a.target,'compile_and_link_calls':len(log),'warnings':[r['stderr'] for r in log if 'warning:' in r['stderr']],'outputs':outputs,'flash_updated':False,'hardware_validated':False}
(OUT/'rebuild_log.json').write_text(json.dumps(log,indent=2,ensure_ascii=False),encoding='utf8')
(OUT/'rebuild_result.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf8')
print(json.dumps(result,ensure_ascii=False))
