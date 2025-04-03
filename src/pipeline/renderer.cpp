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

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();
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

void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) 
{
	// HERE =====================
	// TODO: GENERATE RENDERABLES
	// ==========================
	this->draw_command_opaque_list.clear();
	this->draw_command_transparent_list.clear();
	this->lights_list.clear();

	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];

		if (!entity->visible) {
			continue;
		}
	
		// Store Prefab Entities
		if (entity->getType() == eEntityType::PREFAB) {
			this->parseNode(&((PrefabEntity*)entity)->root, cam);
			continue;
		}

		// Store Lights
		if (entity->getType() == eEntityType::LIGHT) {
			this->lights_list.push_back((LightEntity*)entity);
		}

		// ...
	}
}

void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	this->setupScene();
	this->parseSceneEntities(scene, camera);

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (this->skybox_cubemap) {
		this->renderSkybox(this->skybox_cubemap);
	}

	// HERE =====================
	// TODO: RENDER RENDERABLES
	// ==========================
	
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

	for (sDrawCommand draw_command : this->draw_command_opaque_list) {
		renderMeshWithMaterial(draw_command.model, draw_command.mesh, draw_command.material);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	for (sDrawCommand draw_command : this->draw_command_transparent_list) {
		this->renderMeshWithMaterial(draw_command.model, draw_command.mesh, draw_command.material);
	}

	glDisable(GL_BLEND);
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
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
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

	// Sending the lights
	int num_lights = min(MAX_NUM_LIGHTS, this->lights_list.size());
	vec3 light_ambient = this->scene->ambient_light;
	vec3 light_positions[MAX_NUM_LIGHTS];
	vec3 light_colors[MAX_NUM_LIGHTS];
	float light_intensities[MAX_NUM_LIGHTS] = { 0 };
	int light_types[MAX_NUM_LIGHTS] = { 0 };

	for (int i = 0; i < num_lights; i++) {
		LightEntity* light = this->lights_list.at(i);
		light_positions[i] = light->root.getGlobalMatrix().getTranslation();
		light_intensities[i] = light->intensity;
		light_types[i] = light->light_type;
		light_colors[i] = light->color;
	}

	shader->setUniform("u_num_lights", num_lights);
	shader->setFloat("u_shininess", material->shininess);
	shader->setUniform3("u_light_ambient", light_ambient);
	shader->setUniform1Array("u_light_types", (int*)light_types, num_lights);
	shader->setUniform1Array("u_light_intensities", (float*)light_intensities, num_lights);
	shader->setUniform3Array("u_light_positions", (float*)light_positions, num_lights);
	shader->setUniform3Array("u_light_colors", (float*)light_colors, num_lights);

	//upload uniforms
	shader->setUniform("u_model", model);
	
	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// Upload time, for cool shader effects
	float time = getTime();
	shader->setUniform("u_time", time);

	// Render just the verticies as a wireframe
	if (this->render_wireframe)
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
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