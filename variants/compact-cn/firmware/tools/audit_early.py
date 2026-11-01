#!/usr/bin/env python3
"""Disassemble V2 early entry and assert guarded Ralink prom has no non-stack loads."""
import argparse,hashlib,json
from pathlib import Path
from capstone import Cs,CS_ARCH_MIPS,CS_MODE_MIPS32,CS_MODE_LITTLE_ENDIAN
from capstone.mips import MIPS_OP_MEM,MIPS_OP_REG,MIPS_OP_IMM,MIPS_REG_SP
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser();p.add_argument('vmlinux',type=Path);p.add_argument('config',type=Path);a=p.parse_args()
c=a.config.read_text();assert 'CONFIG_MER1200G_DTB_ONLY=y' in c and 'CONFIG_MIPS_RAW_APPENDED_DTB=y' in c and 'CONFIG_MIPS_CMDLINE_FROM_DTB=y' in c and '# CONFIG_KEXEC is not set' in c and '# CONFIG_MIPS_CMDLINE_FROM_BOOTLOADER is not set' in c and 'CONFIG_BOOT_RAW=y' in c
with a.vmlinux.open('rb') as f:
 e=ELFFile(f);syms=e.get_section_by_name('.symtab'); md=Cs(CS_ARCH_MIPS,CS_MODE_MIPS32|CS_MODE_LITTLE_ENDIAN);md.detail=True
 def sym(n):
  v=syms.get_symbol_by_name(n)
  if not v:raise ValueError('missing symbol '+n)
  return v[0]
 def extract(name):
  z=sym(name);address=z['st_value'];size=z['st_size']
  if not size and name=='__kernel_entry':size=8
  if not size:raise ValueError('zero-size symbol '+name)
  for seg in e.iter_segments():
   h=seg.header
   if h.p_type=='PT_LOAD' and h.p_vaddr<=address and address+size<=h.p_vaddr+h.p_filesz:
    f.seek(h.p_offset+address-h.p_vaddr);return address,list(md.disasm(f.read(size),address))
  raise ValueError('symbol outside PT_LOAD '+name)
 result={}
 for name in ['__kernel_entry','kernel_entry','prom_init','prom_soc_init','plat_mem_setup']:
  address,ins=extract(name);result[name]={'address':hex(address),'instructions':[f'{i.address:08x}: {i.bytes.hex()} {i.mnemonic} {i.op_str}' for i in ins]}
  if name in ('prom_init','prom_soc_init','plat_mem_setup'):
   known={}; forbidden={int(sym(x)['st_value']) for x in ('fw_arg0','fw_arg1','fw_arg2','fw_arg3')}
   for inst in ins:
    ops=inst.operands
    if inst.mnemonic=='lui' and len(ops)==2 and ops[0].type==MIPS_OP_REG and ops[1].type==MIPS_OP_IMM:known[ops[0].reg]=(ops[1].imm<<16)&0xffffffff
    elif inst.mnemonic in ('addiu','ori') and len(ops)==3 and ops[0].type==MIPS_OP_REG and ops[1].type==MIPS_OP_REG and ops[2].type==MIPS_OP_IMM:
     if ops[1].reg in known:known[ops[0].reg]=((known[ops[1].reg]+ops[2].imm) if inst.mnemonic=='addiu' else (known[ops[1].reg]|ops[2].imm))&0xffffffff
     else:known.pop(ops[0].reg,None)
    if inst.mnemonic in ('lw','lwl','lwr','lbu','lb','lhu','lh','ld'):
     for op in ops:
      if op.type==MIPS_OP_MEM and op.mem.base in known and ((known[op.mem.base]+op.mem.disp)&0xffffffff) in forbidden:raise ValueError('direct firmware argument load in '+name+': '+inst.op_str)
  if name=='prom_init':
   assert len(ins)<=24,'prom_init unexpectedly large'
   for i in ins:
    if i.mnemonic in ('lw','lbu','lb','lhu','lh','ld'):
     for op in i.operands:
      if op.type==MIPS_OP_MEM and op.mem.base!=MIPS_REG_SP:raise ValueError('non-stack memory load in prom_init: '+i.mnemonic+' '+i.op_str)
   assert any(i.mnemonic=='jal' and int(i.op_str,16)==sym('prom_soc_init')['st_value'] for i in ins),'prom_soc_init call not seen'
 print(json.dumps({'vmlinux_sha256':hashlib.sha256(a.vmlinux.read_bytes()).hexdigest(),'config_sha256':hashlib.sha256(a.config.read_bytes()).hexdigest(),'elf_entry':hex(e.header.e_entry),'fw_arg_symbols':{n:hex(sym(n)['st_value']) for n in ['fw_arg0','fw_arg1','fw_arg2','fw_arg3']},'disassembly':result,'scope':'Prom loads limited to stack; direct constant-address loads of fw_arg symbols screened in prom_init, prom_soc_init and plat_mem_setup. This is not whole-program proof or a hardware execution test.'},indent=2))
