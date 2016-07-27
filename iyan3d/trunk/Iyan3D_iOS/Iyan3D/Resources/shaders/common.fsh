precision highp float;

uniform sampler2D colorMap;
uniform sampler2D shadowMap;
uniform sampler2D normalMap;
uniform sampler2D reflectionMap;

uniform float shadowDarkness, hasReflectionMap, hasNormalMap, ambientLight, lightCount;
uniform vec3 lightColor[5], lightPos[5];
uniform float fadeEndDistance[5], lightTypes[5];

varying float vHasLighting, vReflectionValue, vHasMeshColor, vTransparencyValue, vShadowDist;
varying vec2 vTexCoord, vTexCoordBias, vReflectionCoord;
varying vec3 vMeshColor;
varying vec3 vEyeVec, vVertexPosition;
varying mat3 vTBNMatrix;

float unpack(const in vec4 rgba_depth)
{
    const vec3 bit_shift = vec3(1.0/65536.0, 1.0/256.0, 1.0);
    float depth = dot(rgba_depth.rgb, bit_shift);
    return depth;
}

float GetShadowValue()
{
    vec4 texelColor = texture2D(shadowMap, texCoordsBias);
    vec4 unpackValue = vec4(texelColor.xyz, 1.0);
    float extractedDistance = unpack(unpackValue);
    float shadow = clamp(extractedDistance - vShadowDist, 0.0, shadowDarkness);
    return shadow;
}

lowp vec4 getColorOfLight(in int index, in vec3 normal)
{
    vec3 lightDir = (lightTypes[index] == 1.0) ? lightPos[index] :  normalize(lightPos[index] - vertexPosCam);
    float distanceFromLight = distance(lightPos[index] , vertexPosCam);

    float distanceRatio = (1.0 - clamp((distanceFromLight / fadeEndDistance[index]), 0.0, 1.0));
    distanceRatio = mix(1.0, distanceRatio, lightTypes[index]);

    float n_dot_l = clamp(dot(normal, lightDir), 0.0, 1.0);

    float darkness = distanceRatio * n_dot_l;
    darkness = clamp(darkness, ambientLight, 1.0);

    lowp vec4 colorOfLight = vec4(vec3(lightColor[index]) * darkness, 1.0);
    return colorOfLight;
}

float getSpecularOfLight(in int index, in vec3 normal)
{
    vec3 lightDir = (lightTypes[index] == 1.0) ? lightPos[index] :  normalize(lightPos[index] - vertexPosCam);
    float n_dot_l = clamp(dot(normal, lightDir), 0.0, 1.0);

    vec3 reflectValue = -lightDir + 2.0 * n_dot_l * normal;
    float e_dot_r =  clamp(dot(vEyeVec, vReflectionValue), 0.0, 1.0);
    
    float maxSpecular = 30.0;
    float s = vReflectionValue * pow(e_dot_r, maxSpecular);
    
    float e_dot_l = dot(lightDir, eyeVec);
    if(e_dot_l < -0.8)
        s = 0.0;
    
    return s;
}

void main()
{
    lowp vec4 diffuse_color = mix(vec4(vMeshColor, 1.0), texture2D(colorMap, vTexCoord), vHasMeshColor);
    
    if(diffuse_color.a <= 0.5)
        discard;
    
    float shadowValue = mix(0.0, GetShadowValue(), vShadowDist);

    vec3 normal = normalize(vTBNMatrix[2]);
    if(hasNormalMap > 0.5) {
        vec3 n = texture2D(normalMap, vTexCoord).xyz * 2.0 - 1.0;
        normal = normalize(tbnMatrix * n);
    }

    lowp vec4 specular = vec4(0.0)
    lowp vec4 colorOfLight = vec4(1.0);

    if(vHasLighting > 0.5) {
        if(hasReflectionMap > 0.5)
            specular = texture2D(reflectionMap, vReflectionCoord);
        else
            specular = vec4(getSpecularOfLight(0, normal));
        
        colorOfLight = vec4(0.0);
        for (int i = 0; i < lightCount; i++) {
            colorOfLight += getColorOfLight(i, normal);
        }
    }
    
    vec4 finalColor = vec4(diffuse_color + specular) * colorOfLight;

    if(hasReflectionMap > 0.5)
        finalColor = mix(diffuse_color, specular, vReflectionValue) * colorOfLight;
    
    finalColor -= (finalColor * shadowValue);
    
    gl_FragColor.xyz = finalColor.xyz;
    gl_FragColor.a = diffuse_color.a * vtransparency;
}


