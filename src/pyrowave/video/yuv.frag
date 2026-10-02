#version 450
layout(set=0,binding=0) uniform sampler2D yPlane;
layout(set=0,binding=1) uniform sampler2D cbPlane;
layout(set=0,binding=2) uniform sampler2D crPlane;
// Negotiated limited-range YCbCr: Rec.709 SDR or BT.2020 PQ HDR.
// PQ-coded RGB passes through to the HDR10 scanout without tone mapping.
// Coefficients are supplied by the renderer to permit future color matrices.
layout(push_constant) uniform Conversion {
    vec4 redGreen; // R Cr, G Cb, G Cr, chroma neutral
    vec4 blueExtent; // B Cb, width, height, HDR flag
    vec4 range; // luma offset/scale, chroma scale, display area scale
} conversion;
// Bitmap glyphs reused from ProsperoLight's native HUD (BlackBearReloaded).
layout(set=0,binding=3,std430) readonly buffer HudText {uint enabled;uint chars[432];uint keyboardEnabled;uint keys[576];} hud;
const uint font[192]=uint[](0u,0u,406600728u,1572888u,13878u,0u,914306614u,3552895u,503528972u,794416u,406020864u,6514188u,1847342620u,7222075u,198150u,0u,101059608u,1575942u,404229126u,396312u,4282148352u,26172u,1057754112u,3084u,0u,101452800u,1056964608u,0u,0u,789504u,202911840u,66310u,2071159614u,4089711u,202116620u,4131852u,472920862u,4141830u,472920862u,1979184u,859192376u,7876735u,807338815u,1979184u,520291868u,1979187u,405812031u,789516u,506671902u,1979187u,1043542814u,923696u,789504u,789504u,789504u,101452800u,50727960u,1575942u,4128768u,16128u,806882310u,396312u,405811998u,786444u,2071683902u,1966971u,858988044u,3355455u,1046898239u,4154982u,50554428u,3958275u,1717974559u,2045542u,504776319u,8341014u,504776319u,984598u,50554428u,8152691u,1060320051u,3355443u,202116126u,1969164u,808464504u,1979187u,506881639u,6776374u,101058063u,8349254u,2139060067u,6513515u,2070898531u,6513523u,1667446300u,1848931u,1046898239u,984582u,858993438u,3677755u,1046898239u,6776374u,235352862u,1979192u,202124607u,1969164u,858993459u,4141875u,858993459u,794163u,2137744227u,6518655u,473326435u,6501916u,506671923u,1969164u,405889919u,8349260u,101058078u,1967622u,403441155u,4218928u,404232222u,1972248u,1664490504u,0u,0u,4278190080u,1575948u,0u,807272448u,7222078u,1040582151u,3892838u,857604096u,1979139u,1043345464u,7222067u,857604096u,1966911u,252065308u,984582u,862846976u,523255347u,1849034247u,6776422u,202244108u,1969164u,808452144u,506671920u,912655879u,6764062u,202116110u,1969164u,2134048768u,6515583u,857669632u,3355443u,857604096u,1979187u,1715142656u,252067430u,862846976u,2016427571u,1849360384u,984678u,54394880u,2043934u,205392904u,1584140u,858980352u,7222067u,858980352u,794163u,1801650176u,3571583u,912457728u,6501916u,858980352u,523255347u,423559168u,4138508u,118230072u,3673100u,1579032u,1579032u,940313607u,461836u,15214u,0u,0u,0u);
layout(location=0) out vec4 rgb;
void main() {
    vec2 uv=gl_FragCoord.xy/conversion.blueExtent.yz;
    uv=(uv-0.5)/conversion.range.w+0.5;
    bool outside=any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)));
    float y=(texture(yPlane,uv).r-conversion.range.x)*conversion.range.y;
    float cb=(texture(cbPlane,uv).r-conversion.redGreen.w)*conversion.range.z;
    float cr=(texture(crPlane,uv).r-conversion.redGreen.w)*conversion.range.z;
    rgb=vec4(clamp(vec3(y+conversion.redGreen.x*cr,
                       y+conversion.redGreen.y*cb+conversion.redGreen.z*cr,
                       y+conversion.blueExtent.x*cb),0.0,1.0),1.0);
    if(outside) rgb=vec4(0,0,0,1);
    ivec2 pos=ivec2(gl_FragCoord.xy)-ivec2(48,48);
    if(hud.enabled!=0 && pos.x>=0 && pos.y>=0 && pos.x<72*24 && pos.y<6*24){
        ivec2 pixel=pos/3;
        uint code=clamp(hud.chars[(pixel.y/8)*72+pixel.x/8],32u,127u)-32u;
        uint row=uint(pixel.y%8);
        uint bits=(font[code*2u+row/4u]>>((row%4u)*8u))&255u;
        bool ink=(bits&(1u<<uint(pixel.x%8)))!=0;
        rgb=vec4(ink?vec3(conversion.blueExtent.w>0.5 ? 0.58 : 1.0):rgb.rgb*0.18,1.0);
    }
    ivec2 keyPos=ivec2(gl_FragCoord.xy)-ivec2(48,1920);
    if(hud.keyboardEnabled!=0 && keyPos.x>=0 && keyPos.y>=0 && keyPos.x<72*24 && keyPos.y<8*24){
        ivec2 pixel=keyPos/3;
        uint code=clamp(hud.keys[(pixel.y/8)*72+pixel.x/8],32u,127u)-32u;
        uint row=uint(pixel.y%8);
        uint bits=(font[code*2u+row/4u]>>((row%4u)*8u))&255u;
        bool ink=(bits&(1u<<uint(pixel.x%8)))!=0;
        rgb=vec4(ink?vec3(conversion.blueExtent.w>0.5 ? 0.58 : 1.0):rgb.rgb*0.12,1.0);
    }

}
