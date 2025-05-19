
#include "material.h"

#include "../core/includes.h"
#include "../gfx/texture.h"
#include "../gfx/shader.h"

using namespace SCN;

std::map<std::string, Material*> Material::sMaterials;
uint32 Material::s_last_index = 0;
Material Material::default_material;

const char* SCN::texture_channel_str[] = { "ALBEDO","EMISSIVE","OPACITY","METALLIC_ROUGHNESS","OCCLUSION","NORMALMAP" };


Material* Material::Get(const char* name)
{
	assert(name);
	std::map<std::string, Material*>::iterator it = sMaterials.find(name);
	if (it != sMaterials.end())
		return it->second;
	return NULL;
}

void Material::registerMaterial(const char* name)
{
	this->name = name;
	sMaterials[name] = this;
}

Material::~Material()
{
	if (name.size())
	{
		auto it = sMaterials.find(name);
		if (it != sMaterials.end())
			sMaterials.erase(it);
	}
}

void Material::Release()
{
	std::vector<Material *>mats;

	for (auto mp : sMaterials)
	{
		Material *m = mp.second;
		mats.push_back(m);
	}

	for (Material *m : mats)
	{
		delete m;
	}
	sMaterials.clear();
}

void Material::bind(GFX::Shader* shader) {
	// First, configure the OpenGL state with the material settings =======================
	{
		// Select the blending
		if (alpha_mode == SCN::eAlphaMode::BLEND)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		else
			glDisable(GL_BLEND);

		// Select if render both sides of the triangles
		if (two_sided)
			glDisable(GL_CULL_FACE);
		else
			glEnable(GL_CULL_FACE);

		// Check if any error
		assert(glGetError() == GL_NO_ERROR);
	}

	// Bind the textures and set uniforms =======================
	{
		GFX::Texture* texture = textures[SCN::eTextureChannel::ALBEDO].texture;

		// HERE =====================
		// TODO: Expand rfor the rest of materials (when you need to)
		//	texture = emissive_texture;
		//	texture = metallic_roughness_texture;
		//	texture = normal_texture;
		//	texture = occlusion_texture;
		// ==========================

		GFX::Texture* metallic_roughness_texture = textures[SCN::eTextureChannel::METALLIC_ROUGHNESS].texture;

		// We always force a default albedo texture

		GFX::Texture* normal_map = textures[SCN::eTextureChannel::NORMALMAP].texture;

		if (texture == NULL) {
			texture = GFX::Texture::getWhiteTexture(); //a 1x1 white texture
		}
		if (normal_map == NULL) {
			normal_map = GFX::Texture::getBlackTexture(); //a 1x1 white texture

		}

		// Set material color uniform
		shader->setUniform("u_color", color);

		// Bind the albedo texture to the shader (unit 0)
		if (texture) {
			shader->setUniform("u_texture", texture, 0);
		}
		// Bind the normal map texture to the shader (unit 1)
		if (normal_map) {
			shader->setUniform("u_texture_normal", normal_map, 1);
		}
		// Bind the metallic-roughness texture to the shader
		if (metallic_roughness_texture) {
			shader->setUniform("u_texture_metallic_roughness", metallic_roughness_texture, 2);
		}

		// Set the alpha cutoff value based on alpha mode
		shader->setUniform("u_alpha_cutoff", alpha_mode == SCN::eAlphaMode::MASK ? alpha_cutoff : 0.001f);

		// Set the shininess uniform for material specularity
		shader->setUniform("u_shininess", shininess);
	}
}