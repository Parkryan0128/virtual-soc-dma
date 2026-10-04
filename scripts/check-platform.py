#!/usr/bin/env python3
"""Verify the generated DMA device tree and reject unsupported boards."""
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
QEMU = str(ROOT/'build/qemu/qemu-system-riscv32')

def require(condition, message):
    # Unlike Python assert, these validation gates survive PYTHONOPTIMIZE.
    if not condition:
        raise AssertionError(message)

def nodes(data):
    magic, _, structure, strings = struct.unpack_from('>4I', data)
    require(magic == 0xd00dfeed, 'invalid FDT magic')
    pos, stack, result = structure, [], {}
    while True:
        token, = struct.unpack_from('>I', data, pos)
        pos += 4
        if token == 1:
            end = data.index(0, pos)
            stack.append(data[pos:end].decode())
            pos = (end + 4) & ~3
            result['/'.join(stack)] = {}
        elif token == 2:
            stack.pop()
        elif token == 3:
            size, offset = struct.unpack_from('>2I', data, pos)
            pos += 8
            end = data.index(0, strings + offset)
            name = data[strings + offset:end].decode()
            result['/'.join(stack)][name] = data[pos:pos + size]
            pos = (pos + size + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            return result
        else:
            raise AssertionError(f'unknown FDT token {token}')


def main():
    artifacts = ROOT/'artifacts/platform'
    artifacts.mkdir(parents=True, exist_ok=True)
    for enabled in [False, True]:
        out = artifacts/('dma.dtb' if enabled else 'stock.dtb')
        machine = f'virt,aia=none,dumpdtb={out}' + (',dma=on' if enabled else '')
        subprocess.run([QEMU, '-M', machine, '-cpu', 'rv32', '-smp', '1', '-m', '128M', '-bios', 'none', '-display', 'none'], check=True, capture_output=True, timeout=30)
        tree = nodes(out.read_bytes())
        key = '/soc/dma@10010000'
        require((key in tree) == enabled, 'DMA presence does not match board option')
        if enabled:
            device = tree[key]
            require(device['compatible'] == b'vsoc,virtual-dma-v1\0', 'DMA compatible mismatch')
            require(struct.unpack('>4I', device['reg']) == (0, 0x10010000, 0, 0x1000), 'DMA MMIO mismatch')
            require(struct.unpack('>I', device['interrupts']) == (16,), 'DMA IRQ mismatch')
            phandle = device['interrupt-parent']
            require(any(props.get('phandle') == phandle and b'plic' in props.get('compatible', b'') for props in tree.values()), 'DMA IRQ parent is not the PLIC')
    rejected = [(args, 'virtual DMA requires') for args in
                [('-smp', '2'), ('-m', '64M'), ('-M', 'virt,dma=on,aia=aplic')]]
    rejected += [(('-global', f'virtual-dma.delay-ns={delay}'), 'delay-ns must be')
                 for delay in (0, 1000000001, 18446744073709551615)]
    for args, expected in rejected:
        cmd = [QEMU, '-M', 'virt,dma=on,aia=none', '-cpu', 'rv32', '-smp', '1', '-m', '128M', '-bios', 'none', '-display', 'none']
        cmd += args
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        require(proc.returncode != 0 and expected in proc.stderr, proc.stderr)
    print('PASS platform DTB and configuration constraints')

if __name__ == '__main__':
    main()
