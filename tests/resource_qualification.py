#!/usr/bin/env python3
"""Offline ARM resource qualification. Requires already provisioned SDK/Zephyr.

Writes a fresh output directory; never installs dependencies or runs firmware.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile

from verify_firmware_link import verify
from nsx_firmware_integration import SDK_REVISION
from zephyr_integration import ZEPHYR_SHA, PROJECTS

ROOT = Path(__file__).resolve().parents[1]
# Explicit reviewed production snapshot; changing this pin requires new evidence.
PRODUCTION_BASELINE = 'fdabd7756068dd2f8586d610c4489236d41b4883'
DRIVERS = ('bme688', 'ledstick', 'ina228', 'mpu6050', 'max86150', 'icm42670', 'icm45605', 'tsl254x', 't5838')


def parse_sizes(output):
    result = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[0].startswith('resource_size_'):
            result[fields[0].removeprefix('resource_size_')] = int(fields[3], 16)
    return result


def external_inputs(sdk_root, zephyr_workspace, run):
    """Require actual clean repository roots at the integration fixtures' pins."""
    inputs = {'sdk': (sdk_root, SDK_REVISION),
              'zephyr': (zephyr_workspace / 'zephyr', ZEPHYR_SHA)}
    inputs.update({name: (zephyr_workspace / relative, revision)
                   for name, (relative, revision) in PROJECTS.items()})
    evidence = {}
    for name, (path, expected) in inputs.items():
        path = path.resolve()
        if not path.is_dir():
            raise ValueError(f'{name}: missing checkout {path}')
        top = Path(run(['git', '-C', path, 'rev-parse', '--show-toplevel']).strip()).resolve()
        if top != path:
            raise ValueError(f'{name}: expected a repository root at {path}')
        revision = run(['git', '-C', path, 'rev-parse', 'HEAD']).strip()
        if revision != expected:
            raise ValueError(f'{name}: expected revision {expected}, found {revision}')
        status = run(['git', '-C', path, 'status', '--porcelain', '--untracked-files=all'])
        if status.strip():
            raise ValueError(f'{name}: checkout must be clean, including untracked files')
        evidence[name] = {'path': str(path), 'revision': revision, 'status': status}
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--arm-prefix', required=True)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--zephyr-workspace', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    raw = out / 'raw'
    raw.mkdir()
    env = os.environ.copy()
    env['PATH'] = str(Path(args.arm_prefix).parent) + os.pathsep + env['PATH']
    commands = []

    def run(command, cwd=ROOT, environment=None):
        command = [str(x) for x in command]
        record = {'command': command, 'cwd': str(cwd)}
        if environment:
            record['environment_overrides'] = {k: v for k, v in environment.items() if env.get(k) != v}
        commands.append(record)
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        completed = subprocess.run(command, cwd=cwd, env=environment or env, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        (raw / f'command-{len(commands):03}.txt').write_text(completed.stdout)
        if completed.returncode:
            raise RuntimeError(completed.stdout)
        return completed.stdout

    prefix = args.arm_prefix
    dependencies = external_inputs(args.sdk_root, args.zephyr_workspace, run)
    report = {'schema_version': 1, 'source_revision': run(['git', 'rev-parse', 'HEAD']).strip(),
              'compiler': run([prefix + 'gcc', '--version']).splitlines()[0],
              'sdk_revision': dependencies['sdk']['revision'],
              'external_inputs': dependencies,
              'zephyr_revision': dependencies['zephyr']['revision'],
              'links': {}, 'layouts': {}, 'framework_links': {}, 'adapter_objects': {},
              'scope': 'Static all-API retention links; no startup, contexts, adapters, heap or runtime stack in portable ELF.',
              'production_baseline': PRODUCTION_BASELINE,
              'working_tree_status': run(['git', 'status', '--porcelain']),
              'input_sha256': {}}
    for directory in ('src', 'include', 'vendor', 'cmake', 'tests/resources'):
        for file in sorted((ROOT / directory).rglob('*')):
            if file.is_file():
                report['input_sha256'][str(file.relative_to(ROOT))] = hashlib.sha256(file.read_bytes()).hexdigest()
    for relative in ('CMakeLists.txt', 'tests/resource_qualification.py', 'tests/nsx_firmware_integration.py',
                     'tests/zephyr_integration.py', 'zephyr/CMakeLists.txt'):
        report['input_sha256'][relative] = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
    report['production_diff_from_baseline'] = run(['git', 'diff', PRODUCTION_BASELINE, '--',
        'src', 'include', 'vendor', 'cmake', 'CMakeLists.txt', 'zephyr'])
    if report['production_diff_from_baseline']:
        raise RuntimeError('Production inputs differ from qualification baseline')
    portable = out / 'portable'
    run(['cmake', '-S', ROOT / 'tests/resources', '-B', portable,
         '-DCMAKE_C_COMPILER=' + prefix + 'gcc', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
         '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    run(['cmake', '--build', portable, '--parallel', '4'])
    for name in ('baseline', *DRIVERS, 'aggregate'):
        elf = portable / name
        if name != 'baseline':
            verify(elf, list(DRIVERS) if name == 'aggregate' else [name], prefix + 'nm', portable / (name + '.map'))
        size = run([prefix + 'size', elf]).splitlines()[1].split()
        text, data, bss = map(int, size[:3])
        report['links'][name] = {'text': text, 'data': data, 'bss': bss,
            'flash_text_plus_data': text + data, 'static_ram_data_plus_bss': data + bss,
            'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest()}
        shutil.copyfile(portable / (name + '.map'), raw / (name + '.map'))
        (raw / (name + '.nm')).write_text(run([prefix + 'nm', '-S', '--defined-only', '--format=posix', elf]))
    for name, row in report['links'].items():
        row['incremental_flash_from_baseline'] = row['flash_text_plus_data'] - report['links']['baseline']['flash_text_plus_data']
    # Compile against each framework's actual production target compile command.
    nsx = out / 'nsx'
    run(['cmake', '-S', ROOT / 'tests/nsx_firmware', '-B', nsx,
         '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'tests/nsx_firmware/toolchain.cmake'),
         '-DNSX_SDK_ROOT=' + str(args.sdk_root), '-DFIRMWARE_MODE=baremetal',
         '-DCMAKE_C_FLAGS=-Os -fstack-usage', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    run(['cmake', '--build', nsx, '--parallel', '4'])
    zephyr = out / 'zephyr'
    zenv = env.copy()
    zenv.update(ZEPHYR_BASE=str(args.zephyr_workspace / 'zephyr'), ZEPHYR_TOOLCHAIN_VARIANT='cross-compile',
                CROSS_COMPILE=prefix, PATH=str(args.zephyr_workspace / 'venv/bin') + os.pathsep + env['PATH'])
    modules = [args.zephyr_workspace / 'modules/hal/cmsis', args.zephyr_workspace / 'modules/hal/cmsis_6', ROOT]
    run(['cmake', '-GNinja', '-S', ROOT / 'tests/zephyr', '-B', zephyr,
         '-DBOARD=qemu_cortex_m3', '-DTOOLCHAIN_HAS_NEWLIB=ON',
         '-DZEPHYR_MODULES=' + ';'.join(map(str, modules)),
         '-DEXTRA_CFLAGS=-fstack-usage', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], environment=zenv)
    run(['cmake', '--build', zephyr, '--parallel', '4'], environment=zenv)
    for label, build, suffix, define in [('portable', portable, '/tests/resources/layout.c', None),
            ('nsx', nsx, '/tests/nsx_firmware/main.c', 'RESOURCE_NSX'),
            ('zephyr', zephyr, '/src/ports/zephyr.c', 'RESOURCE_ZEPHYR')]:
        entries = json.loads((build / 'compile_commands.json').read_text())
        if label != 'portable':
            adapter = next(e for e in entries if e['file'].endswith('/src/ports/' + label + '.c'))
            adapter_command = shlex.split(adapter['command'])
            adapter_obj = Path(adapter['directory']) / adapter_command[adapter_command.index('-o') + 1]
            firmware = build / ('firmware.elf' if label == 'nsx' else 'zephyr/zephyr.elf')
            for target, destination in ((adapter_obj, 'adapter_objects'), (firmware, 'framework_links')):
                columns = run([prefix + 'size', target]).splitlines()[1].split()
                report[destination][label] = dict(zip(('text', 'data', 'bss'), map(int, columns[:3])))
            firmware_map = build / ('firmware.map' if label == 'nsx' else 'zephyr/zephyr.map')
            shutil.copyfile(firmware_map, raw / (label + '-firmware.map'))
            if label == 'zephyr':
                shutil.copyfile(build / 'zephyr/.config', raw / 'zephyr.config')
        entry = next(e for e in entries if e['file'].endswith(suffix))
        command = shlex.split(entry['command'])
        obj = raw / (label + '-layout.o')
        command[command.index('-o') + 1] = str(obj)
        command[command.index('-c') + 1] = str(ROOT / 'tests/resources/layout.c')
        if define:
            command.append('-D' + define)
        run(command, cwd=Path(entry['directory']), environment=zenv if label == 'zephyr' else None)
        nm = run([prefix + 'nm', '-S', '--defined-only', '--format=posix', obj])
        (raw / (label + '-layout.nm')).write_text(nm)
        report['layouts'][label] = parse_sizes(nm)
        expected = set(DRIVERS) | {'bus', 'runtime'}
        if label == 'nsx':
            expected |= {'nsx_adapter', 'nsx_scratch'}
        if label == 'zephyr':
            expected |= {'zephyr_adapter', 'zephyr_tx', 'zephyr_rx'}
        if set(report['layouts'][label]) != expected or not all(report['layouts'][label].values()):
            raise RuntimeError('Incomplete layout evidence: ' + label)
        shutil.copyfile(build / 'compile_commands.json', raw / (label + '-compile_commands.json'))
        for file in build.rglob('*.su'):
            dest = raw / 'stack' / label / file.relative_to(build)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, dest)
    if external_inputs(args.sdk_root, args.zephyr_workspace, run) != dependencies:
        raise RuntimeError('External inputs changed during qualification')
    # Raw reports, maps, commands and stack frames are portable evidence; ELF/builds
    # remain in the requested output directory for deeper local inspection.
    with tarfile.open(out / 'raw.tar.gz', 'w:gz') as archive:
        archive.add(raw, arcname='raw')
        archive.add(out / 'commands.json', arcname='commands.json')
    report['raw_archive_sha256'] = hashlib.sha256((out / 'raw.tar.gz').read_bytes()).hexdigest()
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(out / 'report.json')


if __name__ == '__main__':
    main()
