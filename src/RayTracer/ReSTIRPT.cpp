#include "Framework/VulkanStructs.h"
#include "LumenPCH.h"
#include "ReSTIRPT.h"
#include <vulkan/vulkan_core.h>
#include "imgui/imgui.h"

void ReSTIRPT::init() {
	Integrator::init();

	std::vector<glm::mat4> transformations;
	transformations.resize(lumen_scene->prim_meshes.size());
	for (auto& pm : lumen_scene->prim_meshes) {
		transformations[pm.prim_idx] = pm.world_matrix;
	}

	gris_gbuffer.create("GRIS GBuffer",
						VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
							VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, instance->width * instance->height * sizeof(GBuffer));

	gris_prev_gbuffer.create("GRIS Previous GBuffer",
							 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
							 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, instance->width * instance->height * sizeof(GBuffer));

	direct_lighting_buffer.create("GRIS Direct Lighting",
								  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
									  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
								  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
								  instance->width * instance->height * sizeof(glm::vec3));

	gris_reservoir_ping_buffer.create("GRIS Reservoirs Ping",
									  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
										  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
									  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
									  instance->width * instance->height * sizeof(Reservoir));

	gris_reservoir_pong_buffer.create("GRIS Reservoirs Pong",
									  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
										  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
									  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
									  instance->width * instance->height * sizeof(Reservoir));

	prefix_contribution_buffer.create("Prefix Contributions",
									  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
										  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
									  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
									  instance->width * instance->height * sizeof(glm::vec3));

	debug_vis_buffer.create("Debug Vis",
							VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, instance->width * instance->height * sizeof(uint32_t));

	reconnection_buffer.create(
		"Reservoir Connection",
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		instance->width * instance->height * sizeof(ReconnectionData) * (num_spatial_samples + 1));
	transformations_buffer.create("Transformations Buffer",
								  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
								  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, transformations.size() * sizeof(glm::mat4),
								  transformations.data(), true);

	SceneDesc desc;
	desc.index_addr = lumen_scene->index_buffer.get_device_address();

	desc.material_addr = lumen_scene->materials_buffer.get_device_address();
	desc.prim_info_addr = lumen_scene->prim_lookup_buffer.get_device_address();
	desc.compact_vertices_addr = lumen_scene->compact_vertices_buffer.get_device_address();
	desc.compact_vertices_addr = lumen_scene->compact_vertices_buffer.get_device_address();
	// ReSTIR PT (GRIS)
	desc.transformations_addr = transformations_buffer.get_device_address();
	desc.gris_direct_lighting_addr = direct_lighting_buffer.get_device_address();
	desc.prefix_contributions_addr = prefix_contribution_buffer.get_device_address();
	desc.debug_vis_addr = debug_vis_buffer.get_device_address();
	
	lumen_scene->scene_desc_buffer.create(
							 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
							 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(SceneDesc), &desc, true);


	lumen::TextureSettings settings;
	settings.base_extent = {instance->width, instance->height, 1};
	settings.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	settings.usage_flags = VK_IMAGE_USAGE_STORAGE_BIT;

	canonical_contributions_texture.create_empty_texture("Canonical Contributions Texture", 
														settings, VK_IMAGE_LAYOUT_GENERAL);

	pc_ray.total_light_area = 0;

	frame_num = 0;

	pc_ray.total_frame_num = 0;
	pc_ray.size_x = instance->width;
	pc_ray.size_y = instance->height;
	pc_ray.buffer_idx = 0;
	
	assert(instance->vkb.rg->settings.shader_inference == true);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, prim_info_addr, &lumen_scene->prim_lookup_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, gris_reservoir_addr, &gris_reservoir_ping_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, gris_direct_lighting_addr, &direct_lighting_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, compact_vertices_addr, &lumen_scene->compact_vertices_buffer, instance->vkb.rg);
	REGISTER_BUFFER_WITH_ADDRESS(SceneDesc, desc, debug_vis_addr, &debug_vis_buffer, instance->vkb.rg);

	path_length = config->path_length;
}

void ReSTIRPT::render() {
	lumen::CommandBuffer cmd( /*start*/ true);
	pc_ray.num_lights = (int)lumen_scene->gpu_lights.size();
	pc_ray.prev_random_num = pc_ray.general_seed;
	pc_ray.sampling_seed = rand() % UINT_MAX;
	pc_ray.seed2 = rand() % UINT_MAX;
	pc_ray.seed3 = rand() % UINT_MAX;
	pc_ray.max_depth = path_length;
	pc_ray.sky_col = config->sky_col;
	pc_ray.total_light_area = lumen_scene->total_light_area;
	pc_ray.light_triangle_count = lumen_scene->total_light_triangle_cnt;
	pc_ray.dir_light_idx = lumen_scene->dir_light_idx;
	pc_ray.enable_accumulation = enable_accumulation;
	pc_ray.num_spatial_samples = num_spatial_samples;
	pc_ray.scene_extent = glm::length(lumen_scene->m_dimensions.max - lumen_scene->m_dimensions.min);
	pc_ray.direct_lighting = direct_lighting;
	pc_ray.enable_rr = enable_rr;

	pc_ray.spatial_radius = spatial_reuse_radius;
	pc_ray.enable_spatial_reuse = enable_spatial_reuse;
	pc_ray.show_reconnection_radiance = show_reconnection_radiance;
	pc_ray.min_vertex_distance_ratio = min_vertex_distance_ratio;
	pc_ray.enable_gris = enable_gris;
	pc_ray.frame_num = frame_num;
	pc_ray.pixel_debug = pixel_debug;
	pc_ray.temporal_reuse = uint(enable_temporal_reuse);
	pc_ray.permutation_sampling = uint(enable_permutation_sampling);
	pc_ray.gris_separator = gris_separator;
	pc_ray.canonical_only = canonical_only;
	pc_ray.enable_occlusion = enable_occlusion;

	const std::initializer_list<lumen::ResourceBinding> common_bindings = {output_tex, scene_ubo_buffer,
																		   lumen_scene->scene_desc_buffer, lumen_scene->mesh_lights_buffer};

	const std::array<lumen::Buffer*, 2> reservoir_buffers = {&gris_reservoir_ping_buffer, &gris_reservoir_pong_buffer};
	const std::array<lumen::Buffer*, 2> gbuffers = {&gris_prev_gbuffer, &gris_gbuffer};

	int ping = pc_ray.total_frame_num % 2;
	int pong = ping ^ 1;

	constexpr int WRITE_OR_CURR_IDX = 1;
	constexpr int READ_OR_PREV_IDX = 0;

	// Trace rays
	instance->vkb.rg
		->add_rt("GRIS - Generate Samples",
				 {
					 .shaders = {{"src/shaders/integrators/restir/gris/gris.rgen"},
								 {"src/shaders/integrators/restir/gris/ray.rmiss"},
								 {"src/shaders/ray_shadow.rmiss"},
								 {"src/shaders/integrators/restir/gris/ray.rchit"},
								 {"src/shaders/ray.rahit"}},
					 .macros = {{"STREAMING_MODE", int(streaming_method)},
								lumen::ShaderMacro("ENABLE_ATMOSPHERE", enable_atmosphere)},
					 .dims = {instance->width, instance->height},
				 })
		.push_constants(&pc_ray)
		.zero(debug_vis_buffer)
		.bind(common_bindings)
		.bind(*reservoir_buffers[WRITE_OR_CURR_IDX])
		.bind(*gbuffers[pong])
		.bind(canonical_contributions_texture)
		.bind_texture_array(lumen_scene->scene_textures)
		.bind_tlas(instance->vkb.tlas);
	pc_ray.general_seed = rand() % UINT_MAX;
	if (enable_gris) {
		bool should_do_temporal = enable_temporal_reuse && pc_ray.total_frame_num > 0;
		// Temporal Reuse
		instance->vkb.rg
			->add_rt("GRIS - Temporal Reuse",
					 {
						 .shaders = {{"src/shaders/integrators/restir/gris/temporal_reuse.rgen"},
									 {"src/shaders/integrators/restir/gris/ray.rmiss"},
									 {"src/shaders/ray_shadow.rmiss"},
									 {"src/shaders/integrators/restir/gris/ray.rchit"},
									 {"src/shaders/ray.rahit"}},
						 .dims = {instance->width, instance->height},
					 })
			.push_constants(&pc_ray)
			.bind(common_bindings)
			.bind(*reservoir_buffers[WRITE_OR_CURR_IDX])
			.bind(*reservoir_buffers[READ_OR_PREV_IDX])
			.bind(*gbuffers[pong])
			.bind(*gbuffers[ping])
			.bind(canonical_contributions_texture)
			.bind_texture_array(lumen_scene->scene_textures)
			.bind_tlas(instance->vkb.tlas)
			.skip_execution(!should_do_temporal);
		pc_ray.seed2 = rand() % UINT_MAX;
		if (!canonical_only) {
			if (mis_method == MISMethod::TALBOT) {
				instance->vkb.rg
					->add_rt("GRIS - Spatial Reuse - Talbot",
							 {
								 .shaders = {{"src/shaders/integrators/restir/gris/spatial_reuse_talbot.rgen"},
											 {"src/shaders/integrators/restir/gris/ray.rmiss"},
											 {"src/shaders/ray_shadow.rmiss"},
											 {"src/shaders/integrators/restir/gris/ray.rchit"},
											 {"src/shaders/ray.rahit"}},
								 .dims = {instance->width, instance->height},
							 })
					.push_constants(&pc_ray)
					.bind(common_bindings)
					.bind(*reservoir_buffers[WRITE_OR_CURR_IDX])
					.bind(*reservoir_buffers[READ_OR_PREV_IDX])
					.bind(*gbuffers[pong])
					.bind(canonical_contributions_texture)
					.bind_texture_array(lumen_scene->scene_textures)
					.bind_tlas(instance->vkb.tlas);
			} else {
				// Retrace
				instance->vkb.rg
					->add_rt("GRIS - Retrace Reservoirs",
							 {
								 .shaders = {{"src/shaders/integrators/restir/gris/retrace_paths.rgen"},
											 {"src/shaders/integrators/restir/gris/ray.rmiss"},
											 {"src/shaders/ray_shadow.rmiss"},
											 {"src/shaders/integrators/restir/gris/ray.rchit"},
											 {"src/shaders/ray.rahit"}},
								 .dims = {instance->width, instance->height},
							 })
					.push_constants(&pc_ray)
					.bind(common_bindings)
					.bind(reconnection_buffer)
					.bind(*reservoir_buffers[WRITE_OR_CURR_IDX])
					.bind(*gbuffers[pong])
					.bind_texture_array(lumen_scene->scene_textures)
					.bind_tlas(instance->vkb.tlas);
				// Validate
				instance->vkb.rg
					->add_rt("GRIS - Validate Samples",
							 {
								 .shaders = {{"src/shaders/integrators/restir/gris/validate_samples.rgen"},
											 {"src/shaders/integrators/restir/gris/ray.rmiss"},
											 {"src/shaders/ray_shadow.rmiss"},
											 {"src/shaders/integrators/restir/gris/ray.rchit"},
											 {"src/shaders/ray.rahit"}},
								 .dims = {instance->width, instance->height},
							 })
					.push_constants(&pc_ray)
					.bind(common_bindings)
					.bind(reconnection_buffer)
					.bind(*reservoir_buffers[WRITE_OR_CURR_IDX])
					.bind(*gbuffers[pong])
					.bind_texture_array(lumen_scene->scene_textures)
					.bind_tlas(instance->vkb.tlas);

				// Spatial Reuse
				instance->vkb.rg
					->add_rt("GRIS - Spatial Reuse",
							 {
								 .shaders = {{"src/shaders/integrators/restir/gris/spatial_reuse.rgen"},
											 {"src/shaders/integrators/restir/gris/ray.rmiss"},
											 {"src/shaders/ray_shadow.rmiss"},
											 {"src/shaders/integrators/restir/gris/ray.rchit"},
											 {"src/shaders/ray.rahit"}},
								 .macros = {lumen::ShaderMacro("ENABLE_DEFENSIVE_PAIRWISE_MIS",
															   enable_defensive_formulation)},
								 .dims = {instance->width, instance->height},
							 })
					.push_constants(&pc_ray)
					.bind(common_bindings)
					.bind(reconnection_buffer)
					.bind(*reservoir_buffers[WRITE_OR_CURR_IDX])
					.bind(*reservoir_buffers[READ_OR_PREV_IDX])
					.bind(*gbuffers[pong])
					.bind(canonical_contributions_texture)
					.bind_texture_array(lumen_scene->scene_textures)
					.bind_tlas(instance->vkb.tlas);
			}
			if (pixel_debug || (gris_separator < 1.0f && gris_separator > 0.0f)) {
				uint32_t num_wgs = uint32_t((instance->width * instance->height + 1023) / 1024);
				instance->vkb.rg
					->add_compute("GRIS - Debug Visualiation",
								  {.shader = lumen::Shader("src/shaders/integrators/restir/gris/debug_vis.comp"),
								   .dims = {num_wgs}})
					.push_constants(&pc_ray)
					.bind({output_tex, scene_ubo_buffer, lumen_scene->scene_desc_buffer});
			}
		}
	}
	pc_ray.total_frame_num++;
}

bool ReSTIRPT::update() {
	frame_num++;
	bool updated = Integrator::update();
	if (updated) {
		frame_num = 0;
	}
	return updated;
}

void ReSTIRPT::destroy() {
	Integrator::destroy();
	std::vector<lumen::Buffer*> buffer_list = {
		&gris_gbuffer,			 &gris_reservoir_ping_buffer, &gris_reservoir_pong_buffer,
		&direct_lighting_buffer, &transformations_buffer,	  &prefix_contribution_buffer,
		&reconnection_buffer,	 &gris_prev_gbuffer,		  &debug_vis_buffer};
	for (auto b : buffer_list) {
		b->destroy();
	}
	canonical_contributions_texture.destroy();
}

bool ReSTIRPT::gui() {
	bool result = Integrator::gui();
	result |= ImGui::Checkbox("Direct lighting", &direct_lighting);
	result |= ImGui::Checkbox("Enable atmosphere", &enable_atmosphere);
	result |= ImGui::Checkbox("Enable Russian roulette", &enable_rr);
	result |= ImGui::Checkbox("Enable GRIS", &enable_gris);
	result |= ImGui::SliderInt("Path length", (int*)&path_length, 0, 12);
	result |= ImGui::Checkbox("Enable canonical-only mode", &canonical_only);
	if (!enable_gris) {
		return result;
	}
	int curr_streaming_method = static_cast<int>(streaming_method);
	std::array<const char*, 2> streaming_methods = {
		"Individual contributions",
		"Split at reconnection",
	};
	if (ImGui::Combo("Streaming method", &curr_streaming_method, streaming_methods.data(),
					 int(streaming_methods.size()))) {
		result = true;
		streaming_method = static_cast<StreamingMethod>(curr_streaming_method);
	}
	if (canonical_only) {
		return result;
	}
	result |= ImGui::SliderFloat("GRIS / Default", &gris_separator, 0.0f, 1.0f);
	result |= ImGui::Checkbox("Enable occlusion", &enable_occlusion);
	result |= ImGui::Checkbox("Enable accumulation", &enable_accumulation);
	result |= ImGui::Checkbox("Debug pixels", &pixel_debug);
	result |= ImGui::Checkbox("Enable defensive formulation", &enable_defensive_formulation);
	result |= ImGui::Checkbox("Enable permutation sampling", &enable_permutation_sampling);
	result |= ImGui::Checkbox("Enable spatial reuse", &enable_spatial_reuse);
	result |= ImGui::Checkbox("Show reconnection radiance", &show_reconnection_radiance);
	std::array<const char*, 2> mis_methods = {
		"Talbot (Reconnection only)",
		"Pairwise",
	};
	int curr_mis_method = static_cast<int>(mis_method);
	if (ImGui::Combo("MIS method", &curr_mis_method, mis_methods.data(), int(mis_methods.size()))) {
		result = true;
		mis_method = static_cast<MISMethod>(curr_mis_method);
	}
	result |= ImGui::Checkbox("Enable temporal reuse", &enable_temporal_reuse);
	bool spatial_samples_changed = ImGui::SliderInt("Num spatial samples", (int*)&num_spatial_samples, 0, 12);
	result |= spatial_samples_changed;
	result |= ImGui::SliderFloat("Spatial radius", &spatial_reuse_radius, 0.0f, 128.0f);
	result |= ImGui::SliderFloat("Min reconnection distance ratio", &min_vertex_distance_ratio, 0.0f, 1.0f);

	if (spatial_samples_changed && num_spatial_samples > 0) {
		vkDeviceWaitIdle(VulkanContext::device);
		reconnection_buffer.destroy();
		reconnection_buffer.create(
			"Reservoir Connection",
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			instance->width * instance->height * sizeof(ReconnectionData) * (num_spatial_samples + 1));
	}
	return result;
}
