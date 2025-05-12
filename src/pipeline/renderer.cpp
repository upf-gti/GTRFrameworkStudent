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
#include "../pipeline/deferred.h"

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

	for (int i = 0; i < MAX_NUM_LIGHTS; i++) {
		GFX::FBO* shadow_FBO = new GFX::FBO();
		shadow_FBO->setDepthOnly(1024, 1024);
		this->shadow_FBOs.push_back(shadow_FBO);
	}

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();

	deferred.initGBuffer(1024, 1024);
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

	// Store Children Prefab Entities
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
		this->light_command.positions[i] = light->root.getGlobalMatrix().getTranslation();
		this->light_command.intensities[i] = light->intensity;
		this->light_command.types[i] = light->light_type;
		this->light_command.colors[i] = light->color;

		if (light->light_type == eLightType::DIRECTIONAL) {
			this->light_command.directions[i] = light->root.getGlobalMatrix().frontVector();
			this->light_command.cos_angle_max[i] = 0.0f;
			this->light_command.cos_angle_min[i] = 0.0f;
		}
		else if (light->light_type == eLightType::SPOT) {
			this->light_command.directions[i] = light->root.getGlobalMatrix().frontVector();
			this->light_command.cos_angle_max[i] = light->toCos(light->getCosAngleMax());
			this->light_command.cos_angle_min[i] = light->toCos(light->getCosAngleMin());
		}
		else {
			this->light_command.directions[i] = vec3(0.0f);
			this->light_command.cos_angle_min[i] = 0.0f;
			this->light_command.cos_angle_max[i] = 0.0f;
		}
		i++;
	}
}

void Renderer::parseShadows(std::vector<Camera*> camera_light_list) {
	this->shadow_command.num_shadows = min(MAX_NUM_LIGHTS, (int)this->camera_light_list.size());
	int j = 0;
	for (Camera* light_camera : camera_light_list) {
		this->shadow_command.slots[j] = 2 + j;
		this->shadow_command.depth_textures[j] = this->shadow_FBOs.at(j)->depth_texture;
		this->shadow_command.view_projections[j] = light_camera->viewprojection_matrix;
		if ((this->shadow_command.biases[j] == 0.0f) || (this->shadow_command.biases[j] == NULL)) {
			this->shadow_command.biases[j] = 0.005f;
		}
		j++;
	}
}

void Renderer::parseCameraLights(std::vector<SCN::LightEntity*> light_list)
{
	for (Camera* c : this->camera_light_list) { delete c; }
	this->camera_light_list.clear();

	for (LightEntity* light : light_list) {
		Camera* light_camera = new Camera();

		mat4 light_model = light->root.getGlobalMatrix();
		vec3 light_position = light_model.getTranslation();
		vec3 light_center = light_model * vec3(0.0f, 0.0f, -1.0f);
		vec3 light_up = vec3(0.0f, 1.0f, 0.0f);

		light_camera->lookAt(light_position, light_center, light_up);

		float half_size = light->area / 2.0f;
		float near_plane = light->near_distance;
		float far_plane = light->max_distance;

		if (light->light_type == eLightType::DIRECTIONAL) {
			light_camera->setOrthographic(-half_size, half_size, -half_size, half_size, near_plane, far_plane);
		}
		else if (light->light_type == eLightType::SPOT) {
			light_camera->setPerspective(2.0f * light->cone_info.y, 1.0f, near_plane, far_plane);
		}
		else {
			continue;
		}

		this->camera_light_list.push_back(light_camera);
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

	this->parseLights(this->light_list, scene);
	this->parsePrefabs(this->prefab_list, camera);
	this->parseCameraLights(this->light_list);
	this->parseShadows(this->camera_light_list);
}

// HERE =====================
// TODO: RENDER RENDERABLES
// ==========================
void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	this->setupScene();
	this->parseSceneEntities(scene, camera);

	for (int i = 0; i < this->camera_light_list.size(); i++) {
		Camera* camera_light = this->camera_light_list.at(i);
		GFX::FBO* shadow_fbo = this->shadow_FBOs.at(i);
		this->renderShadows(camera_light, shadow_fbo);
	}

	if (current_pipeline == RenderPipeline::DEFERRED) {
		deferred.render();
	}
	else {

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
	shader->enable();

	material->bind(shader);

	//upload the lights 
	const int n = this->light_command.num_lights;
	shader->setUniform("u_num_lights", this->light_command.num_lights);
	shader->setUniform("u_shininess", material->shininess);
	shader->setUniform("u_light_ambient", this->light_command.ambient);
	shader->setUniform3Array("u_light_positions", (float*)this->light_command.positions, n);
	shader->setUniform3Array("u_light_colors", (float*)this->light_command.colors, n);
	shader->setUniform3Array("u_light_directions", (float*)this->light_command.directions, n);
	shader->setUniform1Array("u_light_types", (int*)this->light_command.types, n);
	shader->setUniform1Array("u_light_cos_angle_max", (float*)this->light_command.cos_angle_max, n);
	shader->setUniform1Array("u_light_cos_angle_min", (float*)this->light_command.cos_angle_min, n);
	shader->setUniform1Array("u_light_intensities", (float*)this->light_command.intensities, n);
	
	//upload the shadowmaps
	const int m = this->shadow_command.num_shadows;
	for (int j = 0; j < m; j++) {
		glActiveTexture(GL_TEXTURE0 + this->shadow_command.slots[j]);
		glBindTexture(GL_TEXTURE_2D, this->shadow_command.depth_textures[j]->texture_id);
	}

	shader->setUniform("u_num_shadows", this->shadow_command.num_shadows);
	shader->setUniform1Array("u_shadow_maps", (int*)this->shadow_command.slots, m);
	shader->setMatrix44Array("u_shadow_vps", (Matrix44*)this->shadow_command.view_projections, m);
	shader->setUniform1Array("u_shadow_biases", (float*)this->shadow_command.biases, m);

	//upload model matrix
	shader->setUniform("u_model", model);

	//upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	//upload time, for cool shader effects
	float time = getTime();
	shader->setUniform("u_time", time);

	//render just the verticies as a wireframe
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
void Renderer::renderShadows(Camera* light_camera, GFX::FBO* shadow_fbo) 
{
	//Create the shadow frame buffer object
	shadow_fbo->bind();

	glColorMask(false, false, false, false);
	glClear(GL_DEPTH_BUFFER_BIT);

	//Render the meshes with the point of view of the light camera
	for (sDrawCommand& draw_command : this->draw_command_opaque_list) {
		this->renderPlain(light_camera, draw_command.model, draw_command.mesh, draw_command.material);
	}

	//Check parameters of the orthographic first!
	glColorMask(true, true, true, true);

	shadow_fbo->unbind();
}

void Renderer::renderPlain(Camera* camera, const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	if (!mesh || !mesh->getNumVertices() || !material || !camera)
		return;
	assert(glGetError() == GL_NO_ERROR);

	GFX::Shader* shader = nullptr;
	glEnable(GL_DEPTH_TEST);

	//use plain shader
	shader = GFX::Shader::Get("plain");

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

	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

<<<<<<< Updated upstream
=======
void Renderer::renderForward() const
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x,
		         scene->background_color.y,
		         scene->background_color.z, 
		         1.0);

	//clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (this->skybox_cubemap) {
		this->renderSkybox(this->skybox_cubemap);
	}

	//render opaque entities
	for (DrawCommand draw_command : this->draw_command_opaque_list) {
		this->renderMeshWithMaterial(draw_command);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Render transparent entities
	for (DrawCommand draw_command : this->draw_command_transparent_list) {
		this->renderMeshWithMaterial(draw_command);
	}

	glDisable(GL_BLEND);
}

void Renderer::renderDeferred()
{
	this->deferred_command.bind();

	//set the clear color (the background color)
	glClearColor(scene->background_color.x,
				 scene->background_color.y,
				 scene->background_color.z,
				 1.0);

	//clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (this->skybox_cubemap) {
		this->renderSkybox(this->skybox_cubemap);
	}

	//render opaque entities
	for (DrawCommand draw_command : this->draw_command_opaque_list) {
		this->renderShader(Camera::current, draw_command, "gbuffer_fill");
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDisable(GL_BLEND);

	renderDeferredLightingPass();
	
	this->deferred_command.view(this->current_gbuffer);
}

void Renderer::renderDeferredLightingPass() {

	Camera* camera = Camera::current;

	// 1. Unbind the GBuffer to draw to the screen
	this->deferred_command.unbind();

	// 2. Get the full-screen quad mesh
	GFX::Mesh* quad = GFX::Mesh::getQuad();

	// 3. Enable the lighting shader
	GFX::Shader* shader = GFX::Shader::Get("deferred_light_pass");
	if (!shader) return;
	shader->enable();

	// 4. Send light uniforms
	this->light_command.uploadUniforms(shader);
	this->shadow_command.uploadUniforms(shader);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_res_inv", vec2(1.0f / 1024, 1.0f / 768));
	shader->setUniform("u_inv_vp_mat", camera->viewprojection_matrix.inverse());

	// 5. Bind G-buffer textures
	GFX::FBO* fbo = this->deferred_command.gbuffer_FBO;
	int texture_slot = 0;
	shader->setTexture("u_gbuffer_color", fbo->color_textures[0], texture_slot++);
	shader->setTexture("u_gbuffer_normal", fbo->color_textures[1], texture_slot++);
	shader->setTexture("u_gbuffer_depth", fbo->depth_texture, texture_slot++);

	// 6. Render the full-screen quad
	quad->render(GL_TRIANGLES);

	// 7. Disable the shader
	shader->disable();
}

>>>>>>> Stashed changes
#ifndef SKIP_IMGUI

void Renderer::showUI()
{
	ImGui::Checkbox("Wireframe", &this->render_wireframe);
	ImGui::Checkbox("Boundaries", &this->render_boundaries);

	//add here your stuff
	
	// Shadow bias UI
	static int selected_light = 0;
	int num_lights = (int)this->shadow_command.num_shadows;
	if (num_lights > 0) {
		ImGui::Separator();
		ImGui::Text("Shadow Bias Editor");

		//Light selector
		ImGui::SliderInt("Light Index", &selected_light, 0, num_lights - 1);

		//Clamp selected_light to valid range in case lights change dynamically
		selected_light = std::clamp(selected_light, 0, num_lights - 1);

		//Bias slider
		float& bias = this->shadow_command.biases[selected_light];
		ImGui::SliderFloat("Bias", &bias, 0.0001f, 0.1f, "%.5f");
	}
}

#else
void Renderer::showUI() {}
#endif
