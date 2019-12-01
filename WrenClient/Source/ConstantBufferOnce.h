#pragma once

struct ConstantBufferPerFrame
{
	XMFLOAT4A directionalLightPos;
	XMFLOAT3A directionalLightColor;
	float pad;
	float ambientIntensity;
};