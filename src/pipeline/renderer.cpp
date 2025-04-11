#include "renderer.h"

#include <algorithm> //sort

#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"

#include "scene.h"

struct sDrawCommand {
	GFX::Mesh* mesh;
	SCN::Material* material;
	Matrix44 model;
};

std::vector<sDrawCommand> draw_command_list;
std::vector<sDrawCommand> transparentNodes;
std::vector<sDrawCommand> opaqueNodes;
std::vector<SCN::LightEntity*> lights;
using namespace SCN;

//some globals
GFX::Mesh sphere;

Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	scene = nullptr;
	skybox_cubemap = nullptr;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();
}

void Renderer::setupScene()
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;
}

void Renderer::orderNodes(Camera* cam) {

	// Separate the nodes into opaque and transparent
	for (sDrawCommand command : draw_command_list) {
		if (command.material->alpha_mode == SCN::eAlphaMode::BLEND) {
			transparentNodes.push_back(command);
		}
		else {
			opaqueNodes.push_back(command);
		}
	}

	// Sort the two vectors acording the indications
	std::sort(transparentNodes.begin(), transparentNodes.end(), [cam](sDrawCommand& n1, sDrawCommand& n2) {
		float distN1 = (n1.model.getTranslation() - cam->eye).length();
		float distN2 = (n2.model.getTranslation() - cam->eye).length();
		return distN1 < distN2;
		});

	std::sort(opaqueNodes.begin(), opaqueNodes.end(), [cam](sDrawCommand& n1, sDrawCommand& n2) {
		float distN1 = (n1.model.getTranslation() - cam->eye).length();
		float distN2 = (n2.model.getTranslation() - cam->eye).length();
		return distN1 > distN2;
		});

}

void Renderer::parseNodes(SCN::Node* node, Camera* cam) {
	if (!node || !node->visible) {
		return;
	}

	if (node->mesh) {

		Matrix44 global = node->getGlobalMatrix();

		// Obtener centro y radio del bounding sphere
		Vector3f center = global * node->mesh->box.center;
		float radius = node->mesh->radius;

		// Hacemos el test del frustum
		if (cam->testSphereInFrustum(center, radius) == 0) {

			//std::cout << "Nodo fuera de frustum: " << node->name << std::endl;

		//	return; // está fuera del frustum, no lo renderizamos
		}

		sDrawCommand draw_com;
		draw_com.mesh = node->mesh;
		draw_com.material = node->material;
		draw_com.model = node->getGlobalMatrix();

		draw_command_list.push_back(draw_com);
	}

	for (SCN::Node* child : node->children) {
		parseNodes(child, cam);
	}
}

void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) {
	// HERE =====================
	// TODO: GENERATE RENDERABLES
	// ==========================
	draw_command_list.clear();
	opaqueNodes.clear();
	transparentNodes.clear();
	lights.clear(); 
	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];

		if (!entity->visible) {
			continue;
		}

		if (entity->getType() == eEntityType::PREFAB) {
			parseNodes(&((PrefabEntity*)entity)->root, cam);
		}
		else if (entity->getType() == eEntityType::LIGHT) {
			LightEntity* lightEntity = (LightEntity*)entity;
			lights.push_back(lightEntity);
		}
		// Store Prefab Entitys
		// ...
		//		Store Children Prefab Entities

		// Store Lights
		// ...
	}

	orderNodes(cam);

}

void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();

	parseSceneEntities(scene, camera);

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (skybox_cubemap)
		renderSkybox(skybox_cubemap);

	// HERE =====================
	// TODO: RENDER RENDERABLES
	// ==========================

	for (sDrawCommand command : opaqueNodes) {
		Renderer::renderMeshWithMaterial(command.model, command.mesh, command.material);
	}

	for (sDrawCommand command : transparentNodes) {
		Renderer::renderMeshWithMaterial(command.model, command.mesh, command.material);
	}
}


void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	// Apply skybox necesarry config:
	// No blending, no dpeth test, we are always rendering the skybox
	// Set the culling aproppiately, since we just want the back faces
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	// Center the skybox at the camera, with a big sphere
	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	shader->setUniform("u_texture", cubemap, 0);

	sphere.render(GL_TRIANGLES);

	shader->disable();

	// Return opengl state to default
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("phong");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();


	material->bind(shader);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t);	

	const int MAX_LIGHTS = 100;
	int numLights = std::min<int>(static_cast<int>(lights.size()), MAX_LIGHTS);


	std::vector<Vector3f> lightPositions;
	std::vector<Vector3f> lightColors;
	std::vector<float> lightIntensities;
	std::vector<int> lightTypes;

	std::vector<Vector3f> lightDirections;
	std::vector<Vector2f> lightConeInfo;

	for (int i = 0; i < numLights; i++) {
		SCN::LightEntity* light = lights[i];

		// Tipo de luz (0: Punto, 1: Direccional)
		int type = 0; // 0: Punto, 1: Direccional, 2: Spotlight

		if (light->light_type == SCN::eLightType::DIRECTIONAL) {
			type = 1;
		}
		else if (light->light_type == SCN::eLightType::SPOT) {
			type = 2;
		}

		lightTypes.push_back(type);

		if (type == 1) { // Direccional
			Vector3f front = light->root.getGlobalMatrix().frontVector().normalize();
			lightPositions.push_back(front);
		}
		else if (type == 2) {
			Vector3f pos = light->root.getGlobalMatrix().getTranslation();
			Vector3f dir = light->root.getGlobalMatrix().frontVector().normalize();

			lightPositions.push_back(pos);
			lightDirections.push_back(dir);

			float alpha_min = DEG2RAD * light->cone_info.x;
			float alpha_max = DEG2RAD * light->cone_info.y;
			lightConeInfo.push_back(Vector2f(alpha_min, alpha_max));
		}
		else { // Puntual
			Vector3f pos = light->root.getGlobalMatrix().getTranslation();
			lightPositions.push_back(pos);
		}

		lightColors.push_back(light->color);
		lightIntensities.push_back(light->intensity);
	}


	// Enviar uniformes al shader
	shader->setUniform("u_numLights", numLights);
	shader->setUniform3Array("u_light_pos", reinterpret_cast<float*>(lightPositions.data()), numLights);
	shader->setUniform3Array("u_light_color", reinterpret_cast<float*>(lightColors.data()), numLights);
	shader->setUniform1Array("u_light_intensity", lightIntensities.data(), numLights);
	shader->setUniform1Array("u_light_type", lightTypes.data(), numLights);
	shader->setUniform("u_ambient_light", scene->ambient_light);

	shader->setUniform3Array("u_light_direction", reinterpret_cast<float*>(lightDirections.data()), numLights);
	shader->setUniform2Array("u_light_cone_info", reinterpret_cast<float*>(lightConeInfo.data()), numLights);

	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

#ifndef SKIP_IMGUI

void Renderer::showUI()
{

	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	//add here your stuff
	//...
}

#else
void Renderer::showUI() {}
#endif