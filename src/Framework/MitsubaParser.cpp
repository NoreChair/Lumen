#include "../LumenPCH.h"
#include "MitsubaParser.h"
#include <mitsuba_parser/tinyparser-mitsuba.h>

void MitsubaParser::parse(const std::string& path) {
	SceneLoader loader;
	auto scene = loader.loadFromFile(path);

	for (const auto& child : scene.anonymousChildren()) {
		Object* obj = child.get();
		switch (obj->type()) {
			case OT_INTEGRATOR: {
				integrator.type = obj->pluginType();
				for (const auto& prop : obj->properties()) {
					if (prop.first == "max_depth") {
						integrator.depth = (int)prop.second.getInteger();
					}
					if (prop.first == "enable_vm") {
						integrator.enable_vm = prop.second.getBool();
					}
				}

			} break;
			case OT_SENSOR: {
				for (const auto& prop : obj->properties()) {
					if (prop.first == "fov") {
						camera.fov = prop.second.getNumber();
					} else if (prop.first == "to_world") {
						float* p_dst = (float*)glm::value_ptr(camera.cam_matrix);
						const auto& src = prop.second.getTransform();
						for (int i = 0; i < 4; i++) {
							for (int j = 0; j < 4; j++) {
								p_dst[4 * j + i] = src.matrix[4 * j + i];
							}
						}
					}
				}

			} break;
			case OT_BSDF: {
				MitsubaBSDF bsdf;
				bsdf.name = obj->id();
				while ((obj->pluginType() == "twosided" || obj->pluginType() == "mask") &&
					   obj->anonymousChildren().size()) {
					obj = obj->anonymousChildren()[0].get();
				}
				bsdf.type = obj->pluginType();
				for (const auto& prop : obj->properties()) {
					// Get reflectance
					if (prop.second.type() == PT_COLOR) {
						if (prop.first.find("reflectance") == std::string::npos &&
							prop.first.find("specularReflectance") == std::string::npos) {
							continue;
						}
						// Assume RGB for the moment
						bsdf.albedo =
							glm::vec3({prop.second.getColor().r, prop.second.getColor().g, prop.second.getColor().b});
					}
					if (prop.first == "alpha") {
						bsdf.roughness = std::sqrt(prop.second.getNumber());
					}
					if (prop.first == "int_ior") {
						bsdf.ior = prop.second.getNumber();
					}
				}
				for (const auto& named_child : obj->namedChildren()) {
					if (named_child.second.get()->type() == OT_TEXTURE) {
						for (const auto& texture_prop : named_child.second.get()->properties()) {
							if (texture_prop.first == "filename") {
								bsdf.texture = texture_prop.second.getString();
							}
						}
					}
				}
				for (const auto& prop : obj->properties()) {
					// Get reflectance
					if (prop.second.type() == PT_COLOR) {
						if (prop.first.find("reflectance") == std::string::npos &&
							prop.first.find("specularReflectance") == std::string::npos) {
							continue;
						}
						// Assume RGB for the moment
						bsdf.albedo =
							glm::vec3({prop.second.getColor().r, prop.second.getColor().g, prop.second.getColor().b});
					}
					if (prop.first == "alpha") {
						bsdf.roughness = std::sqrt(prop.second.getNumber());
					}
				}
				bsdfs.push_back(bsdf);
			} break;
			case OT_SHAPE: {
				MitsubaMesh mesh;
				for (const auto& prop : obj->properties()) {
					if (prop.first == "filename") {
						mesh.file = prop.second.getString();

					} else if (prop.first == "to_world") {
						float* p_dst = (float*)glm::value_ptr(mesh.transform);
						const auto& src = prop.second.getTransform();
						for (int i = 0; i < 4; i++) {
							for (int j = 0; j < 4; j++) {
								p_dst[4 * i + j] = src.matrix[4 * j + i];
							}
						}
					}
				}

				// Assume refs to BSDFs
				for (const auto& mesh_child : obj->anonymousChildren()) {
					auto ref = mesh_child.get()->id();
					for (int i = 0; i < bsdfs.size(); i++) {
						if (bsdfs[i].name == ref) {
							mesh.bsdf_idx = i;
						}
					}
					mesh.bsdf_ref = ref;
				}
				meshes.push_back(mesh);
			} break;
			case OT_EMITTER: {
				MitsubaLight light;
				if (obj->pluginType() == "sunsky") {
					light.type = "directional";
				}
				light.to = glm::vec3(0);
				for (const auto& prop : obj->properties()) {
					if (prop.first == "sun_direction") {
						auto dir = prop.second.getVector();
						light.from = glm::vec3({dir.x, dir.y, dir.z});
					} else if (prop.first == "sun_color") {
						auto col = prop.second.getVector();
						light.L = glm::vec3({col.x, col.y, col.z});
					} else if (prop.first == "sun_scale") {
						light.L *= prop.second.getNumber();
					} else if (prop.first == "sky_color") {
						auto col = prop.second.getVector();
						integrator.sky_col = glm::vec3({col.x, col.y, col.z});
					}
				}
				lights.push_back(light);
			} break;
			default:
				break;
		}
	}
}
