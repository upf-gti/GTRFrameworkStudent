#include "renderer.h"

#include <algorithm> // sort

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

void Renderer::parseNodes(SCN::Node* node, Camera* cam)
{
	if (!node) return;

	// parse all nodes including children
	for (SCN::Node* child : node->children) {
		parseNodes(child, cam);
	}

	if (!node->mesh) return;

	// start rustum culling
	bool in_frustum = cam->testBoxInFrustum(node->aabb.center, node->aabb.halfsize); // note: aabb is the bounding box

	if (!in_frustum) return;
	// end rustum culling

	// since we will draw it for sure we create the renderable
	s_DrawCommand draw_command{
			node->getGlobalMatrix(),
			node->mesh,
			node->material
	};

	// start transparencies
	if (node->isTransparent()) {
		draw_commands_transp.push_back(draw_command);
	}
	else {
		draw_commands_opaque.push_back(draw_command);
	}
	// end transparencies
}

void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) {
	// HERE =====================
	// TODO: GENERATE RENDERABLES
	// ==========================

	// important to clear the list in each pass
	draw_commands_opaque.clear();
	draw_commands_transp.clear();
	
	light_info.count = 0;

	Matrix44 gm;

	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];

		if (!entity->visible) {
			continue;
		}

		switch (entity->getType())
		{
		case eEntityType::PREFAB:
		{
			// once we know it is a PREFAB entity perform static cast
			PrefabEntity* prefab_entity = static_cast<PrefabEntity*>(entity);

			// parse all nodes (including children)
			parseNodes(&prefab_entity->root, cam);
			break;
		}
		case eEntityType::LIGHT:
		{
			// Store Lights
			LightEntity* light = static_cast<LightEntity*>(entity);
			gm = light->root.getGlobalMatrix();

			light_info.intensities[light_info.count] = light->intensity;
			light_info.types[light_info.count] = light->light_type;
			light_info.colors[light_info.count] = light->color;
			light_info.positions[light_info.count] = gm.getTranslation();
			light_info.directions[light_info.count] = gm.frontVector();
			light_info.count++;
			break;
		}
		default:
			break;
		}
	}

	// camera eye is used to sort both opaque and transparent entities
	Vector3f ce = cam->eye;

	// sort opaque entities in ascending order (front to back)
	std::sort(draw_commands_opaque.begin(), draw_commands_opaque.end(), [&ce](s_DrawCommand& a, s_DrawCommand& b) {
		return a.model.getTranslation().distance(ce) < b.model.getTranslation().distance(ce);
		});

	// sort transparent entities in descending order (back to front)
	std::sort(draw_commands_transp.begin(), draw_commands_transp.end(), [&ce](s_DrawCommand& a, s_DrawCommand& b) {
		return a.model.getTranslation().distance(ce) > b.model.getTranslation().distance(ce);
		});
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
	if(skybox_cubemap)
		renderSkybox(skybox_cubemap);

	// HERE =====================
	// TODO: RENDER RENDERABLES
	// ==========================

	// first render opaque entities
	for (s_DrawCommand command : draw_commands_opaque) {
		renderMeshWithMaterial(command.model, command.mesh, command.material);
	}

	// then render transparent entities
	for (s_DrawCommand command : draw_commands_transp) {
		renderMeshWithMaterial(command.model, command.mesh, command.material);
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
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("singlepass");

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
	shader->setUniform("u_time", t );

	// Upload some material properties
	shader->setUniform("u_roughness", material->roughness_factor);
	//shader->setUniform("u_metallic", material->metallic_factor);

	// upload light-related uniforms
	// setUniform3Array for SINGLEPASS and then iterate over all of them (fixed count on for loop, if inside with another uniform for counting)
	shader->setUniform("u_ambient_light", ambient_light);
	shader->setUniform("u_light_count", light_info.count);

	shader->setUniform1Array("u_light_intensity", light_info.intensities, MAX_LIGHTS);
	shader->setUniform1Array("u_light_type", light_info.types, MAX_LIGHTS);
	shader->setUniform3Array("u_light_position", (float*)light_info.positions, MAX_LIGHTS); // position
	shader->setUniform3Array("u_light_color", (float*)light_info.colors, MAX_LIGHTS);
	shader->setUniform3Array("u_light_direction", (float*)light_info.directions, MAX_LIGHTS);

	// Render just the verticies as a wireframe
	if (render_wireframe)
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
		
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	//add here your stuff
	//...
}

#else
void Renderer::showUI() {}
#endif