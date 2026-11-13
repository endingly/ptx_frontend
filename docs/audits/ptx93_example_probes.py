"""Probe concrete statements copied from official PTX 9.3 Examples blocks.

This is bounded, deterministic parser evidence, not a semantic conformance test.
Only the Python standard library is required. The raw manual is an external input;
its hash, source blocks, selected statements, skips, and diagnostics are retained.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
import tempfile


class Examples(HTMLParser):
    """Collect section heading spellings and Examples preformatted source text."""

    def __init__(self) -> None:
        """Initialize document-local capture state."""
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.sections = []
        self.blocks = []
        self.heading = None
        self.code = None
        self.rubric = None
        self.pre = None

    def handle_starttag(self, tag, attrs):
        """Capture only heading code tokens and rubric-scoped pre blocks."""
        attrs = dict(attrs)
        if tag == 'section':
            row = dict(anchor=attrs.get('id', ''), title='', spellings=[], rubric='')
            self.stack.append(row)
            self.sections.append(row)
        if re.fullmatch('h[1-9]', tag) and self.stack:
            self.heading = dict(tag=tag, text=[], row=self.stack[-1])
        if tag == 'code' and self.heading:
            self.code = []
        if tag == 'p' and 'rubric' in attrs.get('class', '').split():
            self.rubric = []
        if tag == 'pre' and self.stack and self.stack[-1]['rubric'] == 'Examples':
            self.pre = dict(section=self.stack[-1], html_line=self.getpos()[0], text=[])

    def handle_endtag(self, tag):
        """Finish independent capture contexts without flattening section ownership."""
        if tag == 'code' and self.code is not None and self.heading:
            self.heading['row']['spellings'].append(''.join(self.code))
            self.code = None
        if self.heading and tag == self.heading['tag']:
            self.heading['row']['title'] = ' '.join(''.join(self.heading['text']).split())
            self.heading = None
        if tag == 'p' and self.rubric is not None:
            if self.stack:
                self.stack[-1]['rubric'] = ''.join(self.rubric).strip()
            self.rubric = None
        if tag == 'pre' and self.pre is not None:
            self.pre['text'] = ''.join(self.pre['text'])
            self.blocks.append(self.pre)
            self.pre = None
        if tag == 'section' and self.stack:
            self.stack.pop()

    def handle_data(self, data):
        """Retain decoded HTML source text in active captures."""
        if self.heading:
            self.heading['text'].append(data)
        if self.code is not None:
            self.code.append(data)
        if self.rubric is not None:
            self.rubric.append(data)
        if self.pre is not None:
            self.pre['text'].append(data)


def heading_roots(section):
    """Derive real instruction roots solely from manual chapter 9 headings."""
    if not section['title'].startswith('9.') or 'instruction' not in section['title'].lower():
        return set()
    return {token.split('.')[0] for code in section['spellings']
            for token in re.split(r'[,\s]+', code)
            if re.fullmatch(r'[a-z][a-z0-9_]*(?:\.[a-zA-Z0-9_:]+)*', token)}


def candidates(text):
    """Split at semicolons after removing comments; preserve braces and source lines.

    Blocks and labels are deliberately not rewritten to manufacture instructions.
    Their fragments are retained as skipped candidates for human inspection.
    """
    clean = re.sub(r'/\*.*?\*/', lambda m: '\n' * m[0].count('\n'), text, flags=re.S)
    clean = re.sub(r'//[^\n]*', '', clean)
    start = 0
    for match in re.finditer(';', clean):
        raw = clean[start:match.end()]
        offset = len(raw) - len(raw.lstrip())
        yield clean[:start + offset].count('\n') + 1, raw.strip()
        start = match.end()
    if clean[start:].strip():
        yield clean[:start].count('\n') + 1, clean[start:].strip()


def main():
    """Extract and run a bounded set, retaining every source and selection decision."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manual', type=Path)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--per-section', type=int, default=2)
    parser.add_argument('--limit', type=int, default=400)
    args = parser.parse_args()
    raw = args.manual.read_bytes()
    doc = Examples()
    doc.feed(raw.decode('utf-8'))
    roots = set().union(*(heading_roots(row) for row in doc.sections))
    blocks, results = [], []
    used = Counter()
    seen = set()
    with tempfile.TemporaryDirectory(prefix='ptx93-example-probes-') as temporary:
        fixture = Path(temporary) / 'example.ptx'
        for block in doc.blocks:
            section = block['section']
            if not heading_roots(section):
                continue
            block_id = len(blocks)
            blocks.append(dict(id=block_id, anchor=section['anchor'], title=section['title'],
                               html_line=block['html_line'], source=block['text']))
            for line, instruction in candidates(block['text']):
                row = dict(block=block_id, source_line=line, instruction=instruction)
                match = re.match(r'(?:@!?[\w%]+\s+)?([a-z][\w]*(?:\.[\w:]+)*)\s*(.*);$', instruction, re.S)
                reason = None
                if not match or match[1].split('.')[0] not in roots:
                    reason = 'not an isolated recognized instruction statement (block/label/declaration/pseudocode)'
                elif re.search(r'\.\.\.|…|[<>]|\{\s*\.[a-z]|\b(?:type|dtype|atype|btype|ctype|shape|num|size|mode)\b', instruction):
                    reason = 'placeholder or template notation requires human context'
                elif instruction in seen:
                    reason = 'duplicate instruction text'
                elif used[section['anchor']] >= args.per_section:
                    reason = 'per-section bounded sample limit'
                elif sum(used.values()) >= args.limit:
                    reason = 'total bounded sample limit'
                if reason:
                    row.update(status='skipped', reason=reason)
                else:
                    seen.add(instruction)
                    used[section['anchor']] += 1
                    row['root'] = match[1].split('.')[0]
                    wrapped = '.version 9.3\n.target sm_120a\n.address_size 64\n.entry audit(){\n' + instruction + '\n}\n'
                    fixture.write_text(wrapped)
                    try:
                        process = subprocess.run([str(args.probe.resolve()), str(fixture)],
                                                 text=True, capture_output=True, timeout=3)
                        row.update(status='parse_pass' if process.returncode == 0 else 'parse_diagnostic',
                                   returncode=process.returncode, stdout=process.stdout, stderr=process.stderr)
                    except subprocess.TimeoutExpired as error:
                        row.update(status='timeout', stderr=str(error))
                results.append(row)
    selected = [row for row in results if row['status'] != 'skipped']
    report = dict(manual_url='https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html',
                  html_sha256=hashlib.sha256(raw).hexdigest(), html_bytes=len(raw),
                  probe_path=str(args.probe.resolve()), probe_sha256=hashlib.sha256(args.probe.read_bytes()).hexdigest(),
                  wrapper='.version 9.3\n.target sm_120a\n.address_size 64\n.entry audit(){\n<instruction>\n}\n',
                  per_section=args.per_section, total_limit=args.limit,
                  selected_count=len(selected), selected_roots=sorted({row['root'] for row in selected}),
                  selected_sections=len(used), outcomes=dict(Counter(row['status'] for row in results)),
                  limitations=['Parser-only: undeclared operands are intentional; no resolve, target, or semantic validity claim.',
                               'Statements are copied with comments removed, without opcode/operand rewrites.',
                               'Block/label/template fragments and sample-limit omissions remain reviewable below.',
                               'Manual examples may contain intentional negatives or editorial inconsistencies; diagnostics require review.'],
                  blocks=blocks, candidates=results)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('html_sha256', 'selected_count', 'selected_sections', 'outcomes')}))
    print('selected roots:', len(report['selected_roots']))


if __name__ == '__main__':
    main()
