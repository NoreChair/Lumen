#ifndef PRINCIPLED_GLSL
#define PRINCIPLED_GLSL
#include "microfacet_commons.glsl"
#include "diffuse.glsl"

// Resources and references:
// Disney BSDF 2012:
// https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf Disney BSDF
// 2015: https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf
// https://github.dev/schuttejoe/Selas/blob/dev/Source/Core/Shading/Disney.cpp
// https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/
// https://schuttejoe.github.io/post/disneybsdf/
// https://github.dev/mitsuba-renderer/mitsuba3
// Nvidia Falcor (StandardBSDF)

void initialize_sampling_pdfs(const Material mat, out float p_spec, out float p_diff, out float p_clearcloat,
							  out float p_spec_trans) {
	float metallic_brdf = mat.metallic * (1.0 - mat.spec_trans);
	float specular_brdf = mat.spec_trans;
	float dielectric_brdf = (1.0 - mat.spec_trans) * (1.0 - mat.metallic);

	float specular_weight = metallic_brdf + dielectric_brdf;
	float transmission_weight = specular_brdf;
	float diffuse_weight = dielectric_brdf;
	float clearcoat_weight = clamp(mat.clearcoat, 0.0, 1.0);

	float norm = 1.0 / (specular_weight + transmission_weight + diffuse_weight + clearcoat_weight);

	p_spec = specular_weight * norm;
	p_spec_trans = transmission_weight * norm;
	p_diff = diffuse_weight * norm;
	p_clearcloat = clearcoat_weight * norm;
}

vec3 eval_disney_diffuse(Material mat, vec3 wo, vec3 wi) {
	vec3 h = normalize(wi + wo);

	float f_wi, f_wo;
	float disney_f = disney_fresnel(wi, wo, mat.roughness, f_wi, f_wo);
	float alpha = mat.roughness * mat.roughness;

	float ss = 0;

	// Retro-reflection
	float rr = 2.0 * alpha * wi.z * wi.z;
	float f_retro = rr * (f_wi + f_wo * f_wi * f_wo * (rr - 1.0));

	float f_diff = (1.0 - 0.5 * f_wi) * (1.0 - 0.5 * f_wo);
	if (mat.flatness > 0.0) {
		float fss90 = 0.5 * rr;
		float h_dot_wi_sqr = dot(h, wi);
		h_dot_wi_sqr *= h_dot_wi_sqr;

		float f_ss = mix(1.0, fss90, f_wi) * mix(1.0, fss90, f_wo);
		ss = 1.25 * (f_ss * (1.0 / (wi.z + wo.z) - 0.5) + 0.5);
	}
	float ss_approx_and_diff = mix(f_diff + f_retro, ss, mat.flatness);
	return mat.albedo * ss_approx_and_diff * INV_PI;
}

// In addition to diffuse, this includes retro reflection, fake subsurface and sheen
vec3 sample_disney_diffuse(Material mat, vec3 wo, out vec3 wi, out float pdf_w, out float cos_theta, vec2 xi) {
	wi = sample_hemisphere(xi);
	cos_theta = wi.z;
	pdf_w = cos_theta * INV_PI;
	if (min(wi.z, wo.z) <= 0.0) {
		return vec3(0);
	}
	return eval_disney_diffuse(mat, wo, wi);
}

vec3 sample_principled_brdf(const Material mat, const vec3 wo, inout vec3 wi, inout float pdf_w, inout float cos_theta,
							const vec2 xi, float eta) {
	bool has_reflection = bsdf_has_property(mat.bsdf_props, BSDF_FLAG_REFLECTION);
	if ((!has_reflection)) {
		return vec3(0);
	}
	float D;
	float alpha = mat.roughness * mat.roughness;

	if (bsdf_is_delta(alpha)) {
		wi = vec3(-wo.x, -wo.y, wo.z);
		pdf_w = 1.0;
		cos_theta = wi.z;
		vec3 F = disney_fresnel(mat, wo, vec3(0, 0, 1), wi, eta);
		return F / abs(cos_theta);
	}
	vec3 h = sample_ggx_vndf_isotropic(vec2(alpha), wo, xi, pdf_w, D);

	wi = reflect(-wo, h);
	// Make sure the reflection lies in the same hemisphere
	if (wo.z * wi.z < 0) {
		return vec3(0);
	}
	vec3 F = disney_fresnel(mat, wo, h, wi, eta);
	pdf_w /= (4.0 * dot(wo, h));
	cos_theta = wi.z;
	return 0.25 * D * F * G_GGX_correlated_isotropic(alpha, wo, wi) / (wi.z * wo.z);
}

float calc_clearcoat_factor(Material mat, vec3 wo, vec3 wi, vec3 h, out float D) {
	const float alpha_2 = 0.25 * 0.25;
	D = D_GGX_isotropic(mix(0.1, 0.001, mat.clearcoat_gloss), h.z);
	float F = fresnel_schlick(0.04, 1.0, dot(wi, h));
	// Use the separable variant
	float G = G1_GGX_isotropic(alpha_2, wo.z) * G1_GGX_isotropic(alpha_2, wi.z);
	return 0.25 * mat.clearcoat * D * F * G;
}

// For clearcoat: We sample the GGX distribution directly and use fixed roughness values (0.25)
vec3 sample_clearcoat(const Material mat, const vec3 wo, inout vec3 wi, inout float pdf_w, inout float cos_theta,
					  const vec2 xi) {
	const float alpha_2 = 0.25 * 0.25;

	float cos_t = sqrt(max(0, (1.0 - pow(alpha_2, 1.0f - xi.x)) / (1.0 - alpha_2)));
	float sin_t = sqrt(max(0, 1.0 - cos_theta * cos_theta));
	float phi = TWO_PI * xi.y;

	vec3 h = vec3(sin_t * cos(phi), sin_t * sin(phi), cos_t);

	if (dot(h, wo) < 0.0) {
		h *= -1.0;
	}

	wi = reflect(-wo, h);
	if (dot(wi, wo) < 0.0) {
		return vec3(0);
	}

	float D;
	float f_clearcoat = calc_clearcoat_factor(mat, wo, wi, h, D);
	pdf_w = D / (4.0 * dot(wo, h));
	cos_theta = wi.z;

	return vec3(f_clearcoat);
}

vec3 eval_clearcoat(Material mat, vec3 wo, vec3 wi, inout float pdf_w, inout float pdf_rev_w) {
	if (wo.z * wi.z < 0) {
		return vec3(0);
	}
	if (wo.z == 0 || wi.z == 0) {
		return vec3(0);
	}
	const float alpha_2 = 0.25 * 0.25;
	vec3 h = normalize(wo + wo);
	float D;
	float f_clearcoat = calc_clearcoat_factor(mat, wo, wi, h, D);
	pdf_w += D / (4.0 * dot(wo, h));
	// Since dot(wo, h) == dot(wi, h) here
	pdf_rev_w += pdf_w;
	return vec3(f_clearcoat);
}
float eval_clearcoat_pdf(Material mat, vec3 wo, vec3 wi) {
	if (wo.z * wi.z < 0) {
		return 0.0;
	}
	if (wo.z == 0 || wi.z == 0) {
		return 0.0;
	}
	vec3 h = normalize(wo + wo);
	float D = D_GGX_isotropic(mix(0.1, 0.001, mat.clearcoat_gloss), h.z);
	return D / (4.0 * dot(wo, h));
}

vec3 eval_principled_brdf(Material mat, vec3 wo, vec3 wi, inout float pdf_w, inout float pdf_rev_w, bool forward_facing,
						  uint mode, bool eval_reverse_pdf) {
	float alpha = mat.roughness * mat.roughness;
	if (bsdf_is_delta(alpha)) {
		return vec3(0);
	}
	if (wo.z * wi.z < 0) {
		return vec3(0);
	}
	if (wo.z == 0 || wi.z == 0) {
		return vec3(0);
	}
	vec3 h = normalize(wo + wo);
	float jacobian = 1.0 / (4.0 * dot(wo, h));
	float D;
	pdf_w += eval_vndf_pdf_isotropic(alpha, wo, h, D) / jacobian;
	float eta = forward_facing ? mat.ior : 1.0 / mat.ior;
	if (eval_reverse_pdf) {
		pdf_rev_w += eval_vndf_pdf_isotropic(alpha, wi, h) / jacobian;
	}
	vec3 F = disney_fresnel(mat, wo, h, wi, eta);
	return 0.25 * D * F * G_GGX_correlated_isotropic(alpha, wo, wi) / (wi.z * wo.z);
}

float eval_principled_brdf_pdf(Material mat, vec3 wo, vec3 wi) {
	float alpha = mat.roughness * mat.roughness;

	if (bsdf_is_delta(alpha)) {
		return 0.0;
	}

	if (wo.z * wi.z < 0) {
		return 0.0;
	}
	if (wo.z == 0 || wi.z == 0) {
		return 0.0;
	}

	vec3 h = normalize(wo + wo);
	// Make sure h is oriented towards the normal
	h *= float(sign(h.z));

	return eval_vndf_pdf_isotropic(alpha, wo, h) / (4.0 * dot(wo, h));
}

vec3 sample_principled(const Material mat, const vec3 wo, out vec3 wi, const uint mode, const bool forward_facing,
					   out float pdf_w, out float cos_theta, vec2 xi) {
	wi = vec3(0);
	pdf_w = 0.0;
	cos_theta = 0.0;

	if (wo.z <= 0.0) {
		return vec3(0);
	}
	float p_spec, p_diff, p_clearcoat, p_spec_trans;
	initialize_sampling_pdfs(mat, p_spec, p_diff, p_clearcoat, p_spec_trans);

	float alpha = mat.roughness * mat.roughness;

	float eta = forward_facing ? mat.ior : 1.0 / mat.ior;

	vec3 f = vec3(0);

	float p_lobe = 0.0;
	if (xi.x < p_spec) {
		xi.x /= p_spec;
		f = sample_principled_brdf(mat, wo, wi, pdf_w, cos_theta, xi, eta);
		p_lobe = p_spec;
	} else if (xi.x > p_spec && xi.x <= (p_spec + p_clearcoat)) {
		f = sample_clearcoat(mat, wo, wi, pdf_w, cos_theta, xi);
		xi.x /= (p_spec + p_clearcoat);
		p_lobe = p_clearcoat;
	} else if (xi.x > (p_spec + p_clearcoat) && xi.x <= (p_spec + p_clearcoat + p_diff)) {
		xi.x /= (p_spec + p_clearcoat + p_diff);
		f = sample_disney_diffuse(mat, wo, wi, pdf_w, cos_theta, xi);
		p_lobe = p_diff;
	} else if (p_spec_trans >= 0.0) {
	}
	pdf_w *= p_lobe;
	return f;
}

vec3 eval_principled(Material mat, vec3 wo, vec3 wi, out float pdf_w, out float pdf_rev_w, bool forward_facing,
					 uint mode, bool eval_reverse_pdf) {
	pdf_w = 0.0;
	pdf_rev_w = 0.0;
	float alpha = mat.roughness * mat.roughness;

	float p_spec, p_diff, p_clearcoat, p_spec_trans;
	initialize_sampling_pdfs(mat, p_spec, p_diff, p_clearcoat, p_spec_trans);

	vec3 f = vec3(0);
	if (p_spec > 0) {
		f += eval_principled_brdf(mat, wo, wi, pdf_w, pdf_rev_w, forward_facing, mode, eval_reverse_pdf);
	}
	bool upper = min(wi.z, wo.z) > 0;
	if (upper) {
		if (p_diff > 0) {
			pdf_w += wi.z * INV_PI;
			pdf_rev_w += pdf_w;
			f += eval_disney_diffuse(mat, wo, wi);
		}
		if (p_clearcoat > 0) {
			f += eval_clearcoat(mat, wo, wi, pdf_w, pdf_rev_w);
		}
	}
	return f;
}

float eval_principled_pdf(Material mat, vec3 wo, vec3 wi, bool forward_facing) {
	float pdf = 0.0;
	float p_spec, p_diff, p_clearcoat, p_spec_trans;
	initialize_sampling_pdfs(mat, p_spec, p_diff, p_clearcoat, p_spec_trans);
	if (p_spec > 0) {
		pdf += eval_principled_brdf_pdf(mat, wo, wi);
	}
	bool upper = min(wi.z, wo.z) > 0;
	if (upper) {
		if (p_diff > 0) {
			pdf += eval_disney_diffuse_pdf(wo, wi);
		}
		if (p_clearcoat > 0) {
			pdf += eval_clearcoat_pdf(mat, wo, wi);
		}
	}
	return pdf;
}

#endif