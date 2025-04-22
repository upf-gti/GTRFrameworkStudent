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

	// Generate the renderer call struct
	struct sDrawCommand {
		GFX::Mesh* mesh;
		SCN::Material* material;
		Matrix44 model;
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
		std::vector<sDrawCommand> draw_command_opaque_list;		// Lab 1
		std::vector<sDrawCommand> draw_command_transparent_list;// Lab 1
		std::vector<LightEntity*> lights_list;					// Lab 2
		GFX::Texture* texture;
		GFX::FBO* shadow_fbo;


		//updated every frame
		Renderer(const char* shaders_atlas_filename );

		//just to be sure we have everything ready for the rendering
		void setupScene();

		//initialises the draw command lists for one entity
		void parseNode(SCN::Node* node, Camera* cam);

		//initialises the draw command and lights lists for all entities
		void parseSceneEntities(SCN::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap) const;

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		//render the shadows given a light and an FBO
		void renderShadows(LightEntity* light, GFX::FBO* fbo);

		void renderPlain(Camera cam, const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void showUI();
	};
};