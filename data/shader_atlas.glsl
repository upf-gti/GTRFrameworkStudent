//example of some shaders compiled
plain basic.vs plain.fs
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
compute test.cs
phong basic.vs phong.fs

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
in vec2 a_coord;
out vec2 v_uv;

void main()
{	
	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
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

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	if(color.a < u_alpha_cutoff)
		discard;

	FragColor = color;
}


\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	FragColor = color;
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

uniform int u_numLights;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform float u_alpha_cutoff;
uniform vec3 u_camera_position;
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform sampler2D u_texture_normal;

// Información de las luces
uniform vec3 u_light_pos[MAX_LIGHTS];
uniform vec3 u_light_color[MAX_LIGHTS];	
uniform float u_light_intensity[MAX_LIGHTS];
uniform int u_light_type[MAX_LIGHTS]; // 0: punto, 1: direccional
uniform bool u_apply_ambient;


// Luz ambiental global
uniform vec3 u_ambient_light;
uniform float u_shininess; // Brillo del material

// Luz spotlight
uniform vec3 u_light_direction[MAX_LIGHTS]; // D: Dirección del spotlight (cono)
uniform vec2 u_light_cone_info[MAX_LIGHTS]; // x: alpha_min, y: alpha_max (¡en radianes!)

//shadows
uniform sampler2D u_shadow_maps[MAX_SHADOW_CASTERS];
uniform mat4 u_shadow_vps[MAX_SHADOW_CASTERS];
uniform float u_shadow_bias;
uniform int    u_numShadowCasters;


out vec4 FragColor;

void main() {
    // === Color base como en texture.fs ===
    vec4 tex_color = texture(u_texture, v_uv);
    vec4 color = tex_color * u_color;

    if (color.a < u_alpha_cutoff)
        discard;

    vec3 base_color = color.rgb;

    // Componentes Phong 
    vec3 ambient = vec3(0.0);
	if (u_apply_ambient) {
		ambient = u_ambient_light * base_color;
	}
    vec3 diffuse_total = vec3(0.0);
    vec3 specular_total = vec3(0.0);

    vec3 normal_pixel = texture(u_texture_normal, v_uv).rgb;
    vec3 N = perturbNormal(normalize(v_normal), v_world_position, v_uv, normal_pixel);
    vec3 V = normalize(u_camera_position - v_world_position);

    for (int i = 0; i < u_numLights; i++) {

        float shadow_factor = 1.0;
		if (i < u_numShadowCasters) {
            vec4 proj_pos     = u_shadow_vps[i] * vec4(v_world_position, 1.0);
            float real_depth  = (proj_pos.z - u_shadow_bias) / proj_pos.w;
            proj_pos /= proj_pos.w;
            vec2 shadow_uv    = proj_pos.xy * 0.5 + 0.5;
            float shadow_depth = texture(u_shadow_maps[i], shadow_uv).x;
            float current_depth = real_depth * 0.5 + 0.5;
            if (shadow_uv.x >= 0.0 && shadow_uv.x <= 1.0 &&
                shadow_uv.y >= 0.0 && shadow_uv.y <= 1.0)
            {
                if (current_depth > shadow_depth)
                    shadow_factor = 0.0;
            }
        }



        vec3 L;
        float attenuation = 1.0;

        if (u_light_type[i] == 0) { // Luz de punto
            L = normalize(u_light_pos[i] - v_world_position);
            float distance = length(u_light_pos[i] - v_world_position);
            attenuation = 1.0 / (distance * distance);
        }
        else if (u_light_type[i] == 1) { // Luz direccional
            L = normalize(u_light_direction[i]);
            attenuation = 1.0;
        }
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

		// === Aquí empieza el orden correcto ===
		float N_dot_L = clamp(dot(N, L), 0.0, 1.0);
		vec3 R = reflect(L, N);
		float R_dot_V = clamp(dot(R, V), 0.0, 1.0);

		vec3 light_diffuse = base_color * N_dot_L * u_light_color[i] * u_light_intensity[i] * attenuation;
		vec3 light_specular = base_color * u_light_color[i] * u_light_intensity[i] * attenuation * pow(R_dot_V, u_shininess);

	// todas las luces con shadow_factor<1 lo tomarán en cuenta
	if (i < u_numShadowCasters &&(u_light_type[i]==1 || u_light_type[i]==2) ){
		light_diffuse  *= shadow_factor;
		light_specular *= shadow_factor;
   }

	diffuse_total += light_diffuse;
	specular_total += light_specular;
			
    }

	vec3 final_color = ambient + (diffuse_total + specular_total);
    FragColor = vec4(final_color, color.a);
}

\plain.fs
#version 330 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.0,0.0,0.0,1.0);
}
