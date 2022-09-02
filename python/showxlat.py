#!/usr/bin/env python
# vim:sw=4 ts=4 et

# A version of examples/showxlat.c translated to Python.
# The script is not meant to be particularly useful, but it should
# demonstrate use of the libaddrxlat Python API.
#
# This file is in public domain.

from __future__ import print_function

import sys
import kdumpfile
import addrxlat

def addrspace_str(addrspace):
    if addrspace == addrxlat.KPHYSADDR:
        return 'KPHYSADDR'
    elif addrspace == addrxlat.MACHPHYSADDR:
        return 'MACHPHYSADDR'
    elif addrspace == addrxlat.KVADDR:
        return 'KVADDR'
    elif addrspace == addrxlat.NOADDR:
        return 'NOADDR'
    else:
        return '<addrspace {:d}>'.format(addrspace)

def fulladdr_str(addr):
    result = addrspace_str(addr.addrspace)
    if addr.addrspace != addrxlat.NOADDR:
        result += ':0x{:x}'.format(addr.addr)
    return result

def print_target_as(meth):
    print('  target_as={}'.format(addrspace_str(meth.target_as)))

def print_linear(meth):
    print('LINEAR')
    print_target_as(meth)
    print('  off=0x{:x}'.format(meth.off))

pte_formats = {
    addrxlat.PTE_NONE: 'none',
    addrxlat.PTE_PFN32: 'pfn32',
    addrxlat.PTE_PFN64: 'pfn64',
    addrxlat.PTE_AARCH64: 'aarch64',
    addrxlat.PTE_AARCH64_LPA: 'aarch64_lpa',
    addrxlat.PTE_AARCH64_LPA2: 'aarch64_lpa2',
    addrxlat.PTE_IA32: 'ia32',
    addrxlat.PTE_IA32_PAE: 'ia32_pae',
    addrxlat.PTE_X86_64: 'x86_64',
    addrxlat.PTE_S390X: 's390x',
    addrxlat.PTE_PPC64_LINUX_RPN30: 'ppc64_linux_rpn30',
}

def print_pgt(meth):
    print('PGT')
    print_target_as(meth)
    print('  root={}'.format(fulladdr_str(meth.root)))
    fmt = pte_formats.get(meth.pte_format, meth.pte_format)
    print('  pte_format={}'.format(fmt))
    print('  fields={}'.format(','.join(str(i) for i in meth.fields)))

def print_lookup(meth):
    print('LOOKUP')
    print_target_as(meth)
    print('  endoff=0x{:x}'.format(meth.endoff))
    for elem in meth.tbl:
        print('  {:x} -> {:x}'.format(elem[0], elem[1]))

def print_memarr(meth):
    print('MEMARR')
    print_target_as(meth)
    print('  base={}'.format(fulladdr_str(meth.base)))
    print('  shift={}'.format(meth.shift))
    print('  elemsz={}'.format(meth.elemsz))
    print('  valsz={}'.format(meth.valsz))

def print_meth(system, name):
    meth = system.get_meth(addrxlat.__dict__['SYS_METH_{}'.format(name)])
    if meth.kind == addrxlat.NOMETH:
        return

    print('METH_{}: '.format(name), end='')

    if meth.kind == addrxlat.CUSTOM:
        print('CUSTOM')
    elif meth.kind == addrxlat.LINEAR:
        print_linear(meth)
    elif meth.kind == addrxlat.PGT:
        print_pgt(meth)
    elif meth.kind == addrxlat.LOOKUP:
        print_lookup(meth)
    elif meth.kind == addrxlat.MEMARR:
        print_memarr(meth)
    else:
        print('<meth kind {:d}>'.format(meth.kind))

    print()

def print_map(system, name):
    print('MAP_{}:'.format(name))

    map = system.get_map(addrxlat.__dict__['SYS_MAP_{}'.format(name)])
    if map is None:
        return

    addr = 0
    for range in map:
        if range.meth in meth_names:
            name = meth_names[range.meth]
        else:
            name = '{:d}'.format(range.meth)
        print('{:x}-{:x}: {}'.format(addr, addr + range.endoff, name))
        addr += range.endoff + 1

def dump_addrxlat(ctx):
    system = k.get_addrxlat_sys()

    print_meth(system, 'PGT')
    print_meth(system, 'UPGT')
    print_meth(system, 'DIRECT')
    print_meth(system, 'KTEXT')
    print_meth(system, 'VMEMMAP')
    print_meth(system, 'RDIRECT')
    print_meth(system, 'MACHPHYS_KPHYS')
    print_meth(system, 'KPHYS_MACHPHYS')

    print_map(system, 'HW')
    print()
    print_map(system, 'KV_PHYS')
    print()
    print_map(system, 'KPHYS_DIRECT')
    print()
    print_map(system, 'MACHPHYS_KPHYS')
    print()
    print_map(system, 'KPHYS_MACHPHYS')

if __name__ == '__main__':
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print('Usage: {} <dumpfile> [<ostype>]'.format(sys.argv[0]),
              file=sys.stderr)
        exit(1)

    k = kdumpfile.kdumpfile(sys.argv[1])

    meth_names = {}
    for name, val in addrxlat.__dict__.items():
        if name.startswith('SYS_METH_') and name != 'SYS_METH_NUM':
            meth_names[val] = name[9:]

    if len(sys.argv) > 2:
        k.attr['addrxlat.ostype'] = sys.argv[2]

    dump_addrxlat(k)
