#include "deferred.h"
#include "../gfx/fbo.h"
#include "../gfx/texture.h"

Deferred::~Deferred() {
	if (gbuffer_FBO) {
		delete gbuffer_FBO;
		gbuffer_FBO = nullptr;
	}
}

void Deferred::initGBuffer(int width, int height) {
	if (gbuffer_FBO) {
		delete gbuffer_FBO;
	}

	gbuffer_FBO = new GFX::FBO();

	//Create 2 color attachments with 4 channels and floating point precision
	bool ok = gbuffer_FBO->create(width, height,
		2,                  //color_targets
		GL_RGBA,			//format
		GL_FLOAT,           //type
		true);              //use_depth_texture

	if (!ok) {
		printf("Error: Failed to create G-buffer FBO.\n");
	}
}

void Deferred::resize(int width, int height) {
	if (gbuffer_FBO) {
		gbuffer_FBO->create(width, height, 3, GL_RGBA16F, GL_FLOAT, true);
	}
}

void Deferred::bindGBuffer() {
	if (gbuffer_FBO)
		gbuffer_FBO->bind();
}

void Deferred::unbindGBuffer() {
	if (gbuffer_FBO)
		gbuffer_FBO->unbind();
}

//TODO
void Deferred::render()
{
	gbuffer_FBO->bind();
	// 1. Geometry Pass to fill G-buffer
	// 2. Lighting Pass (post-process quad with lighting)
	// 3. (Optional) Skybox and transparent pass if needed
	printf("Rendering using deferred renderer...");

	gbuffer_FBO->unbind();
}
