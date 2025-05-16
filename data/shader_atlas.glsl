//example of some shaders compiled
plain basic.vs plain.fs
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
compute test.cs
phong basic.vs phong.fs
quad quad.vs quad.fs
light_volume basic.vs phong_sphere.fs

\test.cs
#version 430 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() 
{
	vec4 i = vec4(0.0);
}

\basic.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;

uniform vec3 u_camera_pos;

uniform mat4 u_model;
uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\quad.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;
out vec2 v_uv;


uniform mat4 u_model;
uniform mat4 u_viewprojection;


//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
}

\quad.fs
#version 330 core

#define MAX_LIGHTS 100
#define MAX_SHADOW_CASTERS 4


mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
  // get edge vectors of the pixel triangle
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);

  // solve the linear system
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

  // construct a scale-invariant frame 
  float invmax = 1.0 / sqrt(max(dot(T,T), dot(B,B)));
  return mat3(normalize(T * invmax), normalize(B * invmax), N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	normal_pixel = normal_pixel * 255./127. - 128./127.;
	mat3 TBN = cotangentFrame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}

// Inputs from vertex shader
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

// Camera info
uniform vec3 u_camera_position;

// Material and textures
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform sampler2D u_texture_normal;

// Light info
uniform int u_numLights;
uniform vec3 u_light_pos[MAX_LIGHTS];
uniform vec3 u_light_color[MAX_LIGHTS];	
uniform float u_light_intensity[MAX_LIGHTS];
uniform int u_light_type[MAX_LIGHTS]; // 0 = point, 1 = directional, 2 = spotlight

// Optional ambient light
uniform bool u_apply_ambient;
uniform vec3 u_ambient_light;
uniform float u_shininess;

// Spotlight data
uniform vec3 u_light_direction[MAX_LIGHTS];
uniform vec2 u_light_cone_info[MAX_LIGHTS]; // x = min angle, y = max angle (in radians)

// Shadow mapping
uniform sampler2D u_shadow_maps[MAX_SHADOW_CASTERS];
uniform mat4 u_shadow_vps[MAX_SHADOW_CASTERS];
uniform float u_shadow_bias;
uniform int    u_numShadowCasters;

// Alpha discard
uniform float u_alpha_cutoff;

out vec4 FragColor; // Final color output

uniform vec2 u_inv_screen_size;
//Unifroms from gbuffer

uniform sampler2D u_gbuffer_color;
uniform sampler2D u_gbuffer_normal;
uniform sampler2D u_gbuffer_depth;

uniform samplerCube u_sky_text;

uniform mat4 u_inv_viewprojection;

uniform bool u_first_pass;

void main() {

	//Assigment 4 getting data from gbuffer
	vec2 uv = gl_FragCoord.xy * u_inv_screen_size;

	float depth = texture(u_gbuffer_depth, uv).r;
	float depth_clip = depth * 2.0 - 1.0;

	vec2 uv_clip = uv * 2.0 - 1.0;
	vec4 clip_coords = vec4(uv_clip.x, uv_clip.y, depth_clip,1.0);

	vec4 not_norm_world_pos = u_inv_viewprojection * clip_coords;

	vec3 world_pos = not_norm_world_pos.xyz / not_norm_world_pos.w;

    // Get the base color
    vec4 tex_color = texture(u_texture, v_uv);
    vec4 color = tex_color * u_color;
	color = texture(u_gbuffer_color,uv);


	// Discard the fragment if its alpha is below the cutoff (transparent)
    if (color.a < u_alpha_cutoff)
        discard;

    vec3 base_color = color.rgb;

	// Store base color for gFBO

    // Ambient term calculation
    vec3 ambient = vec3(0.0);
	if (u_apply_ambient && u_first_pass) {
		ambient = u_ambient_light * base_color;
	}

	// Initialize lighting accumulators
    vec3 diffuse_total = vec3(0.0);
    vec3 specular_total = vec3(0.0);

	// Get the perturbed normal from the normal map
    vec3 normal_pixel = texture(u_gbuffer_normal, uv).rgb;
    vec3 N = normal_pixel * 2.0 - 1.0;
    vec3 V = normalize(u_camera_position - world_pos);

	// Loop through all lights and calculate their contribution
    for (int i = 0; i < u_numLights; i++) {

        float shadow_factor = 1.0; // Default: no shadow

		// Calculate shadow for shadow-casting lights
		if (i < u_numShadowCasters) {
            vec4 proj_pos     = u_shadow_vps[i] * vec4(world_pos, 1.0);
            float real_depth  = (proj_pos.z - u_shadow_bias) / proj_pos.w;
            proj_pos /= proj_pos.w;
            vec2 shadow_uv    = proj_pos.xy * 0.5 + 0.5;
            float shadow_depth = texture(u_shadow_maps[i], shadow_uv).x;
            float current_depth = real_depth * 0.5 + 0.5;

			// Compare the fragment depth with the shadow depth to apply shadowing
            if (shadow_uv.x >= 0.0 && shadow_uv.x <= 1.0 &&
                shadow_uv.y >= 0.0 && shadow_uv.y <= 1.0)
            {
                if (current_depth > shadow_depth)
                    shadow_factor = 0.0;
            }
        }


		// Light direction and attenuation calculation
        vec3 L;
        float attenuation = 1.0;
		
		// Point light calculation
        if (u_light_type[i] == 0 && !u_first_pass) {
            L = normalize(u_light_pos[i] - world_pos);
            float distance = length(u_light_pos[i] - world_pos);
            attenuation = 1.0 / (distance * distance);
        }
		// Directional light calculation
        else if (u_light_type[i] == 1 && u_first_pass) { // Luz direccional
            L = normalize(u_light_direction[i]);
            attenuation = 1.0;
        }
		// Spotlight calculation
		else if (u_light_type[i] == 2 && !u_first_pass) { // Spotlight
  			L = normalize(u_light_pos[i] - world_pos);
            float distance = length(u_light_pos[i] - world_pos);
    
			vec3 D = normalize(u_light_direction[i]);
			float cos_angle = dot(L, D); 

			float cos_alpha_min = cos(u_light_cone_info[i].x);
			float cos_alpha_max = cos(u_light_cone_info[i].y);

			float spot_factor = 0.0;
			if (cos_angle >= cos_alpha_max) {
				spot_factor = clamp(
					(cos_angle - cos_alpha_max) / (cos_alpha_min - cos_alpha_max),
					0.0, 1.0
				);
			}

			attenuation = (1.0 / (distance * distance)) * spot_factor;
		}

		// Phong shading calculations: Diffuse and specular
		float N_dot_L = clamp(dot(N, L), 0.0, 1.0);
		vec3 R = reflect(-L, N); // Reflection vector
		float R_dot_V = clamp(dot(R, V), 0.0, 1.0); // View reflection term

		// Diffuse and specular lighting contributions
		vec3 light_diffuse = base_color * N_dot_L * u_light_color[i] * u_light_intensity[i] * attenuation;
		vec3 light_specular = base_color * u_light_color[i] * u_light_intensity[i] * attenuation * pow(R_dot_V, u_shininess);

		// Apply shadow factor to light if shadow exists
		if (i < u_numShadowCasters &&(u_light_type[i]==1 || u_light_type[i]==2) ){
			light_diffuse  *= shadow_factor;
			light_specular *= shadow_factor;
		}

        // Accumulate diffuse and specular contributions
		if(u_first_pass && u_light_type[i] == 1){
			diffuse_total += light_diffuse;
			specular_total += light_specular;
		}
		
			
    }

	if(depth >= 1.0 && u_first_pass)
	{
		// This is the skybox, don't shade
		vec3 E = world_pos - u_camera_position;
		vec4 skybox_color = texture( u_sky_text, E );
		FragColor = skybox_color;
		return;
	}

	// Final color calculation with ambient, diffuse, and specular components
	vec3 final_color = ambient + (diffuse_total + specular_total);
    FragColor = vec4(final_color, color.a);
	
}




\flat.fs

#version 330 core

uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}


\texture.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;
uniform mat4 u_model;
uniform bool u_transparent;

float dither4x4(vec2 position, float brightness)
{
  int x = int(mod(position.x, 4.0));
  int y = int(mod(position.y, 4.0));
  int index = x + y * 4;
  float limit = 0.0;

  if (x < 8) {
    if (index == 0) limit = 0.0625;
    if (index == 1) limit = 0.5625;
    if (index == 2) limit = 0.1875;
    if (index == 3) limit = 0.6875;
    if (index == 4) limit = 0.8125;
    if (index == 5) limit = 0.3125;
    if (index == 6) limit = 0.9375;
    if (index == 7) limit = 0.4375;
    if (index == 8) limit = 0.25;
    if (index == 9) limit = 0.75;
    if (index == 10) limit = 0.125;
    if (index == 11) limit = 0.625;
    if (index == 12) limit = 1.0;
    if (index == 13) limit = 0.5;
    if (index == 14) limit = 0.875;
    if (index == 15) limit = 0.375;
  }

  return brightness < limit ? 0.0 : 1.0;
}

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;
void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	if (u_transparent) {
        // Apply dithering instead of alpha cutoff
        if (dither4x4(gl_FragCoord.xy, color.a) == 0.0)
            discard;
    } else {
        // Normal alpha cutoff for opaque objects
        if (color.a < u_alpha_cutoff)
            discard;
    }

	vec3 normal = v_normal;
	normal = normalize(normal);
	FragColor = color;
	NormalColor = vec4(normal * 0.5 + 0.5,1.0);
}


\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
layout(location = 0) out vec4 FragColor;    // color buffer

uniform sampler2D u_gbuffer_depth;
uniform vec2 u_inv_screen_size;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	vec2 uv = gl_FragCoord.xy * u_inv_screen_size; //[0,1]

	float depth = texture(u_gbuffer_depth, uv).r;

	if (depth >= 1.0)
	{
		// This is the skybox, don't shade
		FragColor = color;
		return;
	}
	FragColor = vec4(0.0);

}


\multi.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, uv );

	if(color.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize(v_normal);

	FragColor = color;
	NormalColor = vec4(N,1.0);
}


\depth.fs

#version 330 core

uniform vec2 u_camera_nearfar;
uniform sampler2D u_texture; //depth map
in vec2 v_uv;
out vec4 FragColor;

void main()
{
	float n = u_camera_nearfar.x;
	float f = u_camera_nearfar.y;
	float z = texture2D(u_texture,v_uv).x;
	if( n == 0.0 && f == 1.0 )
		FragColor = vec4(z);
	else
		FragColor = vec4( n * (z + 1.0) / (f + n - z * (f - n)) );
}


\instanced.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;

in mat4 u_model;

uniform vec3 u_camera_pos;

uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\phong.fs
#version 330 core

#define MAX_LIGHTS 100
#define MAX_SHADOW_CASTERS 4


mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
  // get edge vectors of the pixel triangle
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);

  // solve the linear system
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

  // construct a scale-invariant frame 
  float invmax = 1.0 / sqrt(max(dot(T,T), dot(B,B)));
  return mat3(normalize(T * invmax), normalize(B * invmax), N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	normal_pixel = normal_pixel * 255./127. - 128./127.;
	mat3 TBN = cotangentFrame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}

// Inputs from vertex shader
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

// Camera info
uniform vec3 u_camera_position;

// Material and textures
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform sampler2D u_texture_normal;

// Light info
uniform int u_numLights;
uniform vec3 u_light_pos[MAX_LIGHTS];
uniform vec3 u_light_color[MAX_LIGHTS];	
uniform float u_light_intensity[MAX_LIGHTS];
uniform int u_light_type[MAX_LIGHTS]; // 0 = point, 1 = directional, 2 = spotlight

// Optional ambient light
uniform bool u_apply_ambient;
uniform vec3 u_ambient_light;
uniform float u_shininess;

// Spotlight data
uniform vec3 u_light_direction[MAX_LIGHTS];
uniform vec2 u_light_cone_info[MAX_LIGHTS]; // x = min angle, y = max angle (in radians)

// Shadow mapping
uniform sampler2D u_shadow_maps[MAX_SHADOW_CASTERS];
uniform mat4 u_shadow_vps[MAX_SHADOW_CASTERS];
uniform float u_shadow_bias;
uniform int    u_numShadowCasters;

// Alpha discard
uniform float u_alpha_cutoff;

out vec4 FragColor; // Final color output
   // color buffer
layout(location = 1) out vec4 NormalColor;  // normal buffer

void main() {
    // Get the base color
    vec4 tex_color = texture(u_texture, v_uv);
    vec4 color = tex_color * u_color;

	// Discard the fragment if its alpha is below the cutoff (transparent)
    if (color.a < u_alpha_cutoff)
        discard;

    vec3 base_color = color.rgb;

	// Store base color for gFBO

    // Ambient term calculation
    vec3 ambient = vec3(0.0);
	if (u_apply_ambient) {
		ambient = u_ambient_light * base_color;
	}

	// Initialize lighting accumulators
    vec3 diffuse_total = vec3(0.0);
    vec3 specular_total = vec3(0.0);

	// Get the perturbed normal from the normal map
    vec3 normal_pixel = texture(u_texture_normal, v_uv).rgb;
    vec3 N = perturbNormal(normalize(v_normal), v_world_position, v_uv, normal_pixel);
    vec3 V = normalize(u_camera_position - v_world_position);

	// Loop through all lights and calculate their contribution
    for (int i = 0; i < u_numLights; i++) {

        float shadow_factor = 1.0; // Default: no shadow

		// Calculate shadow for shadow-casting lights
		if (i < u_numShadowCasters) {
            vec4 proj_pos     = u_shadow_vps[i] * vec4(v_world_position, 1.0);
            float real_depth  = (proj_pos.z - u_shadow_bias) / proj_pos.w;
            proj_pos /= proj_pos.w;
            vec2 shadow_uv    = proj_pos.xy * 0.5 + 0.5;
            float shadow_depth = texture(u_shadow_maps[i], shadow_uv).x;
            float current_depth = real_depth * 0.5 + 0.5;

			// Compare the fragment depth with the shadow depth to apply shadowing
            if (shadow_uv.x >= 0.0 && shadow_uv.x <= 1.0 &&
                shadow_uv.y >= 0.0 && shadow_uv.y <= 1.0)
            {
                if (current_depth > shadow_depth)
                    shadow_factor = 0.0;
            }
        }


		// Light direction and attenuation calculation
        vec3 L;
        float attenuation = 1.0;
		
		// Point light calculation
        if (u_light_type[i] == 0) {
            L = normalize(u_light_pos[i] - v_world_position);
            float distance = length(u_light_pos[i] - v_world_position);
            attenuation = 1.0 / (distance * distance);
        }
		// Directional light calculation
        else if (u_light_type[i] == 1) { // Luz direccional
            L = normalize(u_light_direction[i]);
            attenuation = 1.0;
        }
		// Spotlight calculation
		else if (u_light_type[i] == 2) { // Spotlight
  			L = normalize(u_light_pos[i] - v_world_position);
            float distance = length(u_light_pos[i] - v_world_position);
    
			vec3 D = normalize(u_light_direction[i]);
			float cos_angle = dot(L, D); 

			float cos_alpha_min = cos(u_light_cone_info[i].x);
			float cos_alpha_max = cos(u_light_cone_info[i].y);

			float spot_factor = 0.0;
			if (cos_angle >= cos_alpha_max) {
				spot_factor = clamp(
					(cos_angle - cos_alpha_max) / (cos_alpha_min - cos_alpha_max),
					0.0, 1.0
				);
			}

			attenuation = (1.0 / (distance * distance)) * spot_factor;
		}

		// Phong shading calculations: Diffuse and specular
		float N_dot_L = clamp(dot(N, L), 0.0, 1.0);
		vec3 R = reflect(L, N); // Reflection vector
		float R_dot_V = clamp(dot(R, V), 0.0, 1.0); // View reflection term

		// Diffuse and specular lighting contributions
		vec3 light_diffuse = base_color * N_dot_L * u_light_color[i] * u_light_intensity[i] * attenuation;
		vec3 light_specular = base_color * u_light_color[i] * u_light_intensity[i] * attenuation * pow(R_dot_V, u_shininess);

		// Apply shadow factor to light if shadow exists
		if (i < u_numShadowCasters &&(u_light_type[i]==1 || u_light_type[i]==2) ){
			light_diffuse  *= shadow_factor;
			light_specular *= shadow_factor;
		}

        // Accumulate diffuse and specular contributions
		diffuse_total += light_diffuse;
		specular_total += light_specular;
			
    }

	// Final color calculation with ambient, diffuse, and specular components
	vec3 final_color = ambient + (diffuse_total + specular_total);
    FragColor = vec4(final_color, color.a);
	NormalColor = vec4(v_normal * 0.5 + 0.5,1.0); // Store normal in NormalColor for debugging
}

\plain.fs
#version 330 core

// UV coordinates passed from vertex shader
in vec2 v_uv; 

// Uniforms
uniform int   u_use_mask;
uniform float u_alpha_cutoff;
uniform sampler2D u_opacity_map;

out vec4 FragColor;

void main()
{
    // If masking is enabled
    if (u_use_mask == 1) {
        float a = texture(u_opacity_map, v_uv).x; // Sample red channel as alpha
        if (a < u_alpha_cutoff) discard; // Skip this fragment if too transparent
    }

	// Output black
    FragColor = vec4(0.0);
}

\phong_sphere.fs
#version 330 core

#define MAX_LIGHTS 100
#define MAX_SHADOW_CASTERS 4


mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
  // get edge vectors of the pixel triangle
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);

  // solve the linear system
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

  // construct a scale-invariant frame 
  float invmax = 1.0 / sqrt(max(dot(T,T), dot(B,B)));
  return mat3(normalize(T * invmax), normalize(B * invmax), N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	normal_pixel = normal_pixel * 255./127. - 128./127.;
	mat3 TBN = cotangentFrame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}

// Inputs from vertex shader
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

// Camera info
uniform vec3 u_camera_position;

// Material and textures

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform sampler2D u_texture_normal;


// Light info

uniform vec3 u_light_pos;
uniform vec3 u_light_color;	
uniform float u_light_intensity;
uniform int u_light_type; // 0 = point, 1 = directional, 2 = spotlight

// Optional ambient light
uniform float u_shininess;

// Spotlight data
uniform vec3 u_light_direction;
uniform vec2 u_light_cone_info; // x = min angle, y = max angle (in radians)

// Shadow mapping
uniform sampler2D u_shadow_maps;
uniform mat4 u_shadow_vps;
uniform float u_shadow_bias;
uniform int    u_numShadowCasters;

// Alpha discard
uniform float u_alpha_cutoff;

uniform vec2 u_inv_screen_size;
//Unifroms from gbuffer

uniform sampler2D u_gbuffer_color;
uniform sampler2D u_gbuffer_normal;
uniform sampler2D u_gbuffer_depth;


uniform samplerCube u_sky_text;

uniform mat4 u_inv_viewprojection;

layout(location = 0) out vec4 FragColor; // Final color output
   // color buffer
layout(location = 1) out vec4 NormalColor;  // normal buffer

void main() {
    //Assigment 4 getting data from gbuffer
	vec2 uv = gl_FragCoord.xy * u_inv_screen_size; //[0,1]
	float depth = texture(u_gbuffer_depth, uv).x;
	float depth_clip = depth * 2.0 - 1.0; // [0,1] -> [-1,1]

	vec2 uv_clip = uv * 2.0 - 1.0; // [0,1] -> [-1,1]
	vec4 clip_coords = vec4(uv_clip.x, uv_clip.y, depth_clip, 1.0);

	vec4 not_norm_world_pos = u_inv_viewprojection * clip_coords;

	vec3 world_pos = not_norm_world_pos.xyz / not_norm_world_pos.w;

    // Get the base color
    vec4 tex_color = texture(u_gbuffer_color, uv);
    vec4 color = tex_color;

	// Discard the fragment if its alpha is below the cutoff (transparent)
    if (color.a < u_alpha_cutoff)
        discard;

    vec3 base_color = color.rgb;

	// Store base color for gFBO

	// Initialize lighting accumulators
    vec3 diffuse_total = vec3(0.0);
    vec3 specular_total = vec3(0.0);

	// Get the perturbed normal from the normal map
	vec3 normal_fromuv = texture(u_gbuffer_normal, uv).rgb;
    vec3 normal_pixel = texture(u_texture_normal, uv).rgb;
    vec3 N = perturbNormal(normalize(normal_fromuv), world_pos, uv, normal_pixel);
    vec3 V = normalize(u_camera_position - world_pos);

	/*
	// Get the perturbed normal from the normal map
    vec3 normal_fromuv = texture(u_gbuffer_normal, uv).rgb;
	vec3 normal_pixel = texture(u_texture_normal, uv).rgb;
    vec3 N = perturbNormal(normalize(normal_fromuv), world_pos, uv, normal_pixel);
    //N = normalize(texture(u_gbuffer_normal, uv).rgb) * 2.0 -1.0;
	N = normalize(N);
    vec3 V = normalize(u_camera_position - world_pos);
	*/
	// Loop through all lights and calculate their contribution
   

        float shadow_factor = 1.0; // Default: no shadow

		// Calculate shadow for shadow-casting lights
		
            vec4 proj_pos   = u_shadow_vps * vec4(world_pos, 1.0);
            float real_depth  = (proj_pos.z - u_shadow_bias) / proj_pos.w;
            proj_pos /= proj_pos.w;
            vec2 shadow_uv    = proj_pos.xy * 0.5 + 0.5;
            float shadow_depth = texture(u_shadow_maps, shadow_uv).x;
            float current_depth = real_depth * 0.5 + 0.5;

			// Compare the fragment depth with the shadow depth to apply shadowing
            if (shadow_uv.x >= 0.0 && shadow_uv.x <= 1.0 &&
                shadow_uv.y >= 0.0 && shadow_uv.y <= 1.0)
            {
                if (current_depth > shadow_depth)
                    shadow_factor = 0.0;
            }
        


		// Light direction and attenuation calculation
        vec3 L;
        float attenuation = 1.0;
		
		// Point light calculation
        if (u_light_type == 0) {
            L = normalize(u_light_pos - world_pos);
            float distance = length(u_light_pos - world_pos);
            attenuation = 1.0 / (distance * distance);
        }
		// Spotlight calculation
		else if (u_light_type == 2) { // Spotlight
  			L = normalize(u_light_pos - world_pos);
			
            float distance = length(u_light_pos - world_pos);
    
			vec3 D = normalize(u_light_direction);
			float cos_angle = dot(L, D); 

			float cos_alpha_min = cos(u_light_cone_info.x);
			float cos_alpha_max = cos(u_light_cone_info.y);

			float spot_factor = 0.0;
			if (cos_angle >= cos_alpha_max) {
				spot_factor = clamp(
					(cos_angle - cos_alpha_max) / (cos_alpha_min - cos_alpha_max),
					0.0, 1.0
				);
			}

			attenuation = (1.0 / (distance * distance)) * spot_factor;
		}
		
		// Phong shading calculations: Diffuse and specular
		float N_dot_L = clamp(dot(N, L), 0.0, 1.0);
		vec3 R = reflect(L, N); // Reflection vector
		float R_dot_V = clamp(dot(R, V), 0.0, 1.0); // View reflection term

		// Diffuse and specular lighting contributions
		vec3 light_diffuse = base_color * N_dot_L * u_light_color * u_light_intensity * attenuation;
		vec3 light_specular = base_color * u_light_color * u_light_intensity * attenuation * pow(R_dot_V, 0.1);

		// Apply shadow factor to light if shadow exists
		
		if ((u_light_type==1 || u_light_type==2) ){
			light_diffuse  *= shadow_factor;
			light_specular *= shadow_factor;
		}
		
        // Accumulate diffuse and specular contributions
		diffuse_total += light_diffuse;
		specular_total += light_specular;
			
    

	// Final color calculation with ambient, diffuse, and specular components
	vec3 final_color = (diffuse_total + specular_total);
    FragColor = vec4(light_specular + light_diffuse, color.a);
}

