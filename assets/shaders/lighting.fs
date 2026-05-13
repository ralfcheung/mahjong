#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Output fragment color
out vec4 finalColor;

// Lighting uniforms
uniform vec3 viewPos;
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 ambientColor;

// Material uniforms
uniform float shininess;
uniform float specularStrength;

// Optional: Use texture if provided (for faces)
uniform sampler2D texture0;
uniform int useTexture;

void main()
{
    // --- 1. Ambient ---
    vec3 ambient = ambientColor;

    // --- 2. Diffuse ---
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(lightPos - fragPosition);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // --- 3. Specular (Blinn-Phong) ---
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    
    // Procedural specular mask based on texture brightness (if texture is used)
    float mask = 1.0;
    if (useTexture == 1) {
        vec4 texColor = texture(texture0, fragTexCoord);
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        // Simple threshold: below 0.5 is ink (matte), above is plastic (glossy)
        mask = smoothstep(0.4, 0.6, lum); 
        // Keep *some* specular on ink, but much less (0.2x)
        mask = mix(0.2, 1.0, mask);
    }
    
    // Fresnel effect for plastic look (more reflective at glancing angles)
    float fresnel = pow(1.0 - max(dot(norm, viewDir), 0.0), 3.0);
    float finalSpecStrength = specularStrength * mask + (fresnel * 0.3); // Add subtle rim light

    vec3 specular = finalSpecStrength * spec * lightColor;

    // --- Combine ---
    // If drawing with texture (tile face), use texture color as base.
    // Otherwise use vertex color (fragColor) as base.
    vec4 baseColor = (useTexture == 1) ? texture(texture0, fragTexCoord) * fragColor : fragColor;

    // Apply lighting components
    // We add Specular directly to the RGB channels
    // The alpha channel remains unmodified from the base color
    vec3 lighting = (ambient + diffuse) * baseColor.rgb + specular;

    finalColor = vec4(lighting, baseColor.a);
}
