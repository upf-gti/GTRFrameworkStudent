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

using namespace SCN;

//some globals
GFX::Mesh sphere;


Renderer::Renderer(const char* shader_atlas_filename)
{
	this->render_wireframe = false;
	this->render_boundaries = false;
	this->scene = nullptr;
	this->skybox_cubemap = nullptr;

	/*for (int i = 0; i < MAX_NUM_LIGHTS; i++) {
		GFX::FBO* shadow_FBO = new GFX::FBO();
		shadow_FBO->setDepthOnly(1024, 1024);
		this->shadow_FBOs.push_back(shadow_FBO);
	}*/

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();

	texture = new GFX::Texture(1024, 1024);
	shadow_fbo = new GFX::FBO();
	shadow_fbo->setTexture(texture);
	shadow_fbo->setDepthOnly(1024, 1024);
}

void Renderer::setupScene()
{
	if (this->scene->skybox_filename.size()) {
		std::string filename = this->scene->base_folder + "/" + this->scene->skybox_filename;
		this->skybox_cubemap = GFX::Texture::Get(filename.c_str());
	}
	else {
		this->skybox_cubemap = nullptr;
	}
}


//store Children Prefab Entities
void Renderer::parseNode(SCN::Node* node, Camera* cam)
{
	if (!node) {
		return;
	}

	if (node->mesh) {
		sDrawCommand draw_command;
		draw_command.mesh = node->mesh;
		draw_command.material = node->material;
		draw_command.model = node->getGlobalMatrix();

		if (node->material->alpha_mode == NO_ALPHA) {
			this->draw_command_opaque_list.push_back(draw_command);
		}
		else {
			this->draw_command_transparent_list.push_back(draw_command);
		}
	}

	for (SCN::Node* child : node->children) {
		this->parseNode(child, cam);
	}
}

void Renderer::parsePrefabs(std::vector<SCN::PrefabEntity*> prefab_list, Camera* camera)
{
	this->draw_command_opaque_list.clear();
	this->draw_command_transparent_list.clear();
	for (PrefabEntity* prefab : prefab_list) {
		Node* node = &prefab->root;
		this->parseNode(node, camera);
	}
}

void Renderer::parseLights(std::vector<SCN::LightEntity*> light_list, SCN::Scene* scene)
{
	this->light_command.num_lights = min(MAX_NUM_LIGHTS, (int)light_list.size());
	int i = 0;
	for (LightEntity* light : light_list) {
		this->light_command.light_positions[i] = light->root.getGlobalMatrix().getTranslation();
		this->light_command.light_intensities[i] = light->intensity;
		this->light_command.light_types[i] = light->light_type;
		this->light_command.light_colors[i] = light->color;

		if (light->light_type == eLightType::SPOT) {
			this->light_command.light_directions[i] = light->root.getGlobalMatrix().rotateVector(light->direction);
			this->light_command.light_cos_angle_max[i] = light->toCos(light->getCosAngleMax());
			this->light_command.light_cos_angle_min[i] = light->toCos(light->getCosAngleMin());
		}
		else {
			this->light_command.light_directions[i] = vec3(0.0f);
			this->light_command.light_cos_angle_min[i] = 0.0f;
			this->light_command.light_cos_angle_max[i] = 0.0f;
		}
		i++;
	}
}


// HERE =====================
// TODO: GENERATE RENDERABLES
// ==========================
void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* camera) 
{
	this->light_list.clear();
	this->prefab_list.clear();

	for (BaseEntity* entity : scene->entities) {
		if (!entity->visible) {
			continue;
		}

		// Store Prefab Entities
		if (entity->getType() == eEntityType::PREFAB) {
			this->prefab_list.push_back((PrefabEntity*)entity);
			continue;
		}

		// Store Lights
		if (entity->getType() == eEntityType::LIGHT) {
			this->light_list.push_back((LightEntity*)entity);
		}
	}

	this->parseLights(light_list, scene);
	this->parsePrefabs(prefab_list, camera);
}

// HERE =====================
// TODO: RENDER RENDERABLES
// Entities --> opaque, transparents, lights
// Skybox
// Shadows: per a cada llum, fbo->bind, renderitzar l'escena des del punt de vista de la c�mera-llum 
// ==========================
void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	this->setupScene();
	this->parseSceneEntities(scene, camera);

	for (LightEntity* light : this->light_list) {
		if ((light->light_type == eLightType::DIRECTIONAL) or (light->light_type == eLightType::SPOT)) {
			this->renderShadows(light, shadow_fbo);
		}
	}
	// Set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	// Render skybox
	if (this->skybox_cubemap) {
		this->renderSkybox(this->skybox_cubemap);
	}

	// Sort opaque objects from nearest to farthest
	std::sort(this->draw_command_opaque_list.begin(), this->draw_command_opaque_list.end(),
		[&](const sDrawCommand& a, const sDrawCommand& b) {
			float distA = (camera->eye - a.model.getTranslation()).length();
			float distB = (camera->eye - b.model.getTranslation()).length();
			return distA < distB; // Closest first
		});

	// Sort transparent objects from farthest to nearest
	std::sort(this->draw_command_transparent_list.begin(), this->draw_command_transparent_list.end(),
		[&](const sDrawCommand& a, const sDrawCommand& b) {
			float distA = (camera->eye - a.model.getTranslation()).length();
			float distB = (camera->eye - b.model.getTranslation()).length();
			return distA > distB; // Farthest first
		});

	/////////////////////////////////////////
	// TODO: Render shadows
	//for (int i = 0; i < this->light_command.num_lights; i++) {
	//	this->renderShadows(this->light_list.at(i), this->shadow_FBOs.at(i));
	//}
	/////////////////////////////////////////

	// Render opaque entities
	for (sDrawCommand draw_command : this->draw_command_opaque_list) {
		this->renderMeshWithMaterial(draw_command.model,
			draw_command.mesh, draw_command.material);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Render transparent entities
	for (sDrawCommand draw_command : this->draw_command_transparent_list) {
		this->renderMeshWithMaterial(draw_command.model,
			draw_command.mesh, draw_command.material);
	}

	glDisable(GL_BLEND);
}

// Renders the sky box of the scene
void Renderer::renderSkybox(GFX::Texture* cubemap) const
{
	Camera* camera = Camera::current;

	// Apply skybox necesarry config:
	// No blending, no dpeth test, we are always rendering the skybox
	// Set the culling aproppiately, since we just want the back faces
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	if (this->render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	// Center the skybox at the camera, with a big sphere
	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_texture", cubemap, 0);
	shader->setUniform("u_model", m);

	sphere.render(GL_TRIANGLES);

	shader->disable();

	// Return opengl state to default
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material) const
{
	Camera* camera = Camera::current;

	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = nullptr;

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("texture");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	material->bind(shader);

	// Sending the lights
	int n = this->light_command.num_lights;
	shader->setUniform("u_num_lights", n);
	shader->setFloat("u_shininess", material->shininess);
	shader->setUniform3("u_light_ambient", this->light_command.light_ambient);
	shader->setUniform1Array("u_light_types", (int*)this->light_command.light_types, n);
	shader->setUniform1Array("u_light_intensities", (float*)this->light_command.light_intensities, n);
	shader->setUniform3Array("u_light_positions", (float*)this->light_command.light_positions, n);
	shader->setUniform3Array("u_light_colors", (float*)this->light_command.light_colors, n);
	shader->setUniform3Array("u_light_directions", (float*)this->light_command.light_directions, n);
	shader->setUniform1Array("u_light_cos_angle_min", (float*)this->light_command.light_cos_angle_min, n);
	shader->setUniform1Array("u_light_cos_angle_max", (float*)this->light_command.light_cos_angle_max, n);

	shader->setUniform("u_shadowmap", this->shadow_fbo->depth_texture, 2);

	// Upload model matrix
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// Upload time, for cool shader effects
	float time = (float)getTime();
	shader->setUniform("u_time", time);

	// Render just the verticies as a wireframe
	if (this->render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

//to render shadows (lab 3)
void Renderer::renderShadows(LightEntity* light, GFX::FBO* shadow_fbo) 
{
	//Create the shadow frame buffer object
	shadow_fbo->bind();

	glColorMask(false, false, false, false);
	glClear(GL_DEPTH_BUFFER_BIT);

	// Configure the light camera
	Camera light_camera;

	mat4 light_model = light->root.getGlobalMatrix();
	vec3 light_position = light_model.getTranslation();
	vec3 light_center = light_model * vec3(0.0f, 0.0f, -1.0f);
	vec3 light_up = vec3(0.0f, 1.0f, 0.0f);

	light_camera.lookAt(light_position, light_center, light_up);

	float half_size = light->area / 2.0f;
	float near_plane = light->near_distance;
	float far_plane = light->max_distance;

	if (light->light_type == eLightType::DIRECTIONAL) {
		light_camera.setOrthographic(-half_size, half_size, -half_size, half_size, near_plane, far_plane);
	}
	else if (light->light_type == eLightType::SPOT) {
		light_camera.setPerspective(2.0f * light->cone_info.y, 1.0f, near_plane, far_plane);
	}

	//Render the meshes with the point of view of the light camera
	for (sDrawCommand& draw_command : this->draw_command_opaque_list) {
		this->renderPlain(&light_camera, draw_command.model, draw_command.mesh, draw_command.material);
	}

	//Check parameters of the orthographic first!
	glColorMask(true, true, true, true);

	shadow_fbo->unbind();
}

void Renderer::renderPlain(Camera* camera, const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	GFX::Shader* shader = nullptr;
	glEnable(GL_DEPTH_TEST);

	//use plain shader
	shader = GFX::Shader::Get("plain");
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);

	assert(glGetError() == GL_NO_ERROR);

	if (!shader)
		return;
	shader->enable();

	material->bind(shader);
	shader->setUniform("u_model", model);

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	mesh->render(GL_TRIANGLES);

	shader->disable();

	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

#ifndef SKIP_IMGUI

void Renderer::showUI()
{
	ImGui::Checkbox("Wireframe", &this->render_wireframe);
	ImGui::Checkbox("Boundaries", &this->render_boundaries);

	//add here your stuff
	//...
}

#else
void Renderer::showUI() {}
#endif