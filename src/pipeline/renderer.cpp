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

// A draw command: holds a mesh, a material, and its model matrix
struct sDrawCommand {
	GFX::Mesh* mesh;           // The object’s mesh (geometry)
	SCN::Material* material;   // The material used to draw it
	Matrix44 model;            // Transformation from object space to world space
};

// List of all draw commands (everything to draw)
std::vector<sDrawCommand> draw_command_list;

// List of transparent objects (to draw with blending)
std::vector<sDrawCommand> transparentNodes;

// List of opaque objects (can be drawn first)
std::vector<sDrawCommand> opaqueNodes;

// List of all lights in the scene
std::vector<SCN::LightEntity*> lights;

// If true, use multipass rendering (e.g., per-light passes)
bool use_multipass = false;

// Minimum alpha value to consider a pixel visible (used for alpha testing)
float alpha_cutoff = 0;

using namespace SCN;

//some globals
bool cull_front_faces = true;  
float shadow_bias = 0.001f; 
static const int MAX_SHADOW_CASTERS = 4;

GFX::Mesh sphere;

// Stores FBOs for shadow maps
std::vector<GFX::FBO>   shadow_FBOs;
// Stores view-projection matrices for each light
std::vector<Matrix44>   shadow_vps;

//Assigment 4
//Store the G-buffer FBO
GFX::FBO gbuffer_FBO;

Camera lightCam; // Assignment 3

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

	// Shadow map FBO setup (only depth, 1024x1024)
	shadow_FBOs.resize(MAX_SHADOW_CASTERS);
	for (auto& fbo : shadow_FBOs)
		fbo.setDepthOnly(1024, 1024);

	//Initiallize G fbo
	gbuffer_FBO.create(1024, 768,
		2, // Create two texture to render to
		GL_RGBA, // Each texture has an R G B and A channels
		GL_UNSIGNED_BYTE, // Uses 8 bits per channel
		false); // No depth texture


}

void Renderer::setupScene()
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;
}

void Renderer::orderNodes(Camera* cam) {

	// Separate draw commands based on their material alpha mode
	for (sDrawCommand command : draw_command_list) {
		
		if (command.material->alpha_mode == SCN::eAlphaMode::BLEND) {

			// Transparent objects (need to be drawn back-to-front)
			transparentNodes.push_back(command);

		}

		else {

			// Opaque objects (can be depth-tested normally)
			opaqueNodes.push_back(command);

		}
	}

	// Sort transparent nodes from far to near (back-to-front)
	std::sort(transparentNodes.begin(), transparentNodes.end(), [cam](sDrawCommand& n1, sDrawCommand& n2) {
		float distN1 = (n1.model.getTranslation() - cam->eye).length();
		float distN2 = (n2.model.getTranslation() - cam->eye).length();
		return distN1 < distN2;
		});

	// Sort opaque nodes from near to far (optional, can help with z-culling)
	std::sort(opaqueNodes.begin(), opaqueNodes.end(), [cam](sDrawCommand& n1, sDrawCommand& n2) {
		float distN1 = (n1.model.getTranslation() - cam->eye).length();
		float distN2 = (n2.model.getTranslation() - cam->eye).length();
		return distN1 > distN2;
		});

}

void Renderer::parseNodes(SCN::Node* node, Camera* cam) {
	// Skip null or invisible nodes
	if (!node || !node->visible) {
		return;
	}

	// If the node has a mesh, it's renderable
	if (node->mesh) {

		// Get the node's global transformation matrix
		Matrix44 global = node->getGlobalMatrix();

		// Obtener centro y radio del bounding sphere
		Vector3f center = global * node->mesh->box.center;
		float radius = node->mesh->radius;

		// Hacemos el test del frustum
		if (cam->testSphereInFrustum(center, radius) == 0) {

			//std::cout << "Nodo fuera de frustum: " << node->name << std::endl;

		//	return; // está fuera del frustum, no lo renderizamos
		}

		// Create a draw command for this mesh
		sDrawCommand draw_com;
		draw_com.mesh = node->mesh;
		draw_com.material = node->material;
		draw_com.model = node->getGlobalMatrix();

		// Add it to the draw command list
		draw_command_list.push_back(draw_com);
	}

	// Recursively process all child nodes
	for (SCN::Node* child : node->children) {
		parseNodes(child, cam);
	}
}

void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) {
	// HERE =====================
	// TODO: GENERATE RENDERABLES
	// ==========================

	// Clear previous frame's data
	draw_command_list.clear();
	opaqueNodes.clear();
	transparentNodes.clear();
	lights.clear();

	// Loop through all entities in the scene
	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];

		// Skip entities that are not visible
		if (!entity->visible) {
			continue;
		}

		// If it's a prefab (has geometry), parse its nodes
		if (entity->getType() == eEntityType::PREFAB) {
			parseNodes(&((PrefabEntity*)entity)->root, cam);
		}

		// If it's a light, store it in the lights list
		else if (entity->getType() == eEntityType::LIGHT) {
			LightEntity* lightEntity = (LightEntity*)entity;
			lights.push_back(lightEntity);
		}
	}

	// Sort opaque and transparent nodes based on camera distance
	orderNodes(cam);
}

void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();

	parseSceneEntities(scene, camera);

	renderToShadowMap();// Assignment 3  3.2.2

	rendertoGFBO(); // Assignment 4

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

	// Render all opaque objects first
	for (sDrawCommand command : opaqueNodes) {
		Renderer::renderMeshWithMaterial(command.model, command.mesh, command.material);
	}

	// Then render all transparent objects
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

	// Activate the shader
	shader->enable();

	// Define shadow map uniforms
	static const char* shadowNames[MAX_SHADOW_CASTERS] = {
			"u_shadow_maps[0]",
			"u_shadow_maps[1]",
			"u_shadow_maps[2]",
			"u_shadow_maps[3]"
	};

	// Determine the number of shadow casters
	int nSh = std::min<int>(lights.size(), MAX_SHADOW_CASTERS);

	// Bind material to the shader
	material->bind(shader);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_alpha_cutoff", alpha_cutoff);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t);

	// Prepare light information
	const int MAX_LIGHTS = 100;
	int numLights = std::min<int>(static_cast<int>(lights.size()), MAX_LIGHTS);
	std::vector<Vector3f> lightPositions;
	std::vector<Vector3f> lightColors;
	std::vector<float> lightIntensities;
	std::vector<int> lightTypes;
	std::vector<Vector3f> lightDirections;
	std::vector<Vector2f> lightConeInfo;

	// Collect light data
	for (int i = 0; i < numLights; i++) {

		
		SCN::LightEntity* light = lights[i];

		// Process each light based on type (point, directional, spotlight)
		int type = 0; // 0: Point, 1: Directional, 2: Spotlight
		if (light->light_type == SCN::eLightType::DIRECTIONAL) {
			type = 1;
		}
		else if (light->light_type == SCN::eLightType::SPOT) {
			type = 2;
		}

		lightTypes.push_back(type);

		// Add light data based on type
		if (type == 0) {
			Vector3f pos = light->root.getGlobalMatrix().getTranslation();
			lightPositions.push_back(pos);
			lightDirections.push_back(0);
			lightConeInfo.push_back(Vector2f(0, 0));
		}
		else if (type == 1) {
			Vector3f front = light->root.getGlobalMatrix().frontVector().normalize();
			lightDirections.push_back(front);
			lightPositions.push_back(0);
			lightConeInfo.push_back(Vector2f(0, 0));
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

		// Store light color and intensity
		lightColors.push_back(light->color);
		lightIntensities.push_back(light->intensity);
	}

	// Single pass rendering
	if (!use_multipass) {
		// We uploaded all the shadow maps at once
		for (int i = 0; i < nSh; ++i) {
			shader->setUniform(
				shadowNames[i],
				shadow_FBOs[i].depth_texture,
			    2 + i
			);
		}

		// Upload light and shadow data
		shader->setMatrix44Array("u_shadow_vps", shadow_vps.data(), nSh);
		shader->setUniform("u_numShadowCasters", nSh);
		shader->setUniform("u_shadow_bias", shadow_bias);
		shader->setUniform("u_numLights", numLights);
		shader->setUniform3Array("u_light_pos", reinterpret_cast<float*>(lightPositions.data()), numLights);
		shader->setUniform3Array("u_light_color", reinterpret_cast<float*>(lightColors.data()), numLights);
		shader->setUniform1Array("u_light_intensity", lightIntensities.data(), numLights);
		shader->setUniform1Array("u_light_type", lightTypes.data(), numLights);
		shader->setUniform("u_ambient_light", scene->ambient_light);
		shader->setUniform("u_apply_ambient", 1);
		shader->setUniform3Array("u_light_direction", reinterpret_cast<float*>(lightDirections.data()), numLights);
		shader->setUniform2Array("u_light_cone_info", reinterpret_cast<float*>(lightConeInfo.data()), numLights);

		// Render mesh
		if (render_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		mesh->render(GL_TRIANGLES);
	}
	// Multi-pass rendering
	else {

		// Set blending state for multi-pass rendering
		glDepthFunc(GL_LEQUAL);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		// Render each light separately with its shadow maps
		for (int i = 0; i < numLights; i++) {

			bool hasShadow = (i < nSh) && (lightTypes[i] == 1 || lightTypes[i] == 2);

			if (hasShadow) {
				shader->setUniform(shadowNames[0], shadow_FBOs[i].depth_texture, 2);
				shader->setMatrix44Array("u_shadow_vps", &shadow_vps[i], 1);
				shader->setUniform("u_numShadowCasters", 1);
				shader->setUniform("u_shadow_bias", shadow_bias);
			}
			else {
				// no hay sombra en esta pasada
				shader->setUniform("u_numShadowCasters", 0);
			}
			
			// Enable/disable blending depending on the light and apply or no ambient light
			if (i == 0) {
				glDisable(GL_BLEND);

				shader->setUniform("u_apply_ambient", 1);
			}
			else {
				glEnable(GL_BLEND);

				shader->setUniform("u_apply_ambient", 0);
			}

			// Upload light data for each pass
			shader->setUniform("u_numLights", 1);
			shader->setUniform3Array("u_light_pos", reinterpret_cast<const float*>(&lightPositions[i]), 1);
			shader->setUniform3Array("u_light_color", reinterpret_cast<const float*>(&lightColors[i]), 1);
			shader->setUniform1Array("u_light_intensity", &lightIntensities[i], 1);
			shader->setUniform1Array("u_light_type", &lightTypes[i], 1);
			shader->setUniform("u_ambient_light", scene->ambient_light);
			shader->setUniform3Array("u_light_direction", reinterpret_cast<const float*>(&lightDirections[i]), 1);
			shader->setUniform2Array("u_light_cone_info", reinterpret_cast<const float*>(&lightConeInfo[i]), 1);

			// Render mesh
			if (render_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			mesh->render(GL_TRIANGLES);
		}

		// Reset blending state
		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);
	}

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


//Render the scene using a quad and gFBO assigment 4
void Renderer::renderQuadWithGFBO(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
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

	// Activate the shader
	shader->enable();

	// Define shadow map uniforms
	static const char* shadowNames[MAX_SHADOW_CASTERS] = {
			"u_shadow_maps[0]",
			"u_shadow_maps[1]",
			"u_shadow_maps[2]",
			"u_shadow_maps[3]"
	};

	// Determine the number of shadow casters
	int nSh = std::min<int>(lights.size(), MAX_SHADOW_CASTERS);

	// Bind material to the shader
	material->bind(shader);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_alpha_cutoff", alpha_cutoff);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t);

	// Prepare light information
	const int MAX_LIGHTS = 100;
	int numLights = std::min<int>(static_cast<int>(lights.size()), MAX_LIGHTS);
	std::vector<Vector3f> lightPositions;
	std::vector<Vector3f> lightColors;
	std::vector<float> lightIntensities;
	std::vector<int> lightTypes;
	std::vector<Vector3f> lightDirections;
	std::vector<Vector2f> lightConeInfo;

	// Collect light data
	for (int i = 0; i < numLights; i++) {


		SCN::LightEntity* light = lights[i];

		// Process each light based on type (point, directional, spotlight)
		int type = 0; // 0: Point, 1: Directional, 2: Spotlight
		if (light->light_type == SCN::eLightType::DIRECTIONAL) {
			type = 1;
		}
		else if (light->light_type == SCN::eLightType::SPOT) {
			type = 2;
		}

		lightTypes.push_back(type);

		// Add light data based on type
		if (type == 0) {
			Vector3f pos = light->root.getGlobalMatrix().getTranslation();
			lightPositions.push_back(pos);
			lightDirections.push_back(0);
			lightConeInfo.push_back(Vector2f(0, 0));
		}
		else if (type == 1) {
			Vector3f front = light->root.getGlobalMatrix().frontVector().normalize();
			lightDirections.push_back(front);
			lightPositions.push_back(0);
			lightConeInfo.push_back(Vector2f(0, 0));
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

		// Store light color and intensity
		lightColors.push_back(light->color);
		lightIntensities.push_back(light->intensity);
	}

	// Single pass rendering
	if (!use_multipass) {
		// We uploaded all the shadow maps at once
		for (int i = 0; i < nSh; ++i) {
			shader->setUniform(
				shadowNames[i],
				shadow_FBOs[i].depth_texture,
				2 + i
			);
		}

		// Upload light and shadow data
		shader->setMatrix44Array("u_shadow_vps", shadow_vps.data(), nSh);
		shader->setUniform("u_numShadowCasters", nSh);
		shader->setUniform("u_shadow_bias", shadow_bias);
		shader->setUniform("u_numLights", numLights);
		shader->setUniform3Array("u_light_pos", reinterpret_cast<float*>(lightPositions.data()), numLights);
		shader->setUniform3Array("u_light_color", reinterpret_cast<float*>(lightColors.data()), numLights);
		shader->setUniform1Array("u_light_intensity", lightIntensities.data(), numLights);
		shader->setUniform1Array("u_light_type", lightTypes.data(), numLights);
		shader->setUniform("u_ambient_light", scene->ambient_light);
		shader->setUniform("u_apply_ambient", 1);
		shader->setUniform3Array("u_light_direction", reinterpret_cast<float*>(lightDirections.data()), numLights);
		shader->setUniform2Array("u_light_cone_info", reinterpret_cast<float*>(lightConeInfo.data()), numLights);

		// Render mesh
		if (render_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		mesh->render(GL_TRIANGLES);
	}
	// Multi-pass rendering
	else {

		// Set blending state for multi-pass rendering
		glDepthFunc(GL_LEQUAL);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		// Render each light separately with its shadow maps
		for (int i = 0; i < numLights; i++) {

			bool hasShadow = (i < nSh) && (lightTypes[i] == 1 || lightTypes[i] == 2);

			if (hasShadow) {
				shader->setUniform(shadowNames[0], shadow_FBOs[i].depth_texture, 2);
				shader->setMatrix44Array("u_shadow_vps", &shadow_vps[i], 1);
				shader->setUniform("u_numShadowCasters", 1);
				shader->setUniform("u_shadow_bias", shadow_bias);
			}
			else {
				// no hay sombra en esta pasada
				shader->setUniform("u_numShadowCasters", 0);
			}

			// Enable/disable blending depending on the light and apply or no ambient light
			if (i == 0) {
				glDisable(GL_BLEND);

				shader->setUniform("u_apply_ambient", 1);
			}
			else {
				glEnable(GL_BLEND);

				shader->setUniform("u_apply_ambient", 0);
			}

			// Upload light data for each pass
			shader->setUniform("u_numLights", 1);
			shader->setUniform3Array("u_light_pos", reinterpret_cast<const float*>(&lightPositions[i]), 1);
			shader->setUniform3Array("u_light_color", reinterpret_cast<const float*>(&lightColors[i]), 1);
			shader->setUniform1Array("u_light_intensity", &lightIntensities[i], 1);
			shader->setUniform1Array("u_light_type", &lightTypes[i], 1);
			shader->setUniform("u_ambient_light", scene->ambient_light);
			shader->setUniform3Array("u_light_direction", reinterpret_cast<const float*>(&lightDirections[i]), 1);
			shader->setUniform2Array("u_light_cone_info", reinterpret_cast<const float*>(&lightConeInfo[i]), 1);

			// Render mesh
			if (render_wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			mesh->render(GL_TRIANGLES);
		}

		// Reset blending state
		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);
	}

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

	// Checkbox to enable or disable multipass rendering
	ImGui::Checkbox("Use Multipass Rendering", &use_multipass);

	// Slider to adjust the shadow bias
	ImGui::SliderFloat("Shadow bias", &shadow_bias, 0.0f, 0.1f);

	// Checkbox to toggle the culling of front faces
	ImGui::Checkbox("Cull front faces", &cull_front_faces);

	// Slider to adjust the alpha cutoff for transparency-based rendering
	ImGui::SliderFloat("Alpha Cutoff", &alpha_cutoff, 0.0f, 1.0f);

	// If a prefab entity is selected, show shininess sliders for that prefab
	if (SCN::BaseEntity::s_selected && SCN::BaseEntity::s_selected->getType() == SCN::eEntityType::PREFAB)
	{
		SCN::PrefabEntity* prefab = (SCN::PrefabEntity*)SCN::BaseEntity::s_selected;
		showShininessSliders(&prefab->root); // Show shininess sliders for the prefab
	}
}

//recursive function to show the shininess sliders
void Renderer::showShininessSliders(SCN::Node* node)
{
	// Check if the node has a material
	if (node->material)
	{
		// Display the node name in the UI, showing what node we are editing
		ImGui::Text("Editing Node: %s", node->name.c_str());

		// Create a unique label for the shininess slider based on the node's name
		std::string label = "Shininess##" + node->name;

		// Display a slider to adjust the shininess of the node's material
		ImGui::SliderFloat(label.c_str(), &node->material->shininess, 0.0f, 100.0f);
	}

	// Recursively call the function for all child nodes
	for (SCN::Node* child : node->children)
	{
		showShininessSliders(child);
	}
}

// Assignment 3.2.1
Camera Renderer::configureLightCamera(int idx) {
	// Get the light entity by index
	LightEntity* light_ent = lights[idx];
	Camera light_cam;

	// Compute light position and orientation from its global transform
	Matrix44 light_model = light_ent->root.getGlobalMatrix();
	Vector3f light_pos = light_model.getTranslation();

	// Set camera to look in the light's forward direction
	light_cam.lookAt(light_pos, light_model * Vector3f(0.0f, 0.0f, -1.0f), Vector3f(0.0f, 1.0f, 0.0f));

	// Configure camera projection based on light type
	if (light_ent->light_type == eLightType::SPOT)
	{
		// Spot light uses perspective projection
		float fov = light_ent->cone_info.y * 2.0f; // Full cone angle
		float aspect = 1.0f; // Square shadow map
		light_cam.setPerspective(fov, aspect, light_ent->near_distance, light_ent->max_distance);
	}
	else if (light_ent->light_type == eLightType::DIRECTIONAL)
	{
		// Directional light uses orthographic projection
		float half_size = light_ent->area / 2.0f;
		light_cam.setOrthographic(
			-half_size, half_size,
			-half_size, half_size,
			light_ent->near_distance,
			light_ent->max_distance
		);
	}

	// Return configured light camera
	return light_cam;
}

// Assigment 4
void Renderer::rendertoGFBO() {
	if (lights.empty())
		return;
	// Save the original OpenGL state
	GLint old_viewport[4];
	glGetIntegerv(GL_VIEWPORT, old_viewport);
	GLint old_draw_buf;
	glGetIntegerv(GL_DRAW_BUFFER, &old_draw_buf);

	gbuffer_FBO.bind();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (sDrawCommand& cmd : opaqueNodes)
		renderMeshwithTexture(cmd.model, cmd.mesh, cmd.material);

	gbuffer_FBO.unbind();

}

void Renderer::renderMeshwithTexture(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material) {
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);
	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;
	glEnable(GL_DEPTH_TEST);
	//chose a shader
	shader = GFX::Shader::Get("texture");
	assert(glGetError() == GL_NO_ERROR);
	//no shader? then nothing to render
	if (!shader)
		return;
	// Activate the shader
	shader->enable();
	// Bind material to the shader
	material->bind(shader);
	//upload uniforms
	shader->setUniform("u_model", model);
	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_alpha_cutoff", alpha_cutoff);
	mesh->render(GL_TRIANGLES);
	shader->disable();
}
void Renderer::renderToShadowMap() {
	// Return early if there are no lights
	if (lights.empty())
		return;

	// Save the original OpenGL state
	GLint old_viewport[4];
	glGetIntegerv(GL_VIEWPORT, old_viewport);
	GLint old_draw_buf;
	glGetIntegerv(GL_DRAW_BUFFER, &old_draw_buf);

	const int SHADOW_RES = 1024; // Resolution of the shadow map
	int n = std::min<int>(lights.size(), MAX_SHADOW_CASTERS); // Number of lights to process
	shadow_vps.resize(n); // Resize the shadow view-projection matrices array

	// Loop through the lights to render their shadow maps
	for (int i = 0; i < n; ++i) {
		// Bind the shadow framebuffer and configure OpenGL for depth-only rendering
		shadow_FBOs[i].bind();
		glDrawBuffer(GL_NONE); // No color attachment
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // Disable color writes

		// Set shadow map resolution
		glViewport(0, 0, SHADOW_RES, SHADOW_RES);

		// Set depth settings
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		glClear(GL_DEPTH_BUFFER_BIT);

		// Configure face culling based on settings
		if (cull_front_faces) {
			glEnable(GL_CULL_FACE);
			glFrontFace(GL_CW);
		}
		else {
			glDisable(GL_CULL_FACE);
		}

		// Configure the camera for the current light and store the view-projection matrix
		lightCam = configureLightCamera(i);
		shadow_vps[i] = lightCam.viewprojection_matrix;

		// Render objects using depth-only pass
		for (sDrawCommand& cmd : opaqueNodes)
			renderPlain(lightCam, cmd.model, cmd.mesh, cmd.material);

		// Restore face culling settings
		if (cull_front_faces)
			glFrontFace(GL_CCW);

		// Restore color mask to allow rendering in color
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		shadow_FBOs[i].unbind();
	}

	// Restore original OpenGL state
	glViewport(old_viewport[0], old_viewport[1],
		old_viewport[2], old_viewport[3]);
	glDrawBuffer(old_draw_buf);
}

void Renderer::renderPlain(const Camera& C,
	const Matrix44& model,
	GFX::Mesh* mesh,
	SCN::Material* mat)
{
	if (!mesh || mesh->getNumVertices() == 0) return;

	GFX::Shader* s = GFX::Shader::Get("plain");
	s->enable();
	s->setUniform("u_model", model);
	s->setUniform("u_viewprojection", C.viewprojection_matrix);

	int useMask = (mat && mat->alpha_mode == SCN::MASK &&
		mat->textures[SCN::OPACITY].texture) ? 1 : 0;
	s->setUniform("u_use_mask", useMask);
	s->setUniform("u_alpha_cutoff",
		mat ? mat->alpha_cutoff : 0.5f);
	if (useMask) {
		s->setUniform("u_opacity_map",
			mat->textures[SCN::OPACITY].texture, 0);
	}

	mesh->render(GL_TRIANGLES);
	s->disable();

}

void Renderer::renderTexture(const Camera& C,
	const Matrix44& model,
	GFX::Mesh* mesh,
	SCN::Material* mat)
{
	if (!mesh || mesh->getNumVertices() == 0) return;

	GFX::Shader* s = GFX::Shader::Get("texture");
	s->enable();
	s->setUniform("u_model", model);
	s->setUniform("u_viewprojection", C.viewprojection_matrix);

	int useMask = (mat && mat->alpha_mode == SCN::MASK &&
		mat->textures[SCN::OPACITY].texture) ? 1 : 0;
	s->setUniform("u_use_mask", useMask);
	s->setUniform("u_alpha_cutoff",
		mat ? mat->alpha_cutoff : 0.5f);
	if (useMask) {
		s->setUniform("u_opacity_map",
			mat->textures[SCN::OPACITY].texture, 0);
	}

	mesh->render(GL_TRIANGLES);
	s->disable();

}

#else
void Renderer::showUI() {}
#endif