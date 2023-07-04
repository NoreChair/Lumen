#include "../../../commons.h"

NAMESPACE_BEGIN(RestirPT)

struct PCReSTIRPT {
	vec3 sky_col;
	uint frame_num;
	uint size_x;
	uint size_y;
	int num_lights;
	uint time;
	int max_depth;
	float total_light_area;
	int light_triangle_count;
	uint dir_light_idx;
	uint random_num;
	uint total_frame_num;
	uint enable_accumulation;
	float scene_extent;
	uint num_spatial_samples;
	uint direct_lighting;
	uint enable_rr;
	uint enable_spatial_reuse;
	uint show_reconnection_radiance;
	float spatial_radius;
	float min_vertex_distance_ratio;
	uint path_length;
	uint buffer_idx;
};


struct GBuffer {
	// vec3 pos;
	// vec3 n_g;
	// vec3 n_s;
	// vec2 uv;
	// uint material_idx;
	vec2 barycentrics;
	uvec2 primitive_instance_id;
};

struct GrisData {
	vec3 rc_wi;
	uint init_seed;
	// Layout for the path flags
	// | 5b postfix_length| 5b prefix_length |1b nee_visible |1b side|
	vec3 rc_postfix_L;
	uint path_flags;
	vec3 rc_Li;
	float rc_g;
	vec3 reservoir_contribution;
	uint rc_seed;
	vec2 rc_barycentrics;
	uvec2 rc_primitive_instance_id;
};

struct Reservoir {
	GrisData data;
	uint M;
	float W;
	float w_sum;
};

struct ReconnectionData {
	vec3 reservoir_contribution;
	float jacobian;
	vec3 pad;
	float target_pdf_in_neighbor;
};



struct GrisHitPayload {
	vec2 attribs;
	uint instance_idx;
	uint triangle_idx;
};

NAMESPACE_END()