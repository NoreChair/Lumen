#include "../LumenPCH.h"

#include "PersistentResourceManager.h"
#include <vulkan/vulkan_core.h>
#include <string>
#include <unordered_map>
#include "VulkanContext.h"
#include "VkUtils.h"

static bool operator==(const VkSamplerCreateInfo& lhs, const VkSamplerCreateInfo& rhs) { return lhs == rhs; }
namespace prs {
std::unordered_map<VkSamplerCreateInfo, VkSampler, vk::SamplerHash> _sampler_cache;

VkSampler get(const VkSamplerCreateInfo& sampler_create_info) {
	auto it = _sampler_cache.find(sampler_create_info);
	if (it != _sampler_cache.end()) {
		return it->second;
	}
	auto result = _sampler_cache.insert({sampler_create_info, VkSampler()});
	if (!result.second) {
		return VK_NULL_HANDLE;
	}
	vk::check(vkCreateSampler(vk::context().device, &sampler_create_info, nullptr, &result.first->second),
			  "Could not create a sampler");
	return result.first->second;
}
}  // namespace prs