"""Reproduce the pinned finite scalar-ST assembler evidence for this audit."""
import pathlib,subprocess,hashlib,json
r=pathlib.Path('/workspace/ptx_frontend/docs/audits/ptx93_st_immediate'); exe='/tmp/ptx93-ptxas/cuda_nvcc-linux-x86_64-13.3.33-archive/bin/ptxas'; cases=[]
def case(name,body,version='9.3',sm='100',extra=''):
 """Assemble one side-effect-preserving frontier and its register control."""
 for control in [False,True]:
  ins=body.replace('@SRC@','%v' if control else '17'); src=f'.version {version}\n.target sm_{sm}\n.address_size 64\n'+extra+f'\n.visible .entry probe(.param .u64 output) {{\n.reg .b64 %out, %policy;\n.reg .b32 %v;\n.shared .align 16 .b8 slot[16];\nld.param.u64 %out, [output];\nld.global.b32 %v, [%out];\nld.global.b64 %policy, [%out];\n'+ins+'\nret;\n}\n'
  ident=name+('_reg' if control else '_imm'); p=r/(ident+'.ptx');p.write_text(src);cmd=[exe,'-arch=sm_'+sm,str(p),'-o','/tmp/st_front.cubin'];o=subprocess.run(cmd,capture_output=True,text=True);diag=o.stdout+o.stderr;(r/(ident+'.log')).write_text(diag);cases.append(dict(id=ident,control=control,command=cmd,source_sha256=hashlib.sha256(src.encode()).hexdigest(),returncode=o.returncode,diagnostic=diag));print(ident,o.returncode,diag.splitlines()[:2])
for ver in ['6.3','6.4','7.0']:case('sm75_floor_'+ver,'st.shared.b32 [slot], @SRC@;\nld.shared.b32 %v,[slot];\nst.global.b32 [%out],%v;',ver,'75')
for st in ['st.shared::cta.b32','st.relaxed.cluster.shared::cluster.b32','st.global.L1::evict_last.b32','st.L1::evict_last.b32','st.global.L2::cache_hint.b32','st.L2::cache_hint.b32']:
 addr='slot' if 'shared' in st else '%out'; cache=', %policy' if 'cache_hint' in st else '';case(st.replace(':','_').replace('.','_'),f'{st} [{addr}], @SRC@{cache};\nld.shared.b32 %v,[slot];\nst.global.b32 [%out],%v;')
case('bad_policy_immediate','st.global.L2::cache_hint.b32 [%out], @SRC@, 0;')
func='.func sink(.param .b32 arg) { .reg .b32 %a; .reg .b64 %p; ld.param.b32 %a,[arg]; cvt.u64.u32 %p,%a; st.global.b32 [%p],%a; ret; }\n'
for spelling in ['param','param::func']:
 case('call_'+spelling.replace(':','_'),'{ .param .b32 arg; st.'+spelling+'.b32 [arg], @SRC@; call sink,(arg); }',extra=func)
# Function return store remains observable through the entry's global write.
for spelling in ['param','param::func']:
 for control in [False,True]:
  name='return_'+spelling.replace(':','_')+('_reg' if control else '_imm');val='%v' if control else '17';src=f'.version 9.3\n.target sm_100\n.address_size 64\n.func (.param .b32 result) give() {{ .reg .b32 %v; mov.b32 %v,17; st.{spelling}.b32 [result], {val}; ret; }}\n.visible .entry probe(.param .u64 output) {{ .reg .b64 %out; .reg .b32 %v; ld.param.u64 %out,[output]; {{ .param .b32 result; call (result),give,(); ld.param.b32 %v,[result]; }} st.global.b32 [%out],%v; ret; }}\n';p=r/(name+'.ptx');p.write_text(src);cmd=[exe,'-arch=sm_100',str(p),'-o','/tmp/st_front.cubin'];o=subprocess.run(cmd,capture_output=True,text=True);diag=o.stdout+o.stderr;(r/(name+'.log')).write_text(diag);cases.append(dict(id=name,control=control,command=cmd,source_sha256=hashlib.sha256(src.encode()).hexdigest(),returncode=o.returncode,diagnostic=diag));print(name,o.returncode,diag.splitlines()[:2])
(r/'frontiers.json').write_text(json.dumps(cases,indent=2)+'\n')
