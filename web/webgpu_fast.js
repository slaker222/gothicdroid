// Hot-path replacements for emscripten's library_webgpu.js glue.
//
// setBindGroup with dynamic offsets is called for nearly every draw. The stock
// glue builds a fresh JS array of offsets on every call, which the browser then
// converts as a sequence; the (Uint32Array, start, length) overload reads the
// offsets straight from wasm memory without allocating.
addToLibrary({
  wgpuRenderPassEncoderSetBindGroup__deps: ['$WebGPU'],
  wgpuRenderPassEncoderSetBindGroup: (passId, groupIndex, groupId, dynamicOffsetCount, dynamicOffsetsPtr) => {
    var pass  = WebGPU.mgrRenderPassEncoder.get(passId);
    var group = WebGPU.mgrBindGroup.get(groupId);
    if (dynamicOffsetCount == 0)
      pass.setBindGroup(groupIndex, group);
    else
      pass.setBindGroup(groupIndex, group, HEAPU32, dynamicOffsetsPtr >> 2, dynamicOffsetCount);
  },

  wgpuComputePassEncoderSetBindGroup__deps: ['$WebGPU'],
  wgpuComputePassEncoderSetBindGroup: (passId, groupIndex, groupId, dynamicOffsetCount, dynamicOffsetsPtr) => {
    var pass  = WebGPU.mgrComputePassEncoder.get(passId);
    var group = WebGPU.mgrBindGroup.get(groupId);
    if (dynamicOffsetCount == 0)
      pass.setBindGroup(groupIndex, group);
    else
      pass.setBindGroup(groupIndex, group, HEAPU32, dynamicOffsetsPtr >> 2, dynamicOffsetCount);
  },
});
