#pragma once
#include "scene.h"
#include "prefab.h"

#include "light.h"

constexpr auto MAX_LIGHTS = 10;

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

	// minimal information for a draw call of a node
	struct s_DrawCommand {
		Matrix44 model;
		GFX::Mesh* mesh;
		SCN::Material* material;
	};

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;

		GFX::Texture* skybox_cubemap;

		SCN::Scene* scene;
		Vector3f ambient_light = { 1.f };

		// setup opaque and transparent renderables
		std::vector<SCN::s_DrawCommand> draw_commands_opaque;
		std::vector<SCN::s_DrawCommand> draw_commands_transp;
		
		struct s_light_uniforms {
			uint8_t count = 0;
			float intensities[MAX_LIGHTS];
			float types[MAX_LIGHTS];
			vec3 colors[MAX_LIGHTS];
			vec3 positions[MAX_LIGHTS];
		};

		s_light_uniforms light_info;

		//updated every frame
		Renderer(const char* shaders_atlas_filename );

		//just to be sure we have everything ready for the rendering
		void setupScene();

		void parseSceneEntities(SCN::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void showUI();
		
		// Recursively iterate over all children of a node, adding the needed ones to renderables list
		void parseNodes(SCN::Node* node, Camera* cam);
	};
};