"""Recheck canonical cache modifier order without replacing prior alias evidence."""
import pathlib,json,subprocess,hashlib
root=pathlib.Path('/workspace/ptx_frontend/docs/audits/ptx93_st_immediate'); data=json.loads((root/'matrix.json').read_text()); results=[]
for record in data['results']:
 if any(record['id'].startswith('global_'+q+'_') for q in ['wb','cg','cs','wt']):
  ident='canonical_'+record['id']; src=(root/(record['id']+'.ptx')).read_text(); q=record['qualifier'];src=src.replace('st.'+q+'.global.', 'st.global.'+q+'.'); p=root/(ident+'.ptx');p.write_text(src);cmd=[data['ptxas'],'-arch=sm_100',str(p),'-o','/tmp/st_canonical.cubin'];o=subprocess.run(cmd,capture_output=True,text=True);diag=o.stdout+o.stderr;(root/(ident+'.log')).write_text(diag);results.append(dict(record,id=ident,command=cmd,source_sha256=hashlib.sha256(src.encode()).hexdigest(),returncode=o.returncode,diagnostic=diag))
(root/'canonical-cache.json').write_text(json.dumps(results,indent=2)+'\n')
