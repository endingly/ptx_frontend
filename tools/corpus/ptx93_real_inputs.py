#!/usr/bin/env python3
"""Regenerate/check the isolated real-input corpus or record full frontend/ptxas runs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'corpus/ptx93_real_inputs'
VERSION = 'Cuda compilation tools, release 13.3, V13.3.33'


def digest(path: Path) -> str:
    """Hash original file bytes without rewriting source or compiler output."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def executable(name: str) -> Path:
    """Resolve a command on PATH or an executable path with a useful error."""
    resolved = shutil.which(name)
    if resolved is None:
        raise SystemExit(f'executable not found or not executable: {name}')
    return Path(resolved).resolve()


def invoke(command: list[str], timeout: float = 60) -> dict:
    """Retain complete diagnostics and distinguish a bounded subprocess timeout."""
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                check=False, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        def output(value: bytes | str | None) -> str:
            """Decode partial timeout output using replacement for incomplete UTF-8."""
            return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else value or ''
        return dict(command=command, returncode=124, stdout=output(error.stdout),
                    stderr=output(error.stderr), timed_out=True, timeout_seconds=timeout)
    return dict(command=command, returncode=result.returncode, stdout=result.stdout,
                stderr=result.stderr, timed_out=False, timeout_seconds=timeout)


def main() -> None:
    """Run an explicitly selected operation against the standalone manifest."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['regenerate', 'check', 'run'])
    parser.add_argument('--nvcc', default='nvcc')
    parser.add_argument('--frontend', type=Path)
    parser.add_argument('--ptxas', default='ptxas')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    manifest = json.loads((BASE / 'manifest.json').read_text())
    if args.operation in ('regenerate', 'check'):
        args.nvcc = str(executable(args.nvcc))
        version = invoke([args.nvcc, '--version'])
        if version['returncode'] or VERSION not in version['stdout'].splitlines():
            raise SystemExit('requires exact NVIDIA nvcc V13.3.33')
        stale = []
        with tempfile.TemporaryDirectory(prefix='ptx93-real-') as tmp:
            for fixture in manifest['fixtures']:
                target = ROOT / fixture['path']
                if fixture['kind'] == 'natural_compiler_emission':
                    output = Path(tmp) / target.name
                    result = invoke([args.nvcc, *fixture['nvcc_flags'], fixture['source'],
                                     '-o', str(output)])
                    if result['returncode']:
                        raise SystemExit(json.dumps(result, indent=2))
                    if args.operation == 'regenerate':
                        target.write_bytes(output.read_bytes())
                        fixture['sha256'] = digest(target)
                    elif output.read_bytes() != target.read_bytes():
                        stale.append(fixture['path'])
                if fixture['kind'] == 'upstream_encoded_compiler_output':
                    encoded = (ROOT / fixture['source']).read_text()
                    decoded = bytes(int(word, 16) for word in re.findall(r'0x([0-9a-fA-F]{2})', encoded))
                    if not decoded.endswith(b'\0') or b'\0' in decoded[:-1] or decoded[:-1] != target.read_bytes():
                        stale.append(fixture['path'] + ' (upstream array extraction)')
                if digest(target) != fixture['sha256']:
                    stale.append(fixture['path'] + ' (manifest hash)')
        for source in manifest['sources']:
            if digest(ROOT / source['path']) != source['sha256']:
                stale.append(source['path'] + ' (source hash)')
        if stale:
            raise SystemExit('stale files: ' + ', '.join(stale))
        if args.operation == 'regenerate':
            (BASE / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f"{len(manifest['fixtures'])} fixtures verified")
        return
    if args.frontend is None or args.output is None:
        parser.error('run requires --frontend and --output')
    frontend = executable(str(args.frontend))
    ptxas = executable(args.ptxas)
    report = {'schema': 'ptx_frontend.ptx93_real_inputs_evidence/v1',
              'manifest_sha256': digest(BASE / 'manifest.json'),
              'base_commit': invoke(['git', 'rev-parse', 'HEAD'])['stdout'].strip(),
              'checker_source_sha256': digest(ROOT / 'submod/resolved_ir/src/ptx_resolved_ir_checker.cpp'),
              'runner_sha256': digest(Path(__file__).resolve()),
              'frontend': {'path': str(frontend), 'sha256': digest(frontend)},
              'ptxas': {'path': str(ptxas), 'sha256': digest(ptxas),
                        'version': invoke([str(ptxas), '--version'])},
              'fixtures': []}
    with tempfile.TemporaryDirectory(prefix='ptx93-real-assembly-') as tmp:
        for fixture in manifest['fixtures']:
            path = ROOT / fixture['path']
            if digest(path) != fixture['sha256']:
                raise SystemExit(f'hash mismatch: {path}')
            source = path.read_text()
            target = re.search(r'\.target\s+(\w+)', source).group(1)
            cubin = Path(tmp) / (path.stem + '.cubin')
            assembly = invoke([str(ptxas), '-arch=' + target, str(path), '-o', str(cubin)])
            assembly['outcome'] = ('timed_out' if assembly['timed_out'] else
                                   'accepted' if assembly['returncode'] == 0 else
                                   'unsupported_target' if 'not defined for option' in assembly['stderr']
                                   else 'rejected')
            assembly['cubin_sha256'] = digest(cubin) if cubin.exists() else None
            instructions = re.findall(r'^\s*(?:@!?\S+\s+)?([a-z][a-z0-9]*(?:\.[a-zA-Z0-9_:]+)*)\s+[^;\n]*;', source, re.M)
            report['fixtures'].append({'path': fixture['path'], 'sha256': digest(path),
                'kind': fixture['kind'], 'target': target,
                'descriptive_instruction_count': len(instructions),
                'descriptive_roots': sorted({instruction.split('.')[0] for instruction in instructions}),
                'frontend': invoke([str(frontend), str(path)]), 'ptxas': assembly})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    for fixture in report['fixtures']:
        print(f"{fixture['path']}: frontend={fixture['frontend']['returncode']} ptxas={fixture['ptxas']['outcome']}")


if __name__ == '__main__':
    main()
