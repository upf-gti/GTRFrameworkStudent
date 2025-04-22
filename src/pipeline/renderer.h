#pragma once
#include "scene.h"
#include "prefab.h"
#include "light.h"
#include "camera.h"


#define MAX_NUM_LIGHTS 10

//forward declarations
class Camera;
class Skeleton;
namespace GFX {
	class Shader;
	class Mesh;
	class FBO;
}

namespace SCN {
	
	class Prefab;
	class Material;

	//generate the renderer call struct
	struct sDrawCommand {
		GFX::Mesh* mesh = nullptr;
		SCN::Material* material = nullptr;
		Matrix44 model;
	};

	struct sLightCommand {
		int num_lights;
		vec3 light_ambient;
		vec3 light_positions[MAX_NUM_LIGHTS];
		vec3 light_colors[MAX_NUM_LIGHTS];
		vec3 light_directions[MAX_NUM_LIGHTS];
		float light_intensities[MAX_NUM_LIGHTS] = { 0.0f };
		float light_cos_angle_max[MAX_NUM_LIGHTS] = { 0.0f };
		float light_cos_angle_min[MAX_NUM_LIGHTS] = { 0.0f };
		int light_types[MAX_NUM_LIGHTS] = { 0 };
	};


	//this class is in charge of rendering anything in our system.
	//separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;
		GFX::Texture* skybox_cubemap;
		SCN::Scene* scene;

		std::vector<SCN::PrefabEntity*> prefab_list; //lab 1
		std::vector<SCN::sDrawCommand> draw_command_opaque_list; //lab 1
		std::vector<SCN::sDrawCommand> draw_command_transparent_list;//lab 1

		std::vector<SCN::LightEntity*> light_list; //lab 2
		SCN::sLightCommand light_command; //lab 2
		
		std::vector<GFX::FBO*> shadow_FBOs; //lab 3
		std::vector<Camera*> camera_light_list; //lab 3
		
		//updated every frames
		Renderer(const char* shaders_atlas_filename);

		//just to be sure we have everything ready for the rendering
		void setupScene();

		//initialises the draw command lists for one entity
		void parseNode(SCN::Node* node, Camera* cam);

		//initialises the draw commands for all entities
		void parsePrefabs(std::vector<SCN::PrefabEntity*> prefab_list, Camera* camera);

		//initialises the light command for all entities
		void parseLights(std::vector<SCN::LightEntity*> light_list, SCN::Scene* scene);

		void parseCameraLights(std::vector<SCN::LightEntity*> light_list);

		//initialises the draw command and lights lists for all entities
		void parseSceneEntities(SCN::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap) const;

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material) const;

		//render the shadows given a light and an FBO
		//void renderShadows(LightEntity* light, GFX::FBO* shadow_FBO);
		void renderShadows(Camera* light_camera, GFX::FBO* shadow_fbo);

		//render the an entity from the point of view of the light camera
		void renderPlain(Camera* camera, const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void showUI();
	};
};
