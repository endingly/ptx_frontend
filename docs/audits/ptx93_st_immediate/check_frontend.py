"""Compare the pinned assembler probes with the current full frontend executable."""
import collections
import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent
probe = pathlib.Path('/tmp/ptx93-audit-build/ptx93_probe')
records = json.loads((root / 'matrix.json').read_text())['results']
records += json.loads((root / 'frontiers.json').read_text())
records += json.loads((root / 'canonical-cache.json').read_text())
results = []
for record in records:
    source = root / (record['id'] + '.ptx')
    command = [str(probe), str(source)]
    try:
        completed = subprocess.run(command, capture_output=True, text=True, timeout=30)
        output = completed.stdout + completed.stderr
        returncode = completed.returncode
    except (subprocess.TimeoutExpired, OSError) as failure:
        output = str(failure)
        returncode = None
    accepted = returncode == 0 and 'resolveAndValidate value=1 diagnostics=0' in output
    assembler_accepted = record['returncode'] == 0
    rejected = ((returncode == 3 and 'resolveAndValidate value=0 diagnostics=' in output)
                or (returncode == 2 and 'parse value=0 diagnostics=' in output))
    category = 'agree_accept' if accepted and assembler_accepted else 'agree_reject'
    if not accepted and not rejected:
        category = 'frontend_tool_failure'
    elif 'not defined for option' in record['diagnostic']:
        category = 'assembler_target_unavailable'
    elif accepted != assembler_accepted:
        if record.get('type') == 'b128' and not record['control']:
            category = 'retained_b128_immediate_limit'
        elif record.get('value') == '18446744073709551616' and not record['control']:
            category = 'retained_integer_source_range'
        elif record['id'].startswith(('global_wb_', 'global_cg_', 'global_cs_', 'global_wt_')):
            category = 'retained_cache_modifier_order'
        elif record['id'].startswith('bad_policy_immediate'):
            category = 'retained_register_policy_operand'
        else:
            category = 'unclassified_divergence'
    results.append(dict(id=record['id'], command=command,
                        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                        ptxas_accepted=assembler_accepted, frontend_accepted=accepted,
                        returncode=returncode, output=output, category=category))
(root / 'frontend-comparison.json').write_text(json.dumps(dict(
    executable=str(probe), executable_sha256=hashlib.sha256(probe.read_bytes()).hexdigest(),
    counts=dict(collections.Counter(item['category'] for item in results)), results=results), indent=2) + '\n')
print(collections.Counter(item['category'] for item in results))
for item in results:
    if item['category'] == 'unclassified_divergence':
        print(item['id'], item['output'])
