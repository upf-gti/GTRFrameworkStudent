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

	this->current_pipeline = RenderPipeline::DEFERRED;
	this->current_gbuffer = GbufferType::ALBEDO_MAP;
	this->deferred_command.init(1024, 768);
}

Renderer::~Renderer()
{
	if (this->scene) {
		delete this->scene;
		this->scene = nullptr;
	}
	if (this->skybox_cubemap) {
		delete this->skybox_cubemap;
		this->skybox_cubemap = nullptr;
	}
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

// Store Children Prefab Entities
void Renderer::parseNode(Node* node, Camera* cam)
{
	if (!node) {
		return;
	}

	if (node->mesh) {
		DrawCommand draw = DrawCommand(node->mesh, 
			             node->material, 
			             node->getGlobalMatrix());
		if (node->material->alpha_mode == NO_ALPHA) {
			this->draw_command_opaque_list.push_back(draw);
		}
		else {
			this->draw_command_transparent_list.push_back(draw);
		}
	}

	for (Node* child : node->children) {
		this->parseNode(child, cam);
	}
}

void Renderer::parsePrefabs(std::vector<PrefabEntity*> prefab_list, Camera* camera)
{
	this->draw_command_opaque_list.clear();
	this->draw_command_transparent_list.clear();
	for (PrefabEntity* prefab : prefab_list) {
		Node* node = &prefab->root;
		this->parseNode(node, camera);
	}

	// Sort opaque objects from nearest to farthest
	std::sort(this->draw_command_opaque_list.begin(), this->draw_command_opaque_list.end(),
		[&](const DrawCommand& a, const DrawCommand& b) {
			float distA = (camera->eye - a.model.getTranslation()).length();
			float distB = (camera->eye - b.model.getTranslation()).length();
			return distA < distB;
		});

	// Sort transparent objects from farthest to nearest
	std::sort(this->draw_command_transparent_list.begin(), this->draw_command_transparent_list.end(),
		[&](const DrawCommand& a, const DrawCommand& b) {
			float distA = (camera->eye - a.model.getTranslation()).length();
			float distB = (camera->eye - b.model.getTranslation()).length();
			return distA > distB;
		});
}

void Renderer::parseCameraLights(std::vector<SCN::LightEntity*> light_list)
{
	for (Camera* camera : this->camera_light_list) { 
		delete camera;
	}
	this->camera_light_list.clear();
	for (LightEntity* light : light_list) {
		Camera* camera = light->getCamera();
		if (camera) {
			this->camera_light_list.push_back(camera);
		}
	}
}

//generate renderables
void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* camera) 
{
	this->light_list.clear();
	this->prefab_list.clear();

	for (BaseEntity* entity : scene->entities) {
		if (!entity->visible) {
			continue;
		}

		//store prefab entities
		if (entity->getType() == eEntityType::PREFAB) {
			this->prefab_list.push_back((PrefabEntity*)entity);
			continue;
		}

		//store light entities
		if (entity->getType() == eEntityType::LIGHT) {
			this->light_list.push_back((LightEntity*)entity);
		}
	}

	this->parsePrefabs(this->prefab_list, camera);
	this->parseCameraLights(this->light_list);
	this->light_command.parseLights(this->light_list, scene);
	this->shadow_command.parseShadows(this->camera_light_list, this->shadow_FBOs);
}

//render renderables 
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

	switch (this->current_pipeline) {
	case RenderPipeline::FORWARD:
		this->renderForward();
		break;
	case RenderPipeline::DEFERRED:
		this->renderDeferred();
		break;
	}

}

//renders the sky box of the scene
void Renderer::renderSkybox(GFX::Texture* cubemap) const
{
	Camera* camera = Camera::current;

	//apply skybox necesarry config:
	//no blending, no dpeth test, we are always rendering the skybox
	//set the culling aproppiately, since we just want the back faces
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	if (this->render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	//center the skybox at the camera, with a big sphere
	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);

	//upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_texture", cubemap, 0);
	shader->setUniform("u_model", m);

	sphere.render(GL_TRIANGLES);

	shader->disable();

	//return opengl state to default
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(DrawCommand draw_command) const
{
	Camera* camera = Camera::current;
	
	if (draw_command.check())
		return;

	glEnable(GL_DEPTH_TEST);
	assert(glGetError() == GL_NO_ERROR);

	GFX::Shader* shader = GFX::Shader::Get("texture");
	if (!shader)
		return;
	shader->enable();

	draw_command.material->bind(shader);

	//upload scene properties
	this->light_command.uploadUniforms(shader);
	this->shadow_command.uploadUniforms(shader);
	shader->setUniform("u_shininess", draw_command.material->shininess);
	shader->setUniform("u_model", draw_command.model);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	//upload time, for cool shader effects
	float time = getTime();
	shader->setUniform("u_time", time);

	//render just the verticies as a wireframe
	if (this->render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	draw_command.mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

//renders the meshes from the point of view of the light camera in textures menu
void Renderer::renderShadows(Camera* light_camera, GFX::FBO* shadow_fbo) const
{
	shadow_fbo->bind();
	glColorMask(false, false, false, false);
	glClear(GL_DEPTH_BUFFER_BIT);
	
	for (DrawCommand draw_command : this->draw_command_opaque_list) {
		this->renderShader(light_camera, draw_command, "plain");
	}

	glColorMask(true, true, true, true);
	shadow_fbo->unbind();
}

void Renderer::renderShader(Camera* camera, DrawCommand draw_command, const char* shader_name) const
{
	if (draw_command.check() || !camera)
		return;

	glEnable(GL_DEPTH_TEST);
	assert(glGetError() == GL_NO_ERROR);

	GFX::Shader* shader = GFX::Shader::Get(shader_name);
	if (!shader)
		return;
	shader->enable();

	draw_command.material->bind(shader);

	shader->setUniform("u_model", draw_command.model);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	draw_command.mesh->render(GL_TRIANGLES);

	shader->disable();

	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

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

	renderDeferredLightingPass(); //CRASHES WHEN CALLING THIS

	/*this->deferred_command.unbind();

	this->deferred_command.view(this->current_gbuffer);*/
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

#ifndef SKIP_IMGUI

void Renderer::showUI()
{
	ImGui::Checkbox("Wireframe", &this->render_wireframe);
	ImGui::Checkbox("Boundaries", &this->render_boundaries);
	
	//shadow bias UI
	static int selected_light = 0;
	int num_shadows = this->shadow_command.num_shadows;
	if (num_shadows > 0) {
		ImGui::Separator();
		ImGui::Text("Shadow Bias Editor");

		//light selector
		ImGui::SliderInt("Shadow Index", &selected_light, 0, num_shadows - 1);

		//clamp selected_light to valid range in case lights change dynamically
		selected_light = std::clamp(selected_light, 0, num_shadows - 1);

		//bias slider
		float& bias = this->shadow_command.biases[selected_light];
		ImGui::SliderFloat("Bias", &bias, 0.0001f, 0.1f, "%.5f");
	}

	//render mode UI
	static int selected_mode = (int)this->current_pipeline;
	ImGui::Separator();
	ImGui::Text("Render Mode Selector\n(0 forward, 1 deferred)");
	ImGui::SliderInt("Mode", &selected_mode, 0, 1);
	this->current_pipeline = (RenderPipeline)selected_mode;

	//select deferred texture
	if ((RenderPipeline)selected_mode == RenderPipeline::DEFERRED) {
		static int deferred_texture = (int)this->current_gbuffer;
		int num_textures = this->deferred_command.max_textures;
		ImGui::Separator();
		ImGui::Text("Deferred Texture Selector\n");
		ImGui::SliderInt("Texture", &deferred_texture, 0, num_textures - 1);
		this->current_gbuffer = (GbufferType)deferred_texture;
	}
}

#else
void Renderer::showUI() {}
#endif
