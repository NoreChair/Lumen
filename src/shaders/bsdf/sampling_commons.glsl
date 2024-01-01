#ifndef SAMPLING_COMMONS_GLSL
#define SAMPLING_COMMONS_GLSL
#include "../utils.glsl"

#define SAMPLING_MODE_COS_WEIGHTED 0
#define SAMPLING_MODE_CONCENTRIC_DISK_MAPPING 1

#define SAMPLING_MODE 1

float schlick_w(float u) {
	float m = clamp(1 - u, 0, 1);
	float m2 = m * m;
	return m2 * m2 * m;
}
bool bsdf_is_delta(float alpha) { return alpha == 0.0; }

// Note: eta (relative IOR) is nu_i / nu_o
bool refract(vec3 n_s, vec3 wo, bool forward_facing, float eta, uint mode, out vec3 wi, out vec3 f, out float inv_eta) {
	wi = vec3(0);
	f = vec3(0);
	// Relative IOR (Inside / outside). If the normal direction has been changed, we take the inverse
	float cos_i = dot(n_s, wo);
	inv_eta = forward_facing ? 1.0 / eta : eta;
	const float sin2_t = inv_eta * inv_eta * (1. - cos_i * cos_i);
	if (sin2_t >= 1.) {
#if 1
		return false;
#else
		wi = reflect(-wo, n_s);
#endif
	} else {
		const float cos_t = sqrt(1 - sin2_t);
		wi = -inv_eta * wo + (inv_eta * cos_i - cos_t) * n_s;
	}
	f = mode == 1 ? vec3(inv_eta * inv_eta) : vec3(1);
	return true;
}

bool refract(vec3 n_s, vec3 wo, bool forward_facing, float eta, uint mode, out vec3 wi, out vec3 f) {
	float unused_eta;
	return refract(n_s, wo, forward_facing, eta, mode, wi, f, unused_eta);
}

float fresnel_dielectric(float cos_i, float eta, bool forward_facing) {
	cos_i = clamp(cos_i, -1.0, 1.0);
	if (!forward_facing) {
		eta = 1.0 / eta;
	}
	if (cos_i < 0) {
		eta = 1.0 / eta;
		cos_i = -cos_i;
	}

	float sin2_i = 1 - cos_i * cos_i;
	float sin2_t = sin2_i / (eta * eta);
	if (sin2_t >= 1) return 1.f;
	float cos_t = sqrt(1 - sin2_t);
	float r_parallel = (eta * cos_i - cos_t) / (eta * cos_i + cos_t);
	float r_perp = (cos_i - eta * cos_t) / (cos_i + eta * cos_t);
	return 0.5 * (r_parallel * r_parallel + r_perp * r_perp);
}

// https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/
float fresnel_conductor(float cos_i, float eta, float k) {
	float cos_sqr = cos_i * cos_i;
	float sin_sqr = max(1.0f - cos_sqr, 0.0f);
	float sin_4 = sin_sqr * sin_sqr;

	float inner_term = eta * eta - k * k - sin_sqr;
	float a_sq_p_b_sq = sqrt(max(inner_term * inner_term + 4.0f * eta * eta * k * k, 0.0f));
	float a = sqrt(max((a_sq_p_b_sq + inner_term) * 0.5f, 0.0f));

	float rs = ((a_sq_p_b_sq + cos_sqr) - (2.0f * a * cos_i)) / ((a_sq_p_b_sq + cos_sqr) + (2.0f * a * cos_i));
	float rp = ((cos_sqr * a_sq_p_b_sq + sin_4) - (2.0f * a * cos_i * sin_sqr)) /
			   ((cos_sqr * a_sq_p_b_sq + sin_4) + (2.0f * a * cos_i * sin_sqr));

	return 0.5f * (rs + rs * rp);
}

vec3 fresnel_conductor(float cos_i, vec3 eta, vec3 k) {
	return vec3(fresnel_conductor(cos_i, eta.x, k.x), fresnel_conductor(cos_i, eta.y, k.y),
				fresnel_conductor(cos_i, eta.z, k.z));
}

float fresnel_schlick(float f0, float f90, float ns) {
	// Makes sure that (1.0 - n_s) >= 0
	return f0 + (f90 - f0) * pow(max(1.0 - ns, 0), 5.0f);
}

vec3 fresnel_schlick(vec3 f0, vec3 f90, float ns) {
	// Makes sure that (1.0 - n_s) >= 0
	return f0 + (f90 - f0) * pow(max(1.0 - ns, 0), 5.0f);
}


// Cos-weighted hemisphere sampling with explicit normal
vec3 sample_hemisphere(vec2 xi, vec3 n, out float phi) {
	phi = TWO_PI * xi.x;
	float cos_theta = (2.0 * xi.y - 1.0);
#if SAMPLING_MODE == SAMPLING_MODE_CONCENTRIC_DISK_MAPPING
	vec3 T, B;
	branchless_onb(n, T, B);
	vec2 d = concentric_sample_disk(xi);
	float z = sqrt(max(0., 1. - dot(d, d)));
	return to_world(vec3(d, z), T, B, n);
#else
	return normalize(n + (vec3(sqrt(1.0 - cos_theta * cos_theta) * vec2(cos(phi), sin(phi)), cos_theta)));
#endif
}

vec3 sample_hemisphere(vec2 xi, vec3 n) {
	float unused_phi;
	return sample_hemisphere(xi, n, unused_phi);
}

// Cos-weighted hemisphere sampling in local shading frame
vec3 sample_hemisphere(vec2 xi, out float phi) {
	phi = TWO_PI * xi.x;
	float cos_theta = (2.0 * xi.y - 1.0);
#if SAMPLING_MODE == SAMPLING_MODE_CONCENTRIC_DISK_MAPPING
	vec2 d = concentric_sample_disk(xi);
	float z = sqrt(max(0., 1. - dot(d, d)));
	return vec3(d, z);
#else
	return normalize(vec3(sqrt(1.0 - cos_theta * cos_theta) * vec2(cos(phi), sin(phi)), 1 - cos_theta));
#endif
}

vec3 sample_hemisphere(vec2 xi) {
	float unused_phi;
	return sample_hemisphere(xi, unused_phi);
}

#endif