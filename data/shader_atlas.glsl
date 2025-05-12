//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
compute test.cs
plain basic.vs plain.fs
<<<<<<< Updated upstream
=======
gbuffer_fill basic.vs gbuffer_fill.fs
deferred_light_pass quad.vs deferred_light_pass.fs
>>>>>>> Stashed changes

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


//LAB2 lighting and LAB3 shadows
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

uniform int u_num_lights;					// Number of lights
uniform float u_shininess;					// Coefficient of shininess
uniform vec3 u_light_ambient;				// Ambient light (constant)
uniform int u_light_types[10];				// Light type
uniform vec3 u_light_positions[10];			// Light position
uniform vec3 u_light_colors[10];			// Light color
uniform float u_light_intensities[10];		// Light intensity
uniform vec3 u_camera_position;				// Camera position
uniform vec3 u_light_directions[10];		// Spotlight direction (D)
uniform float u_light_cos_angle_max[10];	// cos(alpha_max)
uniform float u_light_cos_angle_min[10];	// cos(alpha_min)

uniform int u_num_shadows;
uniform sampler2D u_shadow_maps[10];          
uniform mat4 u_shadow_vps[10];               
uniform float u_shadow_biases[10];             

out vec4 FragColor;

float computeShadow(int shadow_num) {                    
    vec4 light_space_pos = u_shadow_vps[shadow_num] * vec4(v_world_position, 1.0);
    vec3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords = (proj_coords + 1) / 2;

	bool outside_shadow = proj_coords.x < 0.0 || proj_coords.x > 1.0 || 
							proj_coords.y < 0.0 || proj_coords.y > 1.0 ||
							proj_coords.z < 0.0 || proj_coords.z > 1.0;
    if (outside_shadow)
        return 0.0;

    float shadow_closest_depth = texture(u_shadow_maps[shadow_num], proj_coords.xy).x;
    float current_depth = proj_coords.z;
    return shadow_closest_depth < (current_depth - u_shadow_biases[shadow_num]) ? 0.0 : 1.0;
}
void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	vec3 ambient_component = u_light_ambient;
	vec3 diffuse_component = vec3(0.0);
	vec3 specular_component = vec3(0.0);

	float shadow = 0.0;
	int shadow_num = 0;

	for (int i = 0; i < u_num_lights; i++) 
	{
		vec3 light_position;
		float attenuation;
		shadow = 1.0;

		if (u_light_types[i] == 0)
		{
			//NO LIGHT
			continue;
		}

		if (u_light_types[i] == 1)
		{	
			//POINT
			light_position = u_light_positions[i] - v_world_position;
			float distance = length(light_position);
			attenuation = 1.0 / max(pow(distance, 2), 0.00001);
		} 

		if (u_light_types[i] == 2)
		{  
			//SPOTLIGHT
			light_position = u_light_positions[i] - v_world_position;
			float distance = length(light_position);
			attenuation = 1.0 / max(pow(distance, 2), 0.00001);

			//Angular attenuation (spotlight cone)
			vec3 L = normalize(light_position);
			vec3 D = normalize(u_light_directions[i]); 

			float cos_max = u_light_cos_angle_max[i];
			float cos_min = u_light_cos_angle_min[i];
			
			float angular_attenuation = 0.0;
			if (dot(L, D) >= cos_max)
			{
				angular_attenuation = clamp((dot(L, D) - cos_max) / max(cos_min - cos_max, 0.00001), 0.0, 1.0);
			} 
			attenuation *= angular_attenuation;
			shadow = computeShadow(shadow_num); 
			shadow_num++;
		}

		if (u_light_types[i] == 3)
		{	 
			//DIRECTIONAL
			light_position = u_light_positions[i];
			attenuation = 1.0;
			shadow = computeShadow(shadow_num);
			shadow_num++;
		}

		light_position = normalize(light_position);
		vec3 view_position = normalize(u_camera_position - v_world_position);
		vec3 normal_direction = normalize(v_normal);
		vec3 reflection_direction = normalize(reflect(-light_position, normal_direction));
		vec3 light_color =  attenuation * u_light_colors[i] * u_light_intensities[i];

		float L_dot_N = clamp(dot(normal_direction, light_position), 0.0, 1.0);
		float R_dot_V = clamp(dot(reflection_direction, view_position), 0.0, 1.0);

		diffuse_component += shadow * light_color * L_dot_N;
		specular_component += shadow * light_color * pow(R_dot_V, u_shininess);
	}

	if(color.a < u_alpha_cutoff)
		discard;

	color.xyz *= ambient_component + diffuse_component + specular_component;
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

\plain.fs

#version 330 core

out vec4 FragColor;

void main()
{
	//Some alpha testing would be good here
	FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
<<<<<<< Updated upstream
=======

\gbuffer_fill.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;

uniform mat4 u_model;
uniform mat4 u_viewprojection;
uniform vec3 u_camera_position;

layout(location = 0) out vec4 gbuffer_albedo;
layout(location = 1) out vec4 gbuffer_normal_map;
layout(location = 2) out vec4 gbuffer_depth_map;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	
	color *= texture( u_texture, uv );
	gbuffer_albedo = color;
	
	gbuffer_normal_map = vec4((1 + normalize(v_normal))/2, 1.0);

	
	float depth = texture(u_texture, uv).r;
	float depth_clip = depth * 2.0 - 1.0;
	vec2 uv_clip = uv * 2.0 - 1.0;
	vec4 clip_coords = vec4(uv_clip.x, uv_clip.y, depth_clip, 1.0);
	vec4 not_norm_world_pos = u_viewprojection * clip_coords;
	vec3 world_pos = not_norm_world_pos.xyz / not_norm_world_pos.w;
	gbuffer_depth_map = vec4(world_pos, 1.0);
}


\deferred_light_pass.fs

#version 330 core

in vec2 v_uv;
out vec4 FragColor;

// G-buffer
uniform sampler2D u_gbuffer_color;
uniform sampler2D u_gbuffer_normal;
uniform sampler2D u_gbuffer_depth;

// View
uniform mat4 u_inv_vp_mat;
uniform vec3 u_camera_position;
uniform vec2 u_res_inv;

// Lighting
#define MAX_NUM_LIGHTS 16
uniform int u_num_lights;
uniform vec3 u_light_ambient;
uniform vec3 u_light_positions[MAX_NUM_LIGHTS];
uniform vec3 u_light_colors[MAX_NUM_LIGHTS];
uniform vec3 u_light_directions[MAX_NUM_LIGHTS];
uniform float u_light_intensities[MAX_NUM_LIGHTS];
uniform int u_light_types[MAX_NUM_LIGHTS]; // 0: point, 1: directional, 2: spot
uniform float u_light_cos_angle_max[MAX_NUM_LIGHTS];
uniform float u_light_cos_angle_min[MAX_NUM_LIGHTS];

const float shininess = 32.0;

// Shading
#define MAX_NUM_SHADOWS 16
uniform int u_num_shadows;
uniform sampler2D u_shadow_maps[MAX_NUM_SHADOWS];
uniform mat4 u_shadow_vps[MAX_NUM_SHADOWS];
uniform float u_shadow_biases[MAX_NUM_SHADOWS];


void main() {
    vec2 uv = gl_FragCoord.xy * u_res_inv;

    vec3 albedo = texture(u_gbuffer_color, uv).rgb;
    vec3 normal = normalize(texture(u_gbuffer_normal, uv).xyz);
    float depth = texture(u_gbuffer_depth, uv).r;

    float depth_clip = depth * 2.0 - 1.0;
    vec2 uv_clip = uv * 2.0 - 1.0;
    vec4 clip_coords = vec4(uv_clip, depth_clip, 1.0);

    vec4 pos_h = u_inv_vp_mat * clip_coords;
    vec3 world_pos = pos_h.xyz / pos_h.w;

    vec3 view_dir = normalize(u_camera_position - world_pos);
    vec3 final_color = u_light_ambient * albedo;

    for (int i = 0; i < u_num_lights && i < MAX_NUM_LIGHTS; ++i) {
        vec3 light_dir;
        float attenuation = 1.0;

        if (u_light_types[i] == 0) { // Point light
            vec3 light_vec = u_light_positions[i] - world_pos;
            float dist = length(light_vec);
            light_dir = normalize(light_vec);
            attenuation = 1.0 / (dist * dist); // Simple attenuation
        }
        else if (u_light_types[i] == 1) { // Directional light
            light_dir = normalize(-u_light_directions[i]);
        }
        else {
            continue;
        }

        // Phong lighting
        float diff = max(dot(normal, light_dir), 0.0);
        vec3 half_vec = normalize(light_dir + view_dir);
        float spec = pow(max(dot(normal, half_vec), 0.0), shininess);

        vec3 light_color = u_light_colors[i] * u_light_intensities[i];
        vec3 diffuse = diff * light_color * albedo;
        vec3 specular = spec * light_color;

        final_color += attenuation * (diffuse + specular);
    }

    FragColor = vec4(final_color, 1.0);
}
>>>>>>> Stashed changes
