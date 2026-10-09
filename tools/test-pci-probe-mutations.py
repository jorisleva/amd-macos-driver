#!/usr/bin/env python3
"""Plant defects in copies of a successful PCI observer build's host inputs.

Requires the unchanged source/test snapshots of build-pci-probe.py. No kext
loading, IOKit connection or GPU operations. Each mutation must be rejected by
-Werror, ASan/UBSan or a test; the unmodified baseline must compile and pass.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from pinned_downloads import digest, local_output

ROOT = Path(__file__).resolve().parents[1]
POLICY = 'ProbePolicy.hpp'
ADAPTER = 'Navi48PciProbe.cpp'
MUTATIONS = [
    ('absent-argument-admitted', POLICY, 'return present && value == 1;', 'return value == 1 || (present && false);', 1),
    ('non-one-argument-admitted', POLICY, 'return present && value == 1;', 'return present && value != 0;', 1),
] + [
    ('identity-field-%d-ignored' % i, POLICY,
     'if (identity[i] != kTarget[i]) return false;',
     'if (i != %du && identity[i] != kTarget[i]) return false;' % i, 1)
    for i in range(6)
] + [
    ('oversized-identity-admitted', POLICY, 'length != 4', 'length < 4', 1),
    ('resource-count-bound-lost', POLICY, 'length > kMaxAddressBytes', 'length > 2 * kMaxAddressBytes', 1),
    ('high-address-bits-lost', POLICY, 'r.base = (uint64_t(le32(p + 4)) << 32) | le32(p + 8);', 'r.base = le32(p + 8);', 1),
    ('overflow-admitted', POLICY, 'r.length > UINT64_MAX - r.base', 'false', 1),
    ('duplicate-register-admitted', POLICY, 'if (seen & bit)', 'if (false && (seen & bit))', 1),
    ('mixed-bdf-admitted', POLICY, 'else if (candidate.bdf != bdf)', 'else if (false && candidate.bdf != bdf)', 1),
    ('overlap-admitted', POLICY, 'if (memory(r.flags) == memory(other.flags) &&', 'if (false && memory(r.flags) == memory(other.flags) &&', 1),
    ('missing-required-bar-admitted', POLICY, 'if ((seen & required) != required)', 'if (false && ((seen & required) != required))', 1),
    ('plain-service-admitted', ADAPTER, '!OSDynamicCast(IOPCIDevice, provider)', 'provider == nullptr', 1),
    ('adapter-ignores-opt-in', ADAPTER, '!n48pci::enabled(present, value)', '(present && value == 1 && false)', 1),
    ('start-ignores-revalidation', ADAPTER, 'if (!readSnapshot(provider, snapshot)) return false;', 'readSnapshot(provider, snapshot);', 1),
    ('publishes-on-provider', ADAPTER, 'const bool published = setProperty(kSnapshotKey, report);', 'const bool published = provider->setProperty(kSnapshotKey, report);', 1),
    ('failed-report-leaks', ADAPTER, 'if (!ok) { report->release(); return nullptr; }', 'if (!ok) { return nullptr; }', 1),
    ('failed-start-not-balanced', ADAPTER, 'if (!published) { IOService::stop(provider); return false; }', 'if (!published) { return false; }', 1),
    ('stop-keeps-stale-snapshot', ADAPTER, 'removeProperty(kSnapshotKey);', 'if (false) removeProperty(kSnapshotKey);', 1),
    ('user-client-admitted', ADAPTER, 'return kIOReturnUnsupported;', 'return 0;', 2),
    ('retained-identity-leaks', ADAPTER, 'if (raw) raw->release();\n        if (!valid)', 'if (!valid)', 1),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        source_build = local_output(ROOT, args.build)
        output = local_output(ROOT, args.output)
    except ValueError as error:
        parser.error(str(error))
    if output.exists():
        parser.error('Output exists; use a fresh directory under out/')
    report_path = source_build / 'build-report.json'
    original = json.loads(report_path.read_text())
    if original['status'] != 'built-and-host-tested-not-load-qualified':
        parser.error('A successful build report is required')
    for group, folder in (('source_sha256', 'source'), ('test_sha256', 'tests')):
        for name, expected in original[group].items():
            if digest(source_build / folder / name) != expected:
                parser.error('Changed build input: ' + name)
    output.mkdir(parents=True)
    tests = output / 'tests'
    shutil.copytree(source_build / 'tests', tests)
    results = []
    report = {'build_report_sha256': digest(report_path), 'runner_sha256': digest(Path(__file__)),
              'kernel_executed': False, 'mutations': results}
    cases = [('baseline', None, None, None, None)] + MUTATIONS
    for name, filename, old, new, occurrences in cases:
        trial = output / name
        source = trial / 'source'
        shutil.copytree(source_build / 'source', source)
        if filename:
            path = source / filename
            text = path.read_text()
            if text.count(old) != occurrences:
                raise ValueError('Mutation does not apply exactly: ' + name)
            path.write_text(text.replace(old, new, 1))
        executable = trial / 'probe_test'
        command = ['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wshadow',
                   '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(tests / 'stubs'), '-I', str(source), str(source / ADAPTER),
                   str(tests / 'probe_test.cpp'), '-o', str(executable)]
        with (trial / 'compile.log').open('w') as log:
            compiled = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120).returncode
        result = {'name': name, 'compile_exit': compiled}
        if compiled:
            result['status'] = 'compile-rejected'
        else:
            with (trial / 'run.log').open('w') as log:
                try:
                    rc = subprocess.run([str(executable)], stdout=log, stderr=subprocess.STDOUT, timeout=30).returncode
                    result.update(run_exit=rc, status='passed' if rc == 0 else 'test-rejected')
                except subprocess.TimeoutExpired:
                    result['status'] = 'timeout-rejected'
        if name == 'baseline':
            report['baseline'] = result
            if result['status'] != 'passed':
                (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
                raise RuntimeError('Baseline did not pass; mutations have no evidentiary value')
        else:
            results.append(result)
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(name + ': ' + result['status'], flush=True)
    report['detected'] = sum(r['status'] != 'passed' for r in results)
    report['escaped'] = sum(r['status'] == 'passed' for r in results)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 1 if report['escaped'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
