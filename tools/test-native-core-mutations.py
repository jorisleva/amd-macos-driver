#!/usr/bin/env python3
"""Mutate ONLY the pure native planning/ownership model; never compile/run GPU routines."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from native_core_audit import LOCAL_FILES
from pinned_downloads import digest, local_output

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = [
    ('missing-opt-in', '!c.argumentPresent || c.argumentValue != 1', 'c.argumentValue != 1'),
    ('non-strict-opt-in', 'c.argumentValue != 1', 'c.argumentValue == 0'),
    ('skip-class', 'i < 6', 'i < 5'),
    ('unknown-console', 'if (!c.consoleKnown)', 'if (false && !c.consoleKnown)'),
    ('unknown-vram-size', '!c.vramSizeKnown ||', 'false ||'),
    ('unknown-window-origin', '!c.apertureVramOffsetKnown ||', 'false ||'),
    ('zero-required-span', '!c.requiredBytes ||', 'false ||'),
    ('short-scratch', 'c.scratch.bytes < c.requiredBytes', 'c.scratch.bytes < 1'),
    ('unaligned-scratch', '(c.scratch.offset & 0xfff)', '(c.scratch.offset & 0)'),
    ('range-end-overrun', 'inner.bytes <= outer.bytes - (inner.offset - outer.offset)',
     'inner.bytes <= outer.bytes'),
    ('truncate-vram-offset', 'inner.offset >= outer.offset',
     'static_cast<uint32_t>(inner.offset) >= outer.offset'),
    ('invert-console-overlap', 'if (overlaps(c.console, c.scratch))',
     'if (!overlaps(c.console, c.scratch))'),
    ('unproven-ownership', 'if (!c.exclusiveOwnership)', 'if (false && !c.exclusiveOwnership)'),
    ('assume-physical-dma', 'c.dma != Dma::QualifiedMapping', 'c.dma == Dma::Unknown'),
    ('reference-equals-restoration', 'c.recovery != Recovery::RestorationTested',
     'c.recovery == Recovery::Unknown'),
    ('unproven-teardown', 'if (!c.teardownQualified)', 'if (false && !c.teardownQualified)'),
    ('stale-plan', 'out = {};', 'out.scratch = {123, 456};'),
    ('continue-after-prepare-failure', 'state = State::FailedBeforePublication;', 'state = State::Prepared;'),
    ('free-gpu-owned-pages', 'case State::PotentiallyGpuOwned:',
     'case State::PotentiallyGpuOwned:\n        if (event == Event::Release) { state = State::Released; return true; }'),
    ('failure-fakes-quiescence', 'case State::PotentiallyGpuOwned:',
     'case State::PotentiallyGpuOwned:\n        if (event == Event::Failure) { state = State::Quiesced; return true; }'),
    ('free-quarantined-pages', 'case State::Quarantined:',
     'case State::Quarantined:\n        if (event == Event::Release) { state = State::Released; return true; }'),
    ('double-release', 'case State::Released:',
     'case State::Released:\n        if (event == Event::Release) return true;'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    directory = local_output(ROOT, args.output)
    if directory.exists():
        parser.error('Choose a fresh child of out/')
    directory.mkdir(parents=True)
    source = directory / 'source'
    source.mkdir()
    for name in LOCAL_FILES:
        path = ROOT / 'native/Navi48FirmwareCore' / name
        if path.is_symlink():
            raise ValueError('Source symlink rejected')
        shutil.copyfile(path, source / name)
    shutil.copytree(ROOT / 'tests/native-core', directory / 'tests')
    shutil.copyfile(Path(__file__), directory / 'runner.py.snapshot')
    original = (source / 'Preflight.cpp').read_text()
    results = []
    report = {'scope': 'pure preflight/ownership model only', 'gpu_code_executed': False,
              'source_sha256': {p.name: digest(p) for p in source.iterdir()},
              'test_sha256': {p.relative_to(directory / 'tests').as_posix(): digest(p)
                             for p in (directory / 'tests').rglob('*') if p.is_file()},
              'results': results}
    try:
        for name, before, after in [('control', '', '')] + MUTATIONS:
            if before and original.count(before) != 1:
                raise ValueError('Non-unique mutation context: ' + name)
            (source / 'Preflight.cpp').write_text(original.replace(before, after) if before else original)
            executable = directory / (name + '.test')
            command = ['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
                       '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-I', str(directory / 'tests/stubs'), '-I', str(source),
                       str(source / 'Preflight.cpp'), str(source / 'NativeLog.cpp'),
                       str(directory / 'tests/preflight_test.cpp'), '-o', str(executable)]
            built = subprocess.run(command, capture_output=True, timeout=60)
            (directory / (name + '-compile.log')).write_bytes(built.stdout + built.stderr)
            if built.returncode:
                raise ValueError('Mutant/control must compile: ' + name)
            test = subprocess.run([str(executable)], capture_output=True, timeout=30)
            text = (test.stdout + test.stderr).decode()
            (directory / (name + '-test.log')).write_text(text)
            match = re.search(r'native_core_test: (\d+) checks, (\d+) failed', text)
            if not match:
                raise ValueError('Missing assertion summary: ' + name)
            status = ('control-pass' if test.returncode == 0 and int(match[2]) == 0 else 'control-failed') if name == 'control' else (
                'detected' if test.returncode == 1 and int(match[2]) > 0 else 'escaped-or-runtime-error')
            results.append({'name': name, 'status': status, 'compiled': True,
                            'checks': int(match[1]), 'failed_checks': int(match[2])})
            print(name + ': ' + status, flush=True)
        report['passed'] = results[0]['status'] == 'control-pass' and all(r['status'] == 'detected' for r in results[1:])
        if not report['passed']:
            raise ValueError('Control failed or a mutant escaped')
    except Exception as error:
        report.update(passed=False, error=str(error))
        raise
    finally:
        (source / 'Preflight.cpp').write_text(original)
        (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
