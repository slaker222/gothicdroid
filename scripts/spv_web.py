#!/usr/bin/env python3
"""Prepare an engine SPIR-V shader for the WebGPU backend.

WGSL has no push constants and no combined image-samplers, so before handing
the module to naga:
  * push_constant blocks become uniform blocks at (set 0, binding PUSH_BINDING)
  * every combined sampler variable at binding N is split into an image
    variable (binding N) and a sampler variable (binding N + SAMPLER_SHIFT)

Output blob layout (consumed by WebGapi::createShader): see write_blob()
"""
import struct
import subprocess
import sys
import os
import tempfile
import re

STRICT = int(os.environ.get('SPV_WEB_STRICT', '0'))
PUSH_BINDING = 64
SAMPLER_SHIFT = 32

OpName, OpEntryPoint, OpDecorate = 5, 15, 71
OpTypeImage, OpTypeSampler, OpTypeSampledImage, OpTypePointer = 25, 26, 27, 32
OpVariable, OpLoad, OpSampledImage = 59, 61, 86
OpFunction = 54
OpMemberDecorate = 72
TYPE_OPS_MIN, TYPE_OPS_MAX = 19, 39   # OpTypeVoid .. OpTypeForwardPointer
CONST_OPS = {41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 1}  # constants, spec constants, OpUndef
Dec_NonReadable = 25
OpConstant, OpTypeArray = 43, 28
OpTypeFloat = 22
OpTypeStruct, OpTypeRuntimeArray, OpMemberName = 30, 29, 6
OpAccessChain, OpInBoundsAccessChain = 65, 66
SC_StorageBuffer = 12
OP_ATOMIC_MIN, OP_ATOMIC_MAX = 227, 242
# opcode -> operand indexes (in our [op, ...] lists) that hold a pointer id
POINTER_OPERANDS = {61: (3,), 62: (1,), 63: (1, 2), 65: (3,), 66: (3,), 68: (3,), 60: (3,), 228: (1,)}

SC_UniformConstant, SC_Uniform, SC_PushConstant = 0, 2, 9
Dec_Binding, Dec_DescriptorSet = 33, 34


def parse(words):
    insts = []
    i = 5
    while i < len(words):
        wc = words[i] >> 16
        op = words[i] & 0xFFFF
        if wc == 0:
            raise ValueError("bad spirv")
        insts.append([op] + list(words[i + 1:i + wc]))
        i += wc
    return words[:5], insts


def emit(header, insts):
    out = list(header)
    for ins in insts:
        out.append((len(ins) << 16) | ins[0])
        out.extend(ins[1:])
    return out


def strip_dead_types(insts):
    removable = {OpTypeArray, OpTypePointer, OpTypeStruct, OpTypeRuntimeArray}
    meta = {OpName, OpMemberName, OpDecorate, OpMemberDecorate}
    while True:
        refs = set()
        for ins in insts:
            if ins[0] in meta:
                continue
            ops = ins[1:]
            if ins[0] in removable:
                ops = ops[1:]
            refs.update(ops)
        dead = {ins[1] for ins in insts if ins[0] in removable and ins[1] not in refs}
        if not dead:
            return insts
        insts = [ins for ins in insts
                 if not (ins[0] in removable and ins[1] in dead)
                 and not (ins[0] in meta and ins[1] in dead)]


def transform(words):
    header, insts = parse(words)
    version = header[1]
    bound = header[3]

    def new_id():
        nonlocal bound
        r = bound
        bound += 1
        return r

    binding = {}
    dset = {}
    names = {}
    for ins in insts:
        if ins[0] == OpDecorate and ins[2] == Dec_Binding:
            binding[ins[1]] = ins[3]
        if ins[0] == OpDecorate and ins[2] == Dec_DescriptorSet:
            dset[ins[1]] = ins[3]
        if ins[0] == OpName:
            names[ins[1]] = ins[2:]

    new_decor = []

    # --- WGSL has no NaN/Inf literals: NaN -> 0 (a NaN gl_Position is used to
    # cull; an all-zero position degenerates the primitive the same way)
    float_types = {ins[1] for ins in insts if ins[0] == OpTypeFloat and ins[2] == 32}
    for ins in insts:
        if ins[0] == OpConstant and len(ins) == 4 and ins[1] in float_types:
            exp = (ins[3] >> 23) & 0xFF
            if exp == 0xFF:
                if ins[3] & 0x7FFFFF:
                    ins[3] = 0
                else:
                    ins[3] = (ins[3] & 0x80000000) | 0x7F7FFFFF

    # --- push constants -> uniform buffer
    for ins in insts:
        if ins[0] == OpTypePointer and ins[2] == SC_PushConstant:
            ins[2] = SC_Uniform
        elif ins[0] == OpVariable and ins[3] == SC_PushConstant:
            ins[3] = SC_Uniform
            new_decor.append([OpDecorate, ins[2], Dec_DescriptorSet, 0])
            new_decor.append([OpDecorate, ins[2], Dec_Binding, PUSH_BINDING])

    sampled_img = {}   # sampledImageType -> imageType
    ptr_to = {}        # pointerId -> (sc, type)
    sampler_type = None
    consts = {}
    arrays = {}        # arrayType -> (elemType, lengthConstId)
    for ins in insts:
        if ins[0] == OpTypeSampledImage:
            sampled_img[ins[1]] = ins[2]
        elif ins[0] == OpTypePointer:
            ptr_to[ins[1]] = (ins[2], ins[3])
        elif ins[0] == OpTypeSampler:
            sampler_type = ins[1]
        elif ins[0] == OpConstant and len(ins) == 4:
            consts[ins[2]] = ins[3]
        elif ins[0] == OpTypeArray:
            arrays[ins[1]] = (ins[2], ins[3])

    extra_types = []
    ptr_cache = {}

    def get_ptr(target, sc_want=SC_UniformConstant):
        for pid, (sc, t) in ptr_to.items():
            if sc == sc_want and t == target:
                return pid
        if (sc_want, target) in ptr_cache:
            return ptr_cache[(sc_want, target)]
        pid = new_id()
        ptr_cache[(sc_want, target)] = pid
        extra_types.append([OpTypePointer, pid, sc_want, target])
        return pid

    # --- resource arrays of length 1 (unsized arrays indexed only by 0 in the
    # non-bindless shader variants) -> single resources; WGSL has no arrays of
    # buffers/textures
    dearr = set()
    for ins in insts:
        if ins[0] == OpVariable and ins[3] in (SC_UniformConstant, SC_Uniform, SC_StorageBuffer):
            sc, pointee = ptr_to.get(ins[1], (None, None))
            if pointee in arrays and consts.get(arrays[pointee][1]) == 1:
                ins[1] = get_ptr(arrays[pointee][0], sc)
                dearr.add(ins[2])
    alias = {}
    if dearr:
        kept = []
        for ins in insts:
            if ins[0] in (OpAccessChain, OpInBoundsAccessChain) and ins[3] in dearr:
                if len(ins) == 5:
                    alias[ins[2]] = ins[3]
                    continue
                ins = ins[:4] + ins[5:]
            kept.append(ins)
        insts = kept
        for ins in insts:
            for k in POINTER_OPERANDS.get(ins[0], ()):
                if k < len(ins) and ins[k] in alias:
                    ins[k] = alias[ins[k]]
            if OP_ATOMIC_MIN <= ins[0] <= OP_ATOMIC_MAX and len(ins) > 3 and ins[3] in alias:
                ins[3] = alias[ins[3]]
        for t in extra_types:
            ptr_to[t[1]] = (t[2], t[3])

    # --- split combined image samplers
    combined_vars = {}  # var -> (samplerVar, imageType, sampledType)
    buffer_vars = set()
    for ins in insts:
        if ins[0] == OpVariable and ins[3] in (SC_Uniform, SC_StorageBuffer):
            buffer_vars.add(ins[2])

    need_sampler_type = False
    if sampler_type is None:
        sampler_type = new_id()
        need_sampler_type = True

    var_insts = []
    for ins in insts:
        if ins[0] == OpVariable and ins[3] == SC_UniformConstant:
            sc, pointee = ptr_to.get(ins[1], (None, None))
            if pointee in sampled_img:
                var_insts.append(ins)

    added_vars = []
    if var_insts:
        smp_ptr = None
        for ins in var_insts:
            img_t = sampled_img[ptr_to[ins[1]][1]]
            img_ptr = get_ptr(img_t)
            if smp_ptr is None:
                smp_ptr = get_ptr(sampler_type)
            si_t = ptr_to[ins[1]][1]
            v = ins[2]
            ins[1] = img_ptr
            sv = new_id()
            combined_vars[v] = (sv, img_t, si_t)
            added_vars.append([OpVariable, smp_ptr, sv, SC_UniformConstant])
            new_decor.append([OpDecorate, sv, Dec_DescriptorSet, dset.get(v, 0)])
            new_decor.append([OpDecorate, sv, Dec_Binding, binding.get(v, 0) + SAMPLER_SHIFT])

    # rebuild instruction stream
    first_decor = None
    for idx, ins in enumerate(insts):
        if ins[0] in (OpDecorate, OpMemberDecorate):
            first_decor = idx
            break
    if first_decor is None and new_decor:
        raise ValueError("no annotation section")

    out = []
    for idx, ins in enumerate(insts):
        if idx == first_decor:
            out.extend(new_decor)
        # WGSL has no write-only storage buffers
        if ins[0] == OpDecorate and ins[2] == Dec_NonReadable and ins[1] in buffer_vars:
            continue
        if ins[0] == OpMemberDecorate and ins[3] == Dec_NonReadable:
            continue
        if ins[0] == OpLoad and ins[3] in combined_vars:
            sv, img_t, si_t = combined_vars[ins[3]]
            t_img = new_id()
            t_smp = new_id()
            out.append([OpLoad, img_t, t_img, ins[3]] + ins[4:])
            out.append([OpLoad, sampler_type, t_smp, sv])
            out.append([OpSampledImage, ins[1], ins[2], t_img, t_smp])
            continue
        if ins[0] == OpEntryPoint and version >= 0x00010400:
            ins = ins + [x[2] for x in added_vars]
        out.append(ins)

    # global section: keep types/constants in order, then all variables, so a
    # variable whose pointer type changed never precedes that type
    g0 = next(i for i, ins in enumerate(out) if TYPE_OPS_MIN <= ins[0] <= TYPE_OPS_MAX or ins[0] in CONST_OPS or ins[0] == OpVariable)
    g1 = next(i for i, ins in enumerate(out) if ins[0] == OpFunction)
    glob = out[g0:g1]
    nonvars = [i for i in glob if i[0] != OpVariable]
    gvars = [i for i in glob if i[0] == OpVariable]
    block = []
    if need_sampler_type and var_insts:
        block.append([OpTypeSampler, sampler_type])
    block += extra_types
    out = out[:g0] + nonvars + block + gvars + added_vars + out[g1:]

    out = strip_dead_types(out)
    header = list(header)
    header[3] = bound
    return emit(header, out)


def fixup_wgsl(text):
    """Post-process naga output for WGSL's stricter uniformity rules.

    Returns (primary, alternative); the backend falls back to the alternative
    when the primary module fails to compile."""
    # GLSL allows implicit derivatives in divergent control flow
    text = 'diagnostic(off, derivative_uniformity);\n' + text
    # A value read from a workgroup atomic is non-uniform for the analysis even
    # right after a barrier; workgroupUniformLoad gives the same value with a
    # uniformity guarantee, but itself must sit in uniform control flow - which
    # only the browser compiler can tell, hence two variants.
    alt = text
    for name in re.findall(r'var<workgroup>\s+(\w+)\s*:\s*atomic<', text):
        alt = alt.replace('atomicLoad((&%s))' % name, 'workgroupUniformLoad((&%s))' % name)
    return text, (alt if alt != text else '')


def main():
    src, dst, naga = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(src, 'rb').read()
    words = list(struct.unpack('<%dI' % (len(data) // 4), data))
    try:
        patched = transform(words)
    except Exception as e:
        if STRICT:
            raise
        sys.stderr.write('%s: transform failed: %r\n' % (src, e))
        patched = None
    with tempfile.TemporaryDirectory() as td:
        if patched is None:
            write_blob(dst, data, '', '')
            return
        spv = os.path.join(td, 'in.spv')
        wgsl = os.path.join(td, 'out.wgsl')
        open(spv, 'wb').write(struct.pack('<%dI' % len(patched), *patched))
        r = subprocess.run([naga, spv, wgsl], capture_output=True, text=True)
        if r.returncode != 0:
            # an empty WGSL part makes the backend report the pipeline as
            # unsupported instead of failing the whole build
            sys.stderr.write('%s: naga failed\n%s%s' % (src, r.stdout, r.stderr))
            if STRICT:
                sys.exit(1)
            text, alt = '', ''
        else:
            text, alt = fixup_wgsl(open(wgsl, 'rb').read().decode('utf-8'))
    write_blob(dst, data, text, alt)


def write_blob(dst, data, text, alt):
    # 'WGSL' u32 n spirv[n] u32 n name[n] u32 n wgsl[n] alt-wgsl(rest)
    name = os.path.basename(dst).encode('utf-8')
    text = text.encode('utf-8')
    with open(dst, 'wb') as f:
        f.write(b'WGSL')
        f.write(struct.pack('<I', len(data)))
        f.write(data)
        f.write(struct.pack('<I', len(name)))
        f.write(name)
        f.write(struct.pack('<I', len(text)))
        f.write(text)
        f.write(alt.encode('utf-8'))


if __name__ == '__main__':
    main()
