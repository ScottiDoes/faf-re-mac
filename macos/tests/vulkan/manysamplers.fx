// vk_effect_test input: the sampler it samples is declared 21st, so its register is s20
// (descriptor binding 84): past MoltenVK's 16 samplers per stage in an ordinary set.
float4x4 CompositeMatrix;
texture Texture1;
sampler Unused00 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused01 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused02 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused03 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused04 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused05 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused06 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused07 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused08 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused09 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused10 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused11 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused12 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused13 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused14 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused15 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused16 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused17 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused18 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler Unused19 = sampler_state { Texture = (Texture1); AddressU = CLAMP; AddressV = CLAMP; };
sampler WrapSampler = sampler_state
{
    Texture = (Texture1);
    MipFilter = POINT; MinFilter = POINT; MagFilter = POINT;
    AddressU = WRAP; AddressV = WRAP;
};

struct VS_OUTPUT
{
    float4 Pos : POSITION;
    float4 Color : COLOR0;
    float2 Tex1 : TEXCOORD0;
};

VS_OUTPUT ManyVS(float3 Pos : POSITION, float4 Color : COLOR0, float2 Tex : TEXCOORD0)
{
    VS_OUTPUT Out;
    Out.Pos = mul(float4(Pos, 1), CompositeMatrix);
    Out.Color = Color.bgra;
    Out.Tex1 = Tex;
    return Out;
}

float4 WrapPS(float4 Pos : POSITION, float4 Diff : COLOR0, float2 Tex1 : TEXCOORD0) : COLOR
{
    return tex2D(WrapSampler, Tex1) * Diff;
}

float4 GreenPS(float4 Pos : POSITION, float4 Diff : COLOR0, float2 Tex1 : TEXCOORD0) : COLOR
{
    return float4(0, 1, 0, 1);
}

technique TWrapSampler20
{
    pass P0
    {
        VertexShader = compile vs_1_1 ManyVS();
        PixelShader = compile ps_2_0 WrapPS();
    }
}

technique TGreen
{
    pass P0
    {
        VertexShader = compile vs_1_1 ManyVS();
        PixelShader = compile ps_2_0 GreenPS();
    }
}
