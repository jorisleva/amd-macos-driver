#!/usr/bin/env python3
"""Compile the owned offscreen corpus on macOS, or check it before Windows use."""
import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
from pathlib import Path

SHADERS = {'fullscreen.vert': ('vertex', 'Vertex', 'fullscreen_vertex'),
           'texture.frag': ('fragment', 'Fragment', 'texture_fragment'),
           'triangle.vert': ('vertex', 'Vertex', 'triangle_vertex'),
           'solid.frag': ('fragment', 'Fragment', 'solid_fragment')}
DRAW_LAYOUT = {'Struct': [{'offset': 0, 'ty': {'Vec': {'scalar': 'Float', 'lanes': 4}}}]
               + [{'offset': offset, 'ty': {'Scalar': 'UInt'}} for offset in (16, 20, 24, 28)]}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def spirv_info(path):
    data = path.read_bytes()
    require(len(data) >= 20 and len(data) % 4 == 0, 'Invalid SPIR-V length')
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    require(words[0] == 0x07230203 and words[1] <= 0x00010500 and words[3] and not words[4], 'Invalid SPIR-V header')
    sets, bindings, capabilities, entries = {}, {}, [], []
    i = 5
    while i < len(words):
        length, op = words[i] >> 16, words[i] & 0xffff
        require(length > 0 and i + length <= len(words), 'Malformed SPIR-V instruction')
        args = words[i + 1:i + length]
        if op == 17:
            require(len(args) == 1, 'Malformed capability')
            capabilities.append(args[0])
        elif op == 15:
            require(len(args) >= 3, 'Malformed entry point')
            raw = struct.pack('<' + 'I' * len(args[2:]), *args[2:])
            require(b'\0' in raw, 'Unterminated entry name')
            entries.append((args[0], raw.split(b'\0', 1)[0].decode()))
        elif op == 71 and len(args) == 3:
            if args[1] == 34:
                sets[args[0]] = args[2]
            if args[1] == 33:
                bindings[args[0]] = args[2]
        i += length
    require(sets.keys() == bindings.keys(), 'Descriptor set/binding mismatch')
    return entries, sorted((sets[key], value) for key, value in bindings.items()), capabilities


def check_corpus(directory, verify_hashes=True):
    directory = Path(directory)
    if verify_hashes:
        for line in (directory / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split(None, 1)
            require(Path(name).name == name, 'Nonlocal manifest path')
            require(sha(directory / name) == digest, 'SHA-256 mismatch: ' + name)
    contract = {}
    for name, (_, stage, entry) in SHADERS.items():
        meta = json.loads((directory / (name + '.reflection.json')).read_text())
        require(meta['reflection_version'] == 56 and meta['stage'] == stage and meta['entry_point'] == entry, name + ': stage/schema/entry')
        require(not meta['vertex_attributes'] and not meta['varyings'] and not meta['function_constants'], name + ': unexpected stage interface')
        resources = meta['bindings']
        descriptors = sorted((item['descriptor']['set'], item['descriptor']['binding']) for item in resources)
        expected = [] if name == 'fullscreen.vert' else [(0, 32), (0, 160)] if name == 'texture.frag' else [(0, 0)]
        require(descriptors == expected and all(item['descriptor']['count'] == 1 for item in resources), name + ': descriptor ABI')
        if stage == 'Vertex':
            require(meta['vertex_builtins'] == {'uses_vertex_index': True, 'uses_instance_index': False, 'writes_position': True}, name + ': vertex builtins')
        else:
            require(meta['render_targets'] == [{'member_index': 0, 'location': 0, 'type_name': 'float4'}], name + ': RGBA output')
        if name == 'texture.frag':
            require([item['kind'] for item in resources] == ['Texture', 'Sampler'], 'Texture/sampler kinds')
            shape = resources[0]['texture_shape']
            require(shape['dimension'] == 'D2' and shape['component'] == 'Float' and not shape['arrayed'] and not shape['multisampled'] and not shape['writable'], 'Texture shape')
            require(resources[0]['access'] == 'Sampled', 'Texture access')
        elif resources:
            item = resources[0]
            require(item['kind'] == 'Buffer' and item['declared_size'] == 32 and item['type_layout'] == DRAW_LAYOUT and item['access'] == 'ReadOnly', name + ': DrawParams layout/access')
        entries, emitted_descriptors, caps = spirv_info(directory / (name + '.spv'))
        require(entries == [(0 if stage == 'Vertex' else 4, 'main')], name + ': SPIR-V entry')
        require(emitted_descriptors == descriptors, name + ': reflection disagrees with emitted SPIR-V')
        require(1 in caps and set(caps) <= {1, 39, 50}, name + ': unsupported capability (bindless profile refused)')
        require(39 not in caps or name == 'texture.frag', name + ': unexpected Int8')
        contract[name] = {'stage': stage, 'air_entry_point': entry, 'spirv_entry_point': 'main',
                          'descriptors': descriptors, 'capabilities': caps, 'requires_shader_int8': 39 in caps}
    return contract


def compile_corpus(repo, out):
    require(platform.system() == 'Darwin', 'Apple Metal compilation requires macOS')
    require(not out.exists() or (out.is_dir() and not any(out.iterdir())), 'Choose a new or empty output directory')
    tools = {name: shutil.which(os.environ.get(override, name)) for name, override in
             [('llvm-dis', 'METAL2VULKAN_LLVM_DIS'), ('spirv-val', 'METAL2VULKAN_SPIRV_VAL'), ('spirv-dis', 'METAL2VULKAN_SPIRV_DIS')]}
    tools['metal2vulkan'] = os.environ.get('METAL2VULKAN_BIN', str(repo / 'out/translator/debug/metal2vulkan'))
    require(all(path and os.access(path, os.X_OK) for path in tools.values()), 'Missing translator/LLVM/SPIR-V tool; set the documented overrides')
    subprocess.run(['xcrun', '--sdk', 'macosx', 'metal', '--version'], check=True, stdout=subprocess.DEVNULL)
    out.mkdir(parents=True, exist_ok=True)
    out = out.resolve()
    env = dict(os.environ, METAL2VULKAN_LLVM_DIS=tools['llvm-dis'], METAL2VULKAN_SPIRV_VAL=tools['spirv-val'], NVMTL_NO_BINDLESS_ALL='1')
    target = os.environ.get('METAL_REFERENCE_TARGET', 'air64-apple-macosx26.0')
    standard = os.environ.get('METAL_REFERENCE_STD', 'metal4.0')
    names = []
    try:
        with (out / 'build.log').open('w') as log:
            def run(args, **kwargs):
                subprocess.run(args, check=True, env=env, stdout=log, stderr=subprocess.STDOUT, **kwargs)
            for name, (stage, _, _) in SHADERS.items():
                shutil.copyfile(repo / 'tests/shaders/graphics' / (name + '.metal'), out / (name + '.metal'))
                with (out / (name + '.metal')).open('rb') as source:
                    run(['xcrun', '--sdk', 'macosx', 'metal', '-x', 'metal', '-O2', '-target', target, '-std=' + standard, '-c', '-', '-o', str(out / (name + '.air'))], stdin=source)
                run([tools['llvm-dis'], str(out / (name + '.air')), '-o', str(out / (name + '.air.ll'))])
                ll = out / (name + '.air.ll')
                ll.write_text('\n'.join(line for line in ll.read_text().splitlines() if not line.startswith('; ModuleID =')) + '\n')
                run(['xcrun', '--sdk', 'macosx', 'metallib', str(out / (name + '.air')), '-o', str(out / (name + '.metallib'))])
                run([tools['metal2vulkan'], str(out / (name + '.air')), str(out / (name + '.spv')), '--stage', stage, '--emit-meta', str(out / (name + '.reflection.json'))])
                run([tools['spirv-val'], '--target-env', 'vulkan1.2', str(out / (name + '.spv'))])
                run([tools['spirv-dis'], str(out / (name + '.spv')), '-o', str(out / (name + '.spvasm'))])
                names += [name + ext for ext in ('.metal', '.air', '.air.ll', '.metallib', '.spv', '.spvasm', '.reflection.json')]
            contract = check_corpus(out, verify_hashes=False)
            for name in names:
                require(b'/Users/' not in (out / name).read_bytes(), 'Personal path in ' + name)
            def capture(*args):
                return subprocess.check_output(args, text=True).strip()
            inputs = ['tools/compile-metal-graphics.py'] + ['tests/shaders/graphics/' + name + '.metal' for name in SHADERS]
            source_names = capture('git', '-C', str(repo), 'ls-files', '--cached', '--others', '--exclude-standard', 'translator/translator').splitlines()
            fingerprint = hashlib.sha256(''.join(f'{name}\0{sha(repo / name)}\n' for name in sorted(source_names)).encode()).hexdigest()
            versions = {name: capture(path, '--version') for name, path in tools.items() if name != 'metal2vulkan'}
            versions.update(metal='\n'.join(line for line in capture('xcrun', '--sdk', 'macosx', 'metal', '--version').splitlines() if not line.startswith('InstalledDir:')),
                            metallib=capture('xcrun', '--sdk', 'macosx', 'metallib', '--version'), xcode=capture('xcodebuild', '-version'),
                            macos_sdk=capture('xcrun', '--sdk', 'macosx', '--show-sdk-version'), rustc=capture('rustc', '--version'))
            provenance = {'schema_version': 1, 'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                          'code_revision': capture('git', '-C', str(repo), 'rev-parse', 'HEAD'),
                          'working_tree': capture('git', '-C', str(repo), 'status', '--porcelain').splitlines(),
                          'machine': {'model': capture('sysctl', '-n', 'hw.model'), 'architecture': platform.machine(),
                                      'macos_version': capture('sw_vers', '-productVersion'), 'macos_build': capture('sw_vers', '-buildVersion')},
                          'versions': versions, 'tools': [{'name': name, 'sha256': sha(path)} for name, path in tools.items()],
                          'options': {'air_target': target, 'metal_standard': standard, 'optimization': '-O2', 'source_input': 'stdin',
                                      'NVMTL_NO_BINDLESS_ALL': '1', 'validation_target': 'vulkan1.2'},
                          'contract': contract, 'draw_params_size': 32, 'draw_params_offsets': [0, 16, 20, 24, 28],
                          'translator_sources_sha256': fingerprint,
                          'inputs': [{'name': name, 'sha256': sha(repo / name)} for name in inputs],
                          'artifacts': [{'name': name, 'sha256': sha(out / name), 'size_bytes': (out / name).stat().st_size} for name in names],
                          'results': {'compilation': 'passed', 'translation': 'passed', 'spirv_validation': 'passed',
                                      'reflection_spirv_contract': 'passed', 'metal_execution': 'not-run', 'radeon_execution': 'not-run'}}
            text = json.dumps(provenance, indent=2) + '\n'
            require('/Users/' not in text, 'Personal path in provenance')
            (out / 'provenance.json').write_text(text)
            (out / 'SHA256SUMS').write_text(''.join(f'{sha(out / name)}  {name}\n' for name in names + ['provenance.json']))
            (out / 'status.txt').write_text('passed (software only; no GPU execution)\n')
    except Exception:
        (out / 'status.txt').write_text('failed\n')
        raise
    return check_corpus(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, nargs='?', default=Path('out/metal-graphics'))
    parser.add_argument('--check', action='store_true', help='verify published hashes, reflection and SPIR-V without compilation or GPU use')
    args = parser.parse_args()
    try:
        contract = check_corpus(args.directory) if args.check else compile_corpus(Path(__file__).resolve().parent.parent, args.directory)
        print(json.dumps(contract, indent=2))
        print('PASS: four graphics shaders; no GPU execution')
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'FAILED: {error}\n')


if __name__ == '__main__':
    main()
