#pragma once
#include "scene.h"
#include "prefab.h"

#include "light.h"

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

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;

		GFX::Texture* skybox_cubemap;

		SCN::Scene* scene;


		//updated every frame
		Renderer(const char* shaders_atlas_filename );

		//just to be sure we have everything ready for the rendering
		void setupScene();

		//add here your functions
		//...

		void parseNodes(SCN::Node* node, Camera* cam);

		void orderNodes(Camera* cam);

		void parseSceneEntities(SCN::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void renderQuadWithGFBO(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, bool firstlightingpass);

		void renderMeshwithTexture(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void showShininessSliders(SCN::Node* node);//Assignment 2

		Camera configureLightCamera(int index); //Assignment 3

		void renderToShadowMap();  //Assignment 3

		void rendertoGFBO(); //Assignment 4 takes data form scenen

		void rendertoLightFBO(); //Assignment 4 to accumulate lighting

		void createSpheresOfLights(std::vector<SCN::LightEntity*> lights); //assigment 4

		void renderPlain(const Camera& lightCam,
			const Matrix44& model,
			GFX::Mesh* mesh,
			SCN::Material* material); //Assignment 3

		void renderTexture(const Camera& lightCam,
			const Matrix44& model,
			GFX::Mesh* mesh,
			SCN::Material* material); //Assignment 4

		void showUI();
	};

};