#!/usr/bin/env python3
"""Sample process memory without extra Python dependencies. Run under Xvfb or a desktop."""
import argparse
import json
import pathlib
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--output', default='build/linux/memory.json')
parser.add_argument('command', nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command
if command and command[0] == '--':
    command = command[1:]
if not command:
    parser.error('Pass a command after -- (use imvim --smoke-seconds 10).')

def memory(pid):
    try:
        values = {}
        for line in pathlib.Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines():
            if ':' in line:
                key, value = line.split(':', 1)
                if value.strip().endswith('kB'):
                    values[key] = int(value.split()[0])
        return {key: values.get(key, 0) for key in ('Rss', 'Pss', 'Private_Clean', 'Private_Dirty')}
    except (FileNotFoundError, ProcessLookupError):
        return None

records = {}
start = time.monotonic()
with subprocess.Popen(command) as process:
    while process.poll() is None:
        # Direct child is nvim. Smoke mode uses --clean, so no LSP is started.
        try:
            children = pathlib.Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
        except FileNotFoundError:
            children = []
        for pid, name in [(process.pid, 'gui')] + [(int(pid), 'nvim') for pid in children]:
            sample = memory(pid)
            if sample is None:
                continue
            record = records.setdefault(name, {'pid': pid, 'peak_sampled_kib': {}, 'last_sample_kib': {}})
            for key, value in sample.items():
                record['peak_sampled_kib'][key] = max(value, record['peak_sampled_kib'].get(key, 0))
            record['last_sample_kib'] = sample
        time.sleep(0.1)
    result = {'command': command, 'exit_code': process.returncode, 'seconds': time.monotonic()-start,
              'note': 'Linux CPU memory only; excludes GPU allocations. Xvfb may use Mesa software rendering. Not a Windows or terminal comparison.',
              'processes': records}
pathlib.Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
print(json.dumps(result, ensure_ascii=False, indent=2))
raise SystemExit(process.returncode)
