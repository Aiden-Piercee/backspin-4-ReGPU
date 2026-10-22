import os
import subprocess
import sys

out_dir = sys.argv[1] if len(sys.argv) > 1 else 'generated'
os.makedirs(out_dir, exist_ok=True)
out_h = os.path.join(out_dir, 'fsr1_cs.h')

# Shader code without FFX includes for the built-in compute passes
hlsl_non_ffx = '''
cbuffer Constants : register(b0)
{
    uint4 con0, con1, con2, con3;
    uint4 rcasCon;
    uint4 outSize;
};
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);
SamplerState linearClamp : register(s0);

[numthreads(8, 8, 1)]
void DepthCopyMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= outSize.xy))
        return;
    const uint2 p = id.xy + con0.xy;
    dst[p] = src.Load(int3(p, 0)).rrrr;
}

float GammaCurve(float x, float p[17])
{
    const float f = saturate(x) * 16.0;
    const uint i = min(uint(f), 15u);
    return lerp(p[i], p[i + 1], f - float(i));
}

[numthreads(8, 8, 1)]
void GammaMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= outSize.xy))
        return;
    const uint4 q[5] = { con0, con1, con2, con3, rcasCon };
    float p[17];
    [unroll] for (uint k = 0; k < 17; k++)
        p[k] = asfloat(q[k / 4][k % 4]);
    const float4 c = src.Load(int3(id.xy, 0));
    dst[id.xy] = float4(GammaCurve(c.r, p), GammaCurve(c.g, p), GammaCurve(c.b, p), c.a);
}

[numthreads(8, 8, 1)]
void EnhanceMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= outSize.xy))
        return;
    const int2 p = int2(id.xy);
    const int2 last = int2(outSize.xy) - 1;
    const float strength = asfloat(rcasCon.x);
    const float4 c = src.Load(int3(p, 0));
    const float4 l = src.Load(int3(max(p.x - 1, 0), p.y, 0));
    const float4 r = src.Load(int3(min(p.x + 1, last.x), p.y, 0));
    const float4 u = src.Load(int3(p.x, max(p.y - 1, 0), 0));
    const float4 d = src.Load(int3(p.x, min(p.y + 1, last.y), 0));
    const float4 blur = (l + r + u + d) * 0.25;
    dst[id.xy] = float4(saturate(c.rgb + strength * (c.rgb - blur.rgb)), c.a);
}

[numthreads(8, 8, 1)]
void DummyMain(uint3 id : SV_DispatchThreadID)
{
}
'''

tmp_hlsl = os.path.join(out_dir, 'temp_cs.hlsl')
with open(tmp_hlsl, 'w') as f:
    f.write(hlsl_non_ffx)

entries = ['DepthCopyMain', 'GammaMain', 'EnhanceMain', 'DummyMain']
blobs = {}

for entry in entries:
    cso = os.path.join(out_dir, entry + '.cso')
    cmd = ['fxc', '/nologo', '/O3', '/T', 'cs_5_1', '/E', entry, '/Fo', cso, tmp_hlsl]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"fxc failed for {entry}: {r.stderr}")
        sys.exit(1)
    with open(cso, 'rb') as f:
        blobs[entry] = f.read()
    os.remove(cso)

os.remove(tmp_hlsl)

with open(out_h, 'w') as out:
    out.write('// Generated compute shader bytecode\n#pragma once\n#include <cstdint>\n\n')
    for name, key in [('kDepthCopyMain', 'DepthCopyMain'), ('kGammaMain', 'GammaMain'), ('kEnhanceMain', 'EnhanceMain'),
                      ('kEasuMain', 'DummyMain'), ('kRcasMain', 'DummyMain')]:
        data = blobs[key]
        out.write(f'alignas(8) const uint8_t {name}[{len(data)}] = {{\n')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            out.write('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')
        out.write('};\n\n')

print(f"Generated {out_h} successfully")
