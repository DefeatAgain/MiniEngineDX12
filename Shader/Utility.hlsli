#pragma once

// ========================= color space utility =========================-

// Gamma ramps and encoding transfer functions
//
// Orthogonal to color space though usually tightly coupled.  For instance, sRGB is both a
// color space (defined by three basis vectors and a white point) and a gamma ramp.  Gamma
// ramps are designed to reduce perceptual error when quantizing floats to integers with a
// limited number of bits.  More variation is needed in darker colors because our eyes are
// more sensitive in the dark.  The way the curve helps is that it spreads out dark values
// across more code words allowing for more variation.  Likewise, bright values are merged
// together into fewer code words allowing for less variation.
//
// The sRGB curve is not a true gamma ramp but rather a piecewise function comprising a linear
// section and a power function.  When sRGB-encoded colors are passed to an LCD monitor, they
// look correct on screen because the monitor expects the colors to be encoded with sRGB, and it
// removes the sRGB curve to linearize the values.  When textures are encoded with sRGB--as many
// are--the sRGB curve needs to be removed before involving the colors in linear mathematics such
// as physically based lighting.

float3 ApplySRGBCurve( float3 x )
{
    // Approximately pow(x, 1.0 / 2.2)
    return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float3 RemoveSRGBCurve( float3 x )
{
    // Approximately pow(x, 2.2)
    return x < 0.04045 ? x / 12.92 : pow( (x + 0.055) / 1.055, 2.4 );
}

// The OETF recommended for content shown on HDTVs.  This "gamma ramp" may increase contrast as
// appropriate for viewing in a dark environment.  Always use this curve with Limited RGB as it is
// used in conjunction with HDTVs.
float3 ApplyREC709Curve( float3 x )
{
    return x < 0.0181 ? 4.5 * x : 1.0993 * pow(x, 0.45) - 0.0993;
}

float3 RemoveREC709Curve( float3 x )
{
    return x < 0.08145 ? x / 4.5 : pow((x + 0.0993) / 1.0993, 1.0 / 0.45);
}

// This is the new HDR transfer function, also called "PQ" for perceptual quantizer.  Note that REC2084
// does not also refer to a color space.  REC2084 is typically used with the REC2020 color space.
float3 ApplyREC2084Curve(float3 L)
{
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 Lp = pow(L, m1);
    return pow((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float3 RemoveREC2084Curve(float3 N)
{
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 Np = pow(N, 1 / m2);
    return pow(max(Np - c1, 0) / (c2 - c3 * Np), 1 / m1);
}

// Color space conversions
//
// These assume linear (not gamma-encoded) values.  A color space conversion is a change
// of basis (like in Linear Algebra).  Since a color space is defined by three vectors--
// the basis vectors--changing space involves a matrix-vector multiplication.  Note that
// changing the color space may result in colors that are "out of bounds" because some
// color spaces have larger gamuts than others.  When converting some colors from a wide
// gamut to small gamut, negative values may result, which are inexpressible in that new
// color space.
//
// It would be ideal to build a color pipeline which never throws away inexpressible (but
// perceivable) colors.  This means using a color space that is as wide as possible.  The
// XYZ color space is the neutral, all-encompassing color space, but it has the unfortunate
// property of having negative values (specifically in X and Z).  To correct this, a further
// transformation can be made to X and Z to make them always positive.  They can have their
// precision needs reduced by dividing by Y, allowing X and Z to be packed into two UNORM8s.
// This color space is called YUV for lack of a better name.
//

// Note:  Rec.709 and sRGB share the same color primaries and white point.  Their only difference
// is the transfer curve used.

float3 REC709toREC2020( float3 RGB709 )
{
    static const float3x3 ConvMat =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(ConvMat, RGB709);
}

float3 REC2020toREC709(float3 RGB2020)
{
    static const float3x3 ConvMat =
    {
        1.660496, -0.587656, -0.072840,
        -0.124547, 1.132895, -0.008348,
        -0.018154, -0.100597, 1.118751
    };
    return mul(ConvMat, RGB2020);
}

float3 REC709toDCIP3( float3 RGB709 )
{
    static const float3x3 ConvMat =
    {
        0.822458, 0.177542, 0.000000,
        0.033193, 0.966807, 0.000000,
        0.017085, 0.072410, 0.910505
    };
    return mul(ConvMat, RGB709);
}

float3 DCIP3toREC709( float3 RGBP3 )
{
    static const float3x3 ConvMat =
    {
        1.224947, -0.224947, 0.000000,
        -0.042056, 1.042056, 0.000000,
        -0.019641, -0.078651, 1.098291
    };
    return mul(ConvMat, RGBP3);
}
// Encodes a smooth logarithmic gradient for even distribution of precision natural to vision
float LinearToLogLuminance( float x, float gamma = 4.0 )
{
    return log2(lerp(1, exp2(gamma), x)) / gamma;
}

// This assumes the default color gamut found in sRGB and REC709.  The color primaries determine these
// coefficients.  Note that this operates on linear values, not gamma space.
float RGBToLuminance( float3 x )
{
    return dot( x, float3(0.212671, 0.715160, 0.072169) );        // Defined by sRGB/Rec.709 gamut
}

float MaxChannel(float3 x)
{
    return max(x.x, max(x.y, x.z));
}

// This is the same as above, but converts the linear luminance value to a more subjective "perceived luminance",
// which could be called the Log-Luminance.
float RGBToLogLuminance( float3 x, float gamma = 4.0 )
{
    return LinearToLogLuminance( RGBToLuminance(x), gamma );
}

// A fast invertible tone map that preserves color (Reinhard)
float3 TM( float3 rgb )
{
    return rgb / (1 + RGBToLuminance(rgb));
}

// Inverse of preceding function
float3 ITM( float3 rgb )
{
    return rgb / (1 - RGBToLuminance(rgb));
}

// 8-bit should range from 16 to 235
float3 RGBFullToLimited8bit( float3 x )
{
    return saturate(x) * 219.0 / 255.0 + 16.0 / 255.0;
}

float3 RGBLimitedToFull8bit( float3 x )
{
    return saturate((x - 16.0 / 255.0) * 255.0 / 219.0);
}

// 10-bit should range from 64 to 940
float3 RGBFullToLimited10bit( float3 x )
{
    return saturate(x) * 876.0 / 1023.0 + 64.0 / 1023.0;
}

float3 RGBLimitedToFull10bit( float3 x )
{
    return saturate((x - 64.0 / 1023.0) * 1023.0 / 876.0);
}

#define COLOR_FORMAT_LINEAR            0
#define COLOR_FORMAT_sRGB_FULL        1
#define COLOR_FORMAT_sRGB_LIMITED    2
#define COLOR_FORMAT_Rec709_FULL    3
#define COLOR_FORMAT_Rec709_LIMITED    4
#define COLOR_FORMAT_HDR10            5
#define COLOR_FORMAT_TV_DEFAULT        COLOR_FORMAT_Rec709_LIMITED
#define COLOR_FORMAT_PC_DEFAULT        COLOR_FORMAT_sRGB_FULL

#define HDR_COLOR_FORMAT            COLOR_FORMAT_LINEAR
#define LDR_COLOR_FORMAT            COLOR_FORMAT_LINEAR
#define DISPLAY_PLANE_FORMAT        COLOR_FORMAT_PC_DEFAULT

float3 ApplyDisplayProfile( float3 x, int DisplayFormat )
{
    switch (DisplayFormat)
    {
    default:
    case COLOR_FORMAT_LINEAR:
        return x;
    case COLOR_FORMAT_sRGB_FULL:
        return ApplySRGBCurve(x);
    case COLOR_FORMAT_sRGB_LIMITED:
        return RGBFullToLimited10bit(ApplySRGBCurve(x));
    case COLOR_FORMAT_Rec709_FULL:
        return ApplyREC709Curve(x);
    case COLOR_FORMAT_Rec709_LIMITED:
        return RGBFullToLimited10bit(ApplyREC709Curve(x));
    case COLOR_FORMAT_HDR10:
        return ApplyREC2084Curve(REC709toREC2020(x));
    };
}

float3 RemoveDisplayProfile( float3 x, int DisplayFormat )
{
    switch (DisplayFormat)
    {
    default:
    case COLOR_FORMAT_LINEAR:
        return x;
    case COLOR_FORMAT_sRGB_FULL:
        return RemoveSRGBCurve(x);
    case COLOR_FORMAT_sRGB_LIMITED:
        return RemoveSRGBCurve(RGBLimitedToFull10bit(x));
    case COLOR_FORMAT_Rec709_FULL:
        return RemoveREC709Curve(x);
    case COLOR_FORMAT_Rec709_LIMITED:
        return RemoveREC709Curve(RGBLimitedToFull10bit(x));
    case COLOR_FORMAT_HDR10:
        return REC2020toREC709(RemoveREC2084Curve(x));
    };
}

float3 ConvertColor( float3 x, int FromFormat, int ToFormat )
{
    if (FromFormat == ToFormat)
        return x;

    return ApplyDisplayProfile(RemoveDisplayProfile(x, FromFormat), ToFormat);
}
// ================================ color space utility =====================================-



// ================================ ToneMapping utility =========================================-
//
// Reinhard
// 

// The Reinhard tone operator.  Typically, the value of k is 1.0, but you can adjust exposure by 1/k.
// I.e. TM_Reinhard(x, 0.5) == TM_Reinhard(x * 2.0, 1.0)
float3 TM_Reinhard(float3 hdr, float k = 1.0)
{
    return hdr / (hdr + k);
}

// The inverse of Reinhard
float3 ITM_Reinhard(float3 sdr, float k = 1.0)
{
    return k * sdr / (k - sdr);
}

//
// Reinhard-Squared
//

// This has some nice properties that improve on basic Reinhard.  Firstly, it has a "toe"--that nice,
// parabolic upswing that enhances contrast and color saturation in darks.  Secondly, it has a long
// shoulder giving greater detail in highlights and taking longer to desaturate.  It's invertible, scales
// to HDR displays, and is easy to control.
//
// The default constant of 0.25 was chosen for two reasons.  It maps closely to the effect of Reinhard
// with a constant of 1.0.  And with a constant of 0.25, there is an inflection point at 0.25 where the
// curve touches the line y=x and then begins the shoulder.
//
// Note:  If you are currently using ACES and you pre-scale by 0.6, then k=0.30 looks nice as an alternative
// without any other adjustments.

float3 TM_ReinhardSq(float3 hdr, float k = 0.25)
{
    float3 reinhard = hdr / (hdr + k);
    return reinhard * reinhard;
}

float3 ITM_ReinhardSq(float3 sdr, float k = 0.25)
{
    return k * (sdr + sqrt(sdr)) / (1.0 - sdr);
}

//
// Stanard (New)
//

// This is the new tone operator.  It resembles ACES in many ways, but it is simpler to evaluate with ALU.  One
// advantage it has over Reinhard-Squared is that the shoulder goes to white more quickly and gives more overall
// brightness and contrast to the image.

float3 TM_Stanard(float3 hdr)
{
    return TM_Reinhard(hdr * sqrt(hdr), sqrt(4.0 / 27.0));
}

float3 ITM_Stanard(float3 sdr)
{
    return pow(ITM_Reinhard(sdr, sqrt(4.0 / 27.0)), 2.0 / 3.0);
}

//
// Stanard (Old)
//

// This is the old tone operator first used in HemiEngine and then MiniEngine.  It's simplistic, efficient,
// invertible, and gives nice results, but it has no toe, and the shoulder goes to white fairly quickly.
//
// Note that I removed the distinction between tone mapping RGB and tone mapping Luma.  Philosophically, I
// agree with the idea of trying to remap brightness to displayable values while preserving hue.  But you
// run into problems where one or more color channels end up brighter than 1.0 and get clipped.

float3 ToneMap( float3 hdr )
{
    return 1 - exp2(-hdr);
}

float3 InverseToneMap(float3 sdr)
{
    return -log2(max(1e-6, 1 - sdr));
}

float ToneMapLuma( float luma )
{
    return 1 - exp2(-luma);
}

float InverseToneMapLuma(float luma)
{
    return -log2(max(1e-6, 1 - luma));
}

//
// ACES
//

// The next generation of filmic tone operators.

float3 ToneMapACES( float3 hdr )
{
    const float A = 2.51, B = 0.03, C = 2.43, D = 0.59, E = 0.14;
    return saturate((hdr * (A * hdr + B)) / (hdr * (C * hdr + D) + E));
}

float3 InverseToneMapACES( float3 sdr )
{
    const float A = 2.51, B = 0.03, C = 2.43, D = 0.59, E = 0.14;
    return 0.5 * (D * sdr - sqrt(((D*D - 4*C*E) * sdr + 4*A*E-2*B*D) * sdr + B*B) - B) / (A - C * sdr);
}
// ================================ ToneMapping utility =========================================-




// ================================ pixel packing utility =======================================-
// The standard 32-bit HDR color format.  Each float has a 5-bit exponent and no sign bit.
uint Pack_R11G11B10_FLOAT( float3 rgb )
{
    // Clamp upper bound so that it doesn't accidentally round up to INF 
    // Exponent=15, Mantissa=1.11111
    rgb = min(rgb, asfloat(0x477C0000));   // 0 10001110 1111100 0000 0000 0000 0000
    // rgb = min(rgb, asfloat(0xF7C0000));       // 0 00011110 1111100 0000 0000 0000 0000
    uint r = ((f32tof16(rgb.x) + 8) >> 4) & 0x000007FF; // f16的指数也是5-bit
    uint g = ((f32tof16(rgb.y) + 8) << 7) & 0x003FF800;
    uint b = ((f32tof16(rgb.z) + 16) << 17) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT( uint rgb )
{
    float r = f16tof32((rgb << 4 ) & 0x7FF0);
    float g = f16tof32((rgb >> 7 ) & 0x7FF0);
    float b = f16tof32((rgb >> 17) & 0x7FE0);
    return float3(r, g, b);
}

// An improvement to float is to store the mantissa in logarithmic form.  This causes a
// smooth and continuous change in precision rather than having jumps in precision every
// time the exponent increases by whole amounts.
uint Pack_R11G11B10_FLOAT_LOG( float3 rgb )
{
    float3 flat_mantissa = asfloat((asuint(rgb) & 0x7FFFFF) | 0x3F800000);
    float3 curved_mantissa = min(log2(flat_mantissa) + 1.0, asfloat(0x3FFFFFFF));
    rgb = asfloat((asuint(rgb) & 0xFF800000) | (asuint(curved_mantissa) & 0x7FFFFF));

    uint r = ((f32tof16(rgb.x) + 8) >>  4) & 0x000007FF;
    uint g = ((f32tof16(rgb.y) + 8) <<  7) & 0x003FF800;
    uint b = ((f32tof16(rgb.z) + 16) << 17) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT_LOG( uint p )
{
    float3 rgb = f16tof32(uint3(p << 4, p >> 7, p >> 17) & uint3(0x7FF0, 0x7FF0, 0x7FE0));
    float3 curved_mantissa = asfloat((asuint(rgb) & 0x7FFFFF) | 0x3F800000);
    float3 flat_mantissa = exp2(curved_mantissa - 1.0);
    return asfloat((asuint(rgb) & 0xFF800000) | (asuint(flat_mantissa) & 0x7FFFFF));
}

// As an alternative to floating point, we can store the log2 of a value in fixed point notation.
// The 11-bit fields store 5.6 fixed point notation for log2(x) with an exponent bias of 15.  The
// 10-bit field uses 5.5 fixed point.  The disadvantage here is we don't handle underflow.  Instead
// we use the extra two exponent values to extend the range down through two more exponents.
// Range = [2^-16, 2^16)
uint Pack_R11G11B10_FIXED_LOG(float3 rgb)
{
    uint3 p = clamp((log2(rgb) + 16.0) * float3(64, 64, 32) + 0.5, 0.0, float3(2047, 2047, 1023));
    return p.b << 22 | p.g << 11 | p.r;
}

float3 Unpack_R11G11B10_FIXED_LOG(uint p)
{
    return exp2((uint3(p, p >> 11, p >> 21) & uint3(2047, 2047, 2046)) / 64.0 - 16.0);
}

// These next two encodings are great for LDR data.  By knowing that our values are [0.0, 1.0]
// (or [0.0, 2.0), incidentally), we can reduce how many bits we need in the exponent.  We can
// immediately eliminate all postive exponents.  By giving more bits to the mantissa, we can
// improve precision at the expense of range.  The 8E3 format goes one bit further, quadrupling
// mantissa precision but increasing smallest exponent from -14 to -6.  The smallest value of 8E3
// is 2^-14, while the smallest value of 7E4 is 2^-21.  Both are smaller than the smallest 8-bit
// sRGB value, which is close to 2^-12.

// This is like R11G11B10_FLOAT except that it moves one bit from each exponent to each mantissa.
uint Pack_R11G11B10_E4_FLOAT( float3 rgb )
{
    // Clamp to [0.0, 2.0).  The magic number is 1.FFFFF x 2^0.  (We can't represent hex floats in HLSL.)
    // This trick works because clamping your exponent to 0 reduces the number of bits needed by 1.
    rgb = clamp( rgb, 0.0, asfloat(0x3FFFFFFF) );
    uint r = ((f32tof16(rgb.r) + 4) >> 3 ) & 0x000007FF;
    uint g = ((f32tof16(rgb.g) + 4) << 8 ) & 0x003FF800;
    uint b = ((f32tof16(rgb.b) + 8) << 18) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_E4_FLOAT( uint rgb )
{
    float r = f16tof32((rgb << 3 ) & 0x3FF8);
    float g = f16tof32((rgb >> 8 ) & 0x3FF8);
    float b = f16tof32((rgb >> 18) & 0x3FF0);
    return float3(r, g, b);
}

// This is like R11G11B10_FLOAT except that it moves two bits from each exponent to each mantissa.
uint Pack_R11G11B10_E3_FLOAT( float3 rgb )
{
    // Clamp to [0.0, 2.0).  Divide by 256 to bias the exponent by -8.  This shifts it down to use one
    // fewer bit while still taking advantage of the denormalization hardware.  In half precision,
    // the exponent of 0 is 0xF.  Dividing by 256 makes the max exponent 0x7--one fewer bit.
    rgb = clamp( rgb, 0.0, asfloat(0x3FFFFFFF) ) / 256.0;
    uint r = ((f32tof16(rgb.r) + 2) >> 2 ) & 0x000007FF;
    uint g = ((f32tof16(rgb.g) + 2) << 9 ) & 0x003FF800;
    uint b = ((f32tof16(rgb.b) + 4) << 19) & 0xFFC00000;
    return r | g | b;
}

float3 Unpack_R11G11B10_E3_FLOAT( uint rgb )
{
    float r = f16tof32((rgb << 2 ) & 0x1FFC);
    float g = f16tof32((rgb >> 9 ) & 0x1FFC);
    float b = f16tof32((rgb >> 19) & 0x1FF8);
    return float3(r, g, b) * 256.0;
}

// RGBE, aka R9G9B9E5_SHAREDEXP, is an unsigned float HDR pixel format where red, green,
// and blue all share the same exponent.  The color channels store a 9-bit value ranging
// from [0/512, 511/512] which multiplies by 2^Exp and Exp ranges from [-15, 16].
// Floating point specials are not encoded.
uint PackRGBE(float3 rgb)
{
    // To determine the shared exponent, we must clamp the channels to an expressible range
    const float kMaxVal = asfloat(0x477F8000); // 1.FF x 2^+15
    const float kMinVal = asfloat(0x37800000); // 1.00 x 2^-16

    // Non-negative and <= kMaxVal
    rgb = clamp(rgb, 0, kMaxVal);

    // From the maximum channel we will determine the exponent.  We clamp to a min value
    // so that the exponent is within the valid 5-bit range.
    float MaxChannel = max(max(kMinVal, rgb.r), max(rgb.g, rgb.b));

    // 'Bias' has to have the biggest exponent plus 15 (and nothing in the mantissa).  When
    // added to the three channels, it shifts the explicit '1' and the 8 most significant
    // mantissa bits into the low 9 bits.  IEEE rules of float addition will round rather
    // than truncate the discarded bits.  Channels with smaller natural exponents will be
    // shifted further to the right (discarding more bits).
    float Bias = asfloat((asuint(MaxChannel) + 0x07804000) & 0x7F800000);

    // Shift bits into the right places
    uint3 RGB = asuint(rgb + Bias);
    uint E = (asuint(Bias) << 4) + 0x10000000;
    return E | RGB.b << 18 | RGB.g << 9 | (RGB.r & 0x1FF);
}

float3 UnpackRGBE(uint p)
{
    float3 rgb = uint3(p, p >> 9, p >> 18) & 0x1FF;
    return ldexp(rgb, (int)(p >> 27) - 24);
}

// This non-standard variant applies a non-linear ramp to the mantissa to get better precision
// with bright and saturated colors.  These colors tend to have one or two channels that prop
// up the shared exponent, leaving little to no information in the dark channels.
uint PackRGBE_sqrt(float3 rgb)
{
    // To determine the shared exponent, we must clamp the channels to an expressible range
    const float kMaxVal = asfloat(0x477FFFFF); // 1.FFFFFF x 2^+15
    const float kMinVal = asfloat(0x37800000); // 1.000000 x 2^-16

    rgb = clamp(rgb, 0, kMaxVal);

    float MaxChannel = max(max(kMinVal, rgb.r), max(rgb.g, rgb.b));

    // Scaling the maximum channel puts it into the range [0, 1).  It does this by negating
    // and subtracting one from the max exponent.
    float Scale = asfloat((0x7EFFFFFF - asuint(MaxChannel)) & 0x7F800000);
    uint3 RGB = sqrt(rgb * Scale) * 511.0 + 0.5;
    uint E = (0x47000000 - asuint(Scale)) << 4;
    return E | RGB.b << 18 | RGB.g << 9 | RGB.r;
}

float3 UnpackRGBE_sqrt(uint p)
{
    float3 rgb = (uint3(p, p >> 9, p >> 18) & 0x1FF) / 511.0;
    return ldexp(rgb * rgb, (int)(p >> 27) - 15);
}


float4 ToRGBM( float3 rgb, float PeakValue = 255.0 / 16.0 )
{
    rgb = saturate(rgb / PeakValue);
    float maxVal = max(max(1e-6, rgb.x), max(rgb.y, rgb.z));
    maxVal = ceil(maxVal * 255.0) / 255.0;
    return float4(rgb / maxVal, maxVal);
}

float3 FromRGBM(float4 rgbm, float PeakValue = 255.0 / 16.0 )
{
    return rgbm.rgb * rgbm.a * PeakValue;
}

// RGBM is a good way to pack HDR values into R8G8B8A8_UNORM
uint PackRGBM( float4 rgbm, bool sRGB = true )
{
    if (sRGB)
        rgbm.rgb = ApplySRGBCurve(rgbm.rgb);
    rgbm = rgbm * 255.0 + 0.5;
    return (uint)rgbm.a << 24 | (uint)rgbm.b << 16 | (uint)rgbm.g << 8 | (uint)rgbm.r;
}

float4 UnpackRGBM( uint p, bool sRGB = true )
{
    float4 rgbm = float4(uint4(p, p >> 8, p >> 16, p >> 24) & 0xFF);
    rgbm /= 255.0;
    if (sRGB)
        rgbm.rgb = RemoveSRGBCurve(rgbm.rgb);
    return rgbm;
}
// ========================= pixel packing utility =========================-
