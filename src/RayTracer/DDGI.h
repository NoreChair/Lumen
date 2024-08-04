#pragma once
#include "Integrator.h"
#include "shaders/integrators/ddgi/ddgi_commons.h"
class DDGI : public Integrator {
   public:
	DDGI(lumen::LumenInstance* scene, LumenScene* lumen_scene)
		: Integrator(scene, lumen_scene), config(CAST_CONFIG(lumen_scene->config.get(), DDGIConfig)) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	void update_ddgi_uniforms();

	DDGIUniforms ddgi_ubo;
	lumen::BufferOld ddgi_ubo_buffer;
	lumen::BufferOld direct_lighting_buffer;
	lumen::BufferOld probe_offsets_buffer;
	lumen::BufferOld g_buffer;

	lumen::Texture2D irr_texes[2];
	lumen::Texture2D depth_texes[2];
	lumen::BufferOld ddgi_output_buffer;

	struct {
		lumen::Texture2D radiance_tex;
		lumen::Texture2D dir_depth_tex;
	} rt;

	struct {
		lumen::Texture2D tex;
	} output;

	float hysteresis = 0.98f;
	int rays_per_probe = 256;
	float depth_sharpness = 50.0f;
	float normal_bias = 0.1f;
	float view_bias = 0.1f;
	float backface_ratio = 0.1f;
	float probe_distance = 0.5f;
	float min_frontface_dist = 0.1f;
	float max_distance;
	glm::ivec3 probe_counts;
	glm::vec3 probe_start_position;

	PCDDGI pc_ray{};
	VkSampler bilinear_sampler;
	VkSampler nearest_sampler;
	bool first_frame = true;
	uint32_t frame_idx = 0;
	uint total_frame_idx = 0;

	DDGIConfig* config;
};
