#ifndef __SSAOUTILS_HLSLI__
#define __SSAOUTILS_HLSLI__

uint EncodeNormal(float3 normal)
{
    normal = normal * 0.5 + 0.5;
    return  (uint(normal.x * 1023) & 0x3FF) << 22 |
            (uint(normal.y * 1023) & 0x3FF) << 12 |
            (uint(normal.z * 1023) & 0x3FF) << 2;
}

float3 DecodeNormal(uint normal)
{
    const float muitpler1023 = 1.0 / 1023.0;

    float3 d0;
    d0.x = float((normal >> 22) & 0x3FF) * muitpler1023;
    d0.y = float((normal >> 12) & 0x3FF) * muitpler1023;
    d0.z = float((normal >> 2) & 0x3FF) * muitpler1023;

    return normalize(2.0 * d0 - 1.0);
}

#endif // __SSAOUTILS_HLSLI__
