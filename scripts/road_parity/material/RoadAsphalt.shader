Shader "RoadParity/AsphaltCommonV1" {
 Properties { roadColor("Raw road albedo",2D)="white"{} roadNormal("Raw road normal",2D)="bump"{} }
 SubShader { Tags { "RenderType"="Opaque" } Pass { Cull Back ZWrite On
 CGPROGRAM
 #pragma target 3.0
 #pragma vertex vert
 #pragma fragment frag
 #include "UnityCG.cginc"
 sampler2D roadColor,roadNormal;
 #define ROAD_SAMPLE(t,u) tex2Dlod(t,float4((u).x,1.0-(u).y,0.0,0.0))
 #include "RoadMaterialParams.inc"
#include "RoadAsphaltCommon.inc"
 struct Input { float4 vertex:POSITION; float3 normal:NORMAL; };
 struct Output { float4 position:SV_POSITION;float3 world:TEXCOORD0;float3 normal:TEXCOORD1; };
 Output vert(Input i) { Output o;o.position=UnityObjectToClipPos(i.vertex);o.world=mul(unity_ObjectToWorld,i.vertex).xyz*float3(1,1,-1);o.normal=UnityObjectToWorldNormal(i.normal)*float3(1,1,-1);return o; }
 float encode(float x) { return x <= 0.0031308 ? x*12.92 : 1.055*pow(max(x,0.0),1.0/2.4)-0.055; }
 float4 frag(Output i):SV_Target { float3 c=roadShade(i.world,i.normal,_WorldSpaceCameraPos*float3(1,1,-1));return float4(encode(c.x),encode(c.y),encode(c.z),1); }
 ENDCG
 } }
}
