#!/usr/bin/env python3
"""Compile/run four upstream native-stack suites with ASan/UBSan, without a GPU.

No IOKit client is opened and no kext is loaded. Tests exercise pure helpers
and source-text invariants, not kernel concurrency or GPU behaviour.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from pinned_downloads import digest, local_output

ROOT = Path(__file__).resolve().parents[1]
SUITES = ('native_s1b', 'native_s1c', 'native_hostimport', 'native_ws_open')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'out/dependencies/Navi48-MacOS')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = local_output(ROOT, args.output)
    if output.exists():
        parser.error('Output exists; choose a new directory under out/')
    source = args.source.resolve()
    pin = json.loads((ROOT / 'dependencies/sources.lock.json').read_text())['components']['navi48']['revision']
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != pin or subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'], text=True):
        parser.error('Expected an unmodified checkout at the pinned Navi48 revision')
    output.mkdir(parents=True)
    kernel = source / 'src/navi48-bringup'
    results = []
    for name in SUITES:
        executable = output / name
        command = ['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(kernel / 'src'), '-I', str(kernel / 'src/amd'),
                   str(kernel / 'tests' / (name + '_test.cpp')), '-o', str(executable)]
        result = {'suite': name, 'compile_command': command}
        with (output / (name + '-build.log')).open('w') as log:
            try:
                rc = subprocess.run(command, cwd=source, stdout=log, stderr=subprocess.STDOUT, timeout=180).returncode
                result['compile_exit'] = rc
                if rc != 0:
                    result['status'] = 'compile-failed'
                else:
                    with (output / (name + '-run.log')).open('w') as run_log:
                        rc = subprocess.run([str(executable), str(source)], cwd=source, stdout=run_log,
                                            stderr=subprocess.STDOUT, timeout=90).returncode
                    result.update(run_exit=rc, status='passed' if rc == 0 else 'failed')
            except subprocess.TimeoutExpired:
                result['status'] = 'timed-out'
        results.append(result)
        print(name + ': ' + result['status'], flush=True)
    report = {'navi48_revision': revision, 'runner_sha256': digest(Path(__file__)),
              'sanitizers': ['address', 'undefined'], 'gpu_executed': False, 'suites': results}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if all(r['status'] == 'passed' for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
