from pathlib import Path
import argparse,json,hashlib,sys
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('manifest',type=Path);a=p.parse_args();root=a.root.resolve();m=json.loads(a.manifest.read_text(encoding='utf8'));issues=[]
for e in m['files']:
 f=(root/e['path']).resolve()
 if not f.is_relative_to(root):issues.append({'path':e['path'],'error':'outside_root'});continue
 if not f.is_file():issues.append({'path':e['path'],'error':'missing'});continue
 if f.stat().st_size!=e['bytes'] or hashlib.sha256(f.read_bytes()).hexdigest()!=e['sha256']:issues.append({'path':e['path'],'error':'changed'})
print(json.dumps({'checked':len(m['files']),'issues':issues},ensure_ascii=False));sys.exit(bool(issues))
