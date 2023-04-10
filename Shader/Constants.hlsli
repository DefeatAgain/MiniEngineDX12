#ifndef __CONSTANTS_HLSLI__
#define __CONSTANTS_HLSLI__

static const float PI = 3.141592653589793;
static const float PI2 = 6.283185307179586;
static const float PI_DIV = 0.31830988618;
static const float3 kDielectricSpecular = float3(0.04, 0.04, 0.04);

static const float2 PoissonDisk16[] = {
	float2(-0.729046, 0.629447),
	float2(-0.0414042, 0.213248),
	float2(-0.292185, 0.936644),
	float2(-0.500975, -0.0682974),
	float2(-0.873973, 0.118084),
	float2(0.0789126, 0.65298),
	float2(-0.327559, 0.519299),
	float2(-0.486096, -0.506308),
	float2(0.476021, 0.544698),
	float2(0.0295523, -0.19292),
	float2(0.391344, 0.142684),
	float2(0.930173, 0.175806),
	float2(0.542538, -0.25083),
	float2(-0.181811, -0.823552),
	float2(0.156925, -0.582289),
	float2(0.514757, -0.835997),
};

static const float2 PoissonDisk32[] = {
    float2(-0.729046, 0.629447),
	float2(-0.24281, 0.33515),
	float2(-0.420139, 0.846668),
	float2(-0.567775, 0.136067),
	float2(-0.939189, 0.191931),
	float2(-0.157733, 0.646087),
	float2(-0.445152, 0.55156),
	float2(-0.862086, -0.153209),
	float2(-0.563915, -0.166839),
	float2(0.123065, 0.569521),
	float2(0.00406384, 0.896643),
	float2(-0.192636, 0.0479462),
	float2(0.0631894, 0.285253),
	float2(0.444199, 0.308674),
	float2(0.1701, 0.00699735),
	float2(0.324172, 0.877298),
	float2(0.686057, 0.485656),
	float2(0.00112057, -0.252855),
	float2(-0.485327, -0.611566),
	float2(-0.821766, -0.537822),
	float2(-0.310392, -0.379864),
	float2(0.645259, 0.0335051),
	float2(0.455278, -0.244452),
	float2(0.986669, 0.124819),
	float2(-0.352476, -0.874031),
	float2(0.0443888, -0.695745),
	float2(0.226146, -0.460793),
	float2(0.310417, -0.917178),
	float2(0.565125, -0.516178),
	float2(-0.0663162, -0.981374),
	float2(0.586453, -0.808731),
	float2(0.758625, -0.253756),
};

#endif // __CONSTANTS_HLSLI__
