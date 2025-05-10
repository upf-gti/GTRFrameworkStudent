#pragma once

namespace GFX {
	class FBO;
}

class Deferred {
public:
	GFX::FBO* gbuffer_FBO = nullptr;

	Deferred() = default;
	~Deferred();

	void initGBuffer(int width, int height);
	void resize(int width, int height);
	void bindGBuffer();
	void unbindGBuffer();
	void render();
};

