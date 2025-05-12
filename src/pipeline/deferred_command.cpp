#include "deferred_command.h"

#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/material.h"

DeferredCommand::DeferredCommand()
{
	this->max_textures = 3;
	this->gbuffer_FBO = nullptr;
}

DeferredCommand::~DeferredCommand() 
{
	if (gbuffer_FBO) {
		delete gbuffer_FBO;
		gbuffer_FBO = nullptr;
	}
}

void DeferredCommand::init(int width, int height) 
{
	if (gbuffer_FBO) {
		delete gbuffer_FBO;
	}
	GFX::FBO* gbuffer_FBO = new GFX::FBO();
	int n = max_textures;
	bool status = gbuffer_FBO->create(width, height, n, GL_RGBA, GL_UNSIGNED_BYTE, true);
	if (!status) {
		printf("Error: Failed to create G-buffer FBO.\n");
	}
}

void DeferredCommand::resize(int width, int height) 
{
	if (!gbuffer_FBO) {
		return;
	}
	int n = max_textures;
	bool status = gbuffer_FBO->create(width, height, n, GL_RGBA16F, GL_FLOAT, true);
	if (!status) {
		printf("Error: Failed to resize G-buffer FBO.\n");
	}
}

void DeferredCommand::bind() 
{
	if (gbuffer_FBO)
		gbuffer_FBO->bind();
}

void DeferredCommand::unbind() 
{
	if (gbuffer_FBO)
		gbuffer_FBO->unbind();
}

void DeferredCommand::view(GbufferType type)
{
	if (gbuffer_FBO)
		gbuffer_FBO->color_textures[(int)type]->toViewport();
}