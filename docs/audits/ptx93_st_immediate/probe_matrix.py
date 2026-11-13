"""Reproduce the pinned finite scalar-ST assembler evidence for this audit."""
import pathlib, subprocess, hashlib, json
root=pathlib.Path('/workspace/ptx_frontend/docs/audits/ptx93_st_immediate'); exe='/tmp/ptx93-ptxas/cuda_nvcc-linux-x86_64-13.3.33-archive/bin/ptxas'
cases=[]
def add(name,typ='b32',value='0',space='shared',qual='',version='9.3',sm='100'):
    """Schedule one typed data producer and its matched register control."""
    cases.append((name,typ,value,space,qual,version,sm))
for typ in ['b8','b16','b32','b64','b128','u8','u16','u32','u64','s8','s16','s32','s64','f32','f64']:
    for tag,value in [('zero','0'),('one','1'),('minus1','-1'),('hex','0xffffffff'),('float','1.5'),('exact32','0f3fc00000'),('exact64','0d3ff8000000000000')]: add(typ+'_'+tag,typ,value)
for tag,value in [('maxu64','18446744073709551615'),('overu64','18446744073709551616'),('maxu32','4294967295'),('overu32','4294967296'),('mins32','-2147483648'),('under32','-2147483649'),('expression','(1+2)'),('negfloat','-1.5'),('hexfloat','0x3f800000')]:
 for typ in ['b32','u32','s32','b64']: add(typ+'_'+tag,typ,value)
for space in ['global','local','generic']:
 for typ in ['b32','b64','f32','b128']:
  for tag,value in [('zero','0'),('one','1'),('float','1.5')]: add(space+'_'+typ+'_'+tag,typ,value,space)
for qual,space in [('volatile','shared'),('volatile','global'),('release.cta','shared'),('relaxed.cta','shared'),('release.gpu','global'),('relaxed.gpu','global'),('wb','global'),('cg','global'),('cs','global'),('wt','global')]: add(space+'_'+qual.replace('.','_'),space=space,qual=qual,value='17')
for ver,sm in [('1.0','50'),('4.0','50'),('6.0','70'),('7.0','80'),('8.0','90'),('8.6','100')]: add('floor_'+ver,version=ver,sm=sm,value='17')
results=[]
for name,typ,value,space,qual,version,sm in cases:
 for control in [False,True]:
  ident=name+('_reg' if control else '_imm'); st='st.'+(qual+'.' if qual else '')+('' if space=='generic' else space+'.')+typ
  addr={'shared':'slot','local':'loc','global':'%out','generic':'%out'}[space]; operand='%v' if control else value
  # Readback into a register and global write retains local/shared stores' effect.
  readback='' if space in ['global','generic'] else f'ld.{space}.{typ} %v, [{addr}];\n st.global.{typ} [%out], %v;'
  src=f'''.version {version}\n.target sm_{sm}\n.address_size 64\n.visible .entry probe(.param .u64 output) {{\n .reg .b64 %out;\n .reg .{typ} %v;\n .shared .align 16 .b8 slot[16];\n .local .align 16 .b8 loc[16];\n ld.param.u64 %out, [output];\n ld.global.{typ} %v, [%out];\n {st} [{addr}], {operand};\n {readback}\n ret;\n}}\n'''
  path=root/(ident+'.ptx'); path.write_text(src); cmd=[exe,'-arch=sm_'+sm,str(path),'-o','/tmp/st_matrix.cubin']; run=subprocess.run(cmd,capture_output=True,text=True)
  diag=run.stdout+run.stderr; (root/(ident+'.log')).write_text(diag)
  results.append(dict(id=ident,type=typ,value=value,space=space,qualifier=qual,version=version,sm=sm,control=control,command=cmd,source_sha256=hashlib.sha256(src.encode()).hexdigest(),returncode=run.returncode,diagnostic=diag))
(root/'matrix.json').write_text(json.dumps(dict(ptxas=exe,ptxas_sha256=hashlib.sha256(pathlib.Path(exe).read_bytes()).hexdigest(),results=results),indent=2)+'\n')
for r in results:
 if not r['control']:
  c=next(x for x in results if x['id']==r['id'][:-4]+'_reg'); print(r['id'],r['returncode'],c['returncode'],r['diagnostic'].splitlines()[:1])
