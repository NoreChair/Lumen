#pragma once
#include "Integrator.h"
#include "shaders/integrators/sppm/sppm_commons.h"
class SPPM : public Integrator {
   public:
	SPPM(lumen::LumenInstance* scene, LumenScene* lumen_scene, const vk::BVH& tlas)
		: Integrator(scene, lumen_scene, tlas), config(CAST_CONFIG(lumen_scene->config.get(), SPPMConfig)) {}
	virtual void init() override;
	virtual void render() override;
	virtual bool update() override;
	virtual void destroy() override;

   private:
	PCSPPM pc_ray{};
	VkDescriptorPool desc_pool{};
	VkDescriptorSetLayout desc_set_layout{};
	VkDescriptorSet desc_set;

	vk::Buffer* sppm_data_buffer;
	vk::Buffer* atomic_data_buffer;
	vk::Buffer* photon_buffer;
	vk::Buffer* residual_buffer;
	vk::Buffer* counter_buffer;
	SPPMConfig* config;
};
