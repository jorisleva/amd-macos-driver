"""Pinned native compute engine sources; no graphics hook/legacy service compiled."""
import io
from pathlib import Path
import subprocess
import tarfile

ENGINES = ('amdgpu_discovery.cpp', 'gmc_v12_0.cpp', 'nbif_v6_3_1.cpp', 'psp_v14_0.cpp',
           'smu_v14_0.cpp', 'imu_v12_0.cpp', 'rlc_v12_0.cpp', 'cp_v12_0.cpp',
           'mes_v12_1.cpp', 'gfx_v12_0.cpp', 'sdma_v7_0.cpp', 'amdgpu_gart.cpp',
           'amdgpu_doorbell.cpp', 'fw_loader.cpp', 'compute_test.cpp', 'amdgpu_ucode_extract.cpp')


def export_compute(root, target, revision):
    repo = root / 'out/dependencies/Navi48-MacOS'
    archive = subprocess.check_output(['git', '-C', repo, 'archive', revision,
                                      'src/navi48-bringup/src', 'src/navi48-bringup/shaders'], timeout=60)
    prefix = 'src/navi48-bringup/src/'
    selected = {'amd/' + name for name in ENGINES} | {'amd/fw_table.cpp'}
    shaders = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as bundle:
        for member in bundle.getmembers():
            if member.isfile() and member.name.startswith('src/navi48-bringup/shaders/'):
                name = member.name.split('/')[-1]
                if name in ('store_magic.bin', 'store_magic_hsa.bin', 'store_magic.s', 'store_magic_hsa.s'):
                    data = bundle.extractfile(member).read()
                    path = target / 'shaders' / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
                    if name.endswith('.bin'): shaders[name[:-4]] = data
                continue
            name = member.name.removeprefix(prefix)
            if member.isfile() and member.name.startswith(prefix) and (name in selected or name.endswith('.h') or name.endswith('.hpp')):
                path = target / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(bundle.extractfile(member).read())
    if any(not (target / name).is_file() for name in selected):
        raise ValueError('Missing pinned native engine')
    if set(shaders) != {'store_magic', 'store_magic_hsa'}:
        raise ValueError('Missing pinned shader bytecode')
    shader_c = ['#include <stdint.h>', 'extern "C" {']
    for name, data in sorted(shaders.items()):
        symbol = 'fw_shader_' + name
        shader_c += ['extern const uint8_t ' + symbol + '[];', 'extern const uint64_t ' + symbol + '_size;',
                     'alignas(4096) const uint8_t ' + symbol + '[] = {' + ','.join(str(b) for b in data) + '};',
                     'const uint64_t ' + symbol + '_size = ' + str(len(data)) + ';']
    shader_c.append('}')
    (target / 'compute_shaders.cpp').write_text('\n'.join(shader_c) + '\n')
    selected.add('compute_shaders.cpp')
    # Changes are explicit/reproducible, the unmodified archive remains pinned.
    (target / 'amd/amdgpu_regs.h').write_text('#pragma once\n#include "ComputeAccess.hpp"\n')
    (target / 'n48log.h').write_text('#pragma once\n#include "NativeLog.hpp"\n')
    (target / 'amd/amdgpu_sysmem.h').write_text('#pragma once\n#include "ComputeSysMem.hpp"\n')
    path = target / 'amd/gmc_v12_0.cpp'
    text = path.read_text()
    old = '        uint64_t pa = busAddr + (uint64_t)i * kAMDGPUGPUPageSize;'
    if text.count(old) != 1:
        raise ValueError('Pinned GMC binding changed')
    text = text.replace(old, '''        uint64_t pa = 0;
        if (!sysmem_iovm_page(busAddr, i, pa)) return kIOReturnNotReady;
        // Real IODMACommand IOVM per page; NEVER CPU physical / contiguous inference.''')
    old_hi = 'if (dev.vramSizeBytes > dev.vramLimit + kVramHiTotalReserve) {'
    if text.count(old_hi) != 1:
        raise ValueError('Pinned high-VRAM pool changed')
    text = text.replace(old_hi, 'if (false) { // native trial: NO allocation outside explicit scratch')
    path.write_text(text)
    return sorted(selected)
