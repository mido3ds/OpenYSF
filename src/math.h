#pragma once

#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/gtc/type_ptr.hpp> // glm::value_ptr
#include <glm/gtx/transform.hpp>
#include <glm/gtx/norm.hpp> // glm::length2

#include <mu/utils.h>

// YS angle format, degrees(0->360): YS(0x0000->0xFFFF), extracted from ys blender scripts
constexpr float YS_MAX      = 0xFFFF;
constexpr float RADIANS_MAX = 6.283185307179586f;
constexpr float DEGREES_MAX = 360.0f;

// euclidean modulo (https://stackoverflow.com/a/52529440)
// always positive
constexpr
int mod(int a, int b) {
	const auto r = a % b;
	if (r < 0) {
		// return r + (b < 0) ? -b : b; // avoid this form: it is UB when b == INT_MIN
		return (b < 0) ? r - b : r + b;
	}
	return r;
}

static_assert(mod(+7, +3) == 1);
static_assert(mod(+7, -3) == 1);
static_assert(mod(-7, +3) == 2);
static_assert(mod(-7, -3) == 2);
static_assert(mod(0-1, 5) == 4);

// region R = { (x, y, z) | min.x<=x<=max.x, min.y<=y<=max.y, min.z<=z<=max.z }
struct AABB {
	glm::vec3 min, max;
};

// no intersection if separated along an axis
// overlapping on all axes means AABBs are intersecting
bool aabbs_intersect(const AABB& a, const AABB& b) {
	return glm::all(glm::greaterThanEqual(a.max, b.min)) && glm::all(glm::greaterThanEqual(b.max, a.min));
}

void test_aabbs_intersection() {
	mu_test_suite("test_aabbs_intersection");

	{
		const AABB x {
			.min={0.0f, 0.0f, 2.0f},
			.max={1.0f, 1.0f, 5.0f},
		};
		const AABB y {
			.min={0.5f, 0.5f, 3.0f},
			.max={3.0f, 3.0f, 4.0f},
		};
		mu_test(aabbs_intersect(x, y));
	}

	{
		const AABB x {
			.min={0.0f, 0.0f, 2.0f},
			.max={1.0f, 1.0f, 5.0f},
		};
		const AABB y {
			.min={0.5f, 0.5f, -3.0f},
			.max={3.0f, 3.0f, -4.0f},
		};
		mu_test(aabbs_intersect(x, y) == false);
	}

	{
		const AABB x {
			.min={0.0f, 0.0f, 2.0f},
			.max={1.0f, 1.0f, 5.0f},
		};
		const AABB y {
			.min={0.5f, 0.5f, -3.0f},
			.max={3.0f, 3.0f, 4.0f},
		};
		mu_test(aabbs_intersect(x, y));
	}
}

// margin of error
constexpr double EPS = 0.001;

bool
almost_equal(const glm::vec3& a, const glm::vec3& b) {
	const auto c = a - b;
	return ::fabs(c.x) < EPS && ::fabs(c.y) < EPS && ::fabs(c.z) < EPS;
}

bool
almost_equal(const glm::vec2& a, const glm::vec2& b) {
	const auto c = a - b;
	return ::fabs(c.x) < EPS && ::fabs(c.y) < EPS;
}

bool
almost_equal(const glm::vec4& a, const glm::vec4& b) {
	const auto c = a - b;
	return ::fabs(c.x) < EPS && ::fabs(c.y) < EPS && ::fabs(c.z) < EPS && ::fabs(c.w) < EPS;
}

bool
almost_equal(const float& a, const float& b) {
	return ::fabs(a - b) < EPS;
}

// http://paulbourke.net/geometry/pointlineplane/
// http://paulbourke.net/geometry/pointlineplane/lineline.c
bool lines_intersect(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec3& p4) {
	const glm::vec3 p43 = p4 - p3;
	if (almost_equal(p43, {0,0,0})) {
		return false;
	}

	const glm::vec3 p21 = p2 - p1;
	if (almost_equal(p21, {0,0,0})) {
		return false;
	}

	const glm::vec3 p13 = p1 - p3;
	const double d1343 = glm::dot(p13, p43);
	const double d4321 = glm::dot(p43, p21);
	const double d1321 = glm::dot(p13, p21);
	const double d4343 = glm::dot(p43, p43);
	const double d2121 = glm::dot(p21, p21);

	const double denom = d2121 * d4343 - d4321 * d4321;
	if (almost_equal(denom, 0)) {
		return false;
	}
	const double numer = d1343 * d4321 - d1321 * d4343;

	const double mua = numer / denom;
	const double mub = (d1343 + d4321 * (mua)) / d4343;

	return mua >= 0 && mua <= 1 && mub >= 0 && mub <= 1;
}

bool lines2d_intersect(const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, const glm::vec2& p4) {
	const glm::vec2 p43 = p4 - p3;
	if (almost_equal(p43, {0,0})) {
		return false;
	}

	const glm::vec2 p21 = p2 - p1;
	if (almost_equal(p21, {0,0})) {
		return false;
	}

	const glm::vec2 p13 = p1 - p3;
	const double d1343 = glm::dot(p13, p43);
	const double d4321 = glm::dot(p43, p21);
	const double d1321 = glm::dot(p13, p21);
	const double d4343 = glm::dot(p43, p43);
	const double d2121 = glm::dot(p21, p21);

	const double denom = d2121 * d4343 - d4321 * d4321;
	if (almost_equal(denom, 0)) {
		return false;
	}
	const double numer = d1343 * d4321 - d1321 * d4343;

	const double mua = numer / denom;
	const double mub = (d1343 + d4321 * (mua)) / d4343;

	return mua >= 0 && mua <= 1 && mub >= 0 && mub <= 1;
}

mu::Vec<uint32_t>
polygons_to_triangles(const mu::Vec<glm::vec3>& vertices, const mu::Vec<uint32_t>& orig_indices, const glm::vec3& center) {
	// dbl_indices -> orig_indices -> vertices
	// vertex = vertices[orig_indices[dbl_indices[i]]]
	// indices to indices to vertices
	// sort dbl_indices from farthest from center to nearst
	mu::Vec<size_t> dbl_indices(mu::memory::tmp());
	for (size_t i = 0; i < orig_indices.size(); i++) {
		dbl_indices.push_back(i);
	}
	mu::Vec<double> dist_from_center(mu::memory::tmp());
	for (const auto& v : vertices) {
		dist_from_center.push_back(glm::distance(center, v));
	}
	std::sort(dbl_indices.begin(), dbl_indices.end(), [&](size_t a, size_t b) {
		return dist_from_center[orig_indices[a]] > dist_from_center[orig_indices[b]];
	});

	mu::Vec<uint32_t> out{};
	auto indices = orig_indices;

	// limit no of iterations to avoid inf loop
	size_t k = indices.size() + 1;
	while (k > 0 && indices.size() > 3) {
		k--;

		for (size_t j = 0; j < dbl_indices.size(); j++) {
			auto i = dbl_indices[j];

			// indices
			const uint32_t iv0 = indices[mod(i-1, indices.size())];
			const uint32_t iv2 = indices[mod(i+1, indices.size())];

			bool is_ear = true;

			// segment: (v0, v2) must not intersect with any other edge in polygon
			// for edge in edges:
			//   if not share_vertex(segment, edge):
			//     if intersects(segment, edge): return false
			for (size_t j = 0; j < indices.size(); j++) {
				// edge
				const uint32_t jv0 = indices[j];
				const uint32_t jv1 = indices[mod(j+1, indices.size())];

				// don't test the edge if it shares a vertex with it
				if ((jv0 != iv0 && jv0 != iv2) && (jv1 != iv0 && jv1 != iv2)) {
					if (lines_intersect(vertices[jv0], vertices[jv1], vertices[iv0], vertices[iv2])) {
						is_ear = false;
						break;
					}
				}
			}

			if (is_ear) {
				out.push_back(indices[mod(i-1, indices.size())]);
				out.push_back(indices[i]);
				out.push_back(indices[mod(i+1, indices.size())]);

				indices.erase(indices.begin()+i);
				dbl_indices.erase(dbl_indices.begin()+j);

				for (auto& id : dbl_indices) {
					if (id > i) {
						id--;
					}
				}

				// exit the loop so that we check again the first vertex of the loop, maybe it became now a convex one
				break;
			}
		}
	}

	if (indices.size() != 3) {
		mu::log_error("failed to tesselate");
	}
	std::copy(indices.begin(), indices.end(), std::back_inserter(out));
	return out;
}

mu::Vec<uint32_t>
polygons2d_to_triangles(const mu::Vec<glm::vec2>& vertices, mu::memory::Allocator* allocator = mu::memory::default_allocator()) {
	glm::vec2 center {};
	for (const auto& vertex : vertices) {
		center += vertex;
	}
	center /= vertices.size();

	mu::Vec<uint32_t> orig_indices(mu::memory::tmp());
	orig_indices.reserve(vertices.size());
	for (int i = 0; i < vertices.size(); i++) {
		orig_indices.push_back(i);
	}

	// dbl_indices -> orig_indices -> vertices
	// vertex = vertices[orig_indices[dbl_indices[i]]]
	// indices to indices to vertices
	// sort dbl_indices from farthest from center to nearst
	mu::Vec<size_t> dbl_indices(mu::memory::tmp());
	for (size_t i = 0; i < orig_indices.size(); i++) {
		dbl_indices.push_back(i);
	}
	mu::Vec<double> dist_from_center(mu::memory::tmp());
	for (const auto& v : vertices) {
		dist_from_center.push_back(glm::distance(center, v));
	}
	std::sort(dbl_indices.begin(), dbl_indices.end(), [&](size_t a, size_t b) {
		return dist_from_center[orig_indices[a]] > dist_from_center[orig_indices[b]];
	});

	mu::Vec<uint32_t> out(allocator);
	auto indices = orig_indices;

	// limit no of iterations to avoid inf loop
	size_t k = indices.size() + 1;
	while (k > 0 && indices.size() > 3) {
		k--;

		for (size_t j = 0; j < dbl_indices.size(); j++) {
			auto i = dbl_indices[j];

			// indices
			const uint32_t iv0 = indices[mod(i-1, indices.size())];
			const uint32_t iv2 = indices[mod(i+1, indices.size())];

			bool is_ear = true;

			// segment: (v0, v2) must not intersect with any other edge in polygon
			// for edge in edges:
			//   if not share_vertex(segment, edge):
			//     if intersects(segment, edge): return false
			for (size_t j = 0; j < indices.size(); j++) {
				// edge
				const uint32_t jv0 = indices[j];
				const uint32_t jv1 = indices[mod(j+1, indices.size())];

				// don't test the edge if it shares a vertex with it
				if ((jv0 != iv0 && jv0 != iv2) && (jv1 != iv0 && jv1 != iv2)) {
					if (lines2d_intersect(vertices[jv0], vertices[jv1], vertices[iv0], vertices[iv2])) {
						is_ear = false;
						break;
					}
				}
			}

			if (is_ear) {
				out.push_back(indices[mod(i-1, indices.size())]);
				out.push_back(indices[i]);
				out.push_back(indices[mod(i+1, indices.size())]);

				indices.erase(indices.begin()+i);
				dbl_indices.erase(dbl_indices.begin()+j);

				for (auto& id : dbl_indices) {
					if (id > i) {
						id--;
					}
				}

				// exit the loop so that we check again the first vertex of the loop, maybe it became now a convex one
				break;
			}
		}
	}

	if (indices.size() != 3) {
		mu::log_error("failed to tesselate");
	}
	std::copy(indices.begin(), indices.end(), std::back_inserter(out));
	return out;
}

void test_polygons_to_triangles() {
	mu_test_suite("test_polygons_to_triangles");

	{
		const mu::Vec<glm::vec3> vertices {
			{2,4,0},
			{2,2,0},
			{3,2,0},
			{4,3,0},
			{4,4,0},
		};
		const auto indices = mu::Vec<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {3, 3, 0};
		mu_test(polygons_to_triangles(vertices, indices, center) == mu::Vec<uint32_t>({4, 0, 1, 4, 1, 2, 2, 3, 4}));
	}

	{
		// shouldn't intersect
		glm::vec3 a {2,4,0};
		glm::vec3 b {4,4,0};
		glm::vec3 c {4,3,0};
		glm::vec3 d {3,2,0};

		mu_test(lines_intersect(a, b, c, d) == false);
	}

	{
		// shouldn't intersect
		glm::vec3 a {1.311345,  0.627778,  1.068002};
		glm::vec3 b {1.311345, -0.000053, -1.472697};
		glm::vec3 c {1.311345, -0.000053,  1.717336};
		glm::vec3 d {1.311345,  0.512254,  2.414495};

		mu_test(lines_intersect(a, b, c, d) == false);
	}

	{
		const mu::Vec<glm::vec3> vertices{
			{4,4,0},
			{5,3,0},
			{4,2,0},
			{3,3,0},
		};
		const auto indices = mu::Vec<uint32_t>({0,1,2,3});
		const glm::vec3 center {4, 3, 0};
		mu_test(polygons_to_triangles(vertices, indices, center) == mu::Vec<uint32_t>({3, 0, 1, 1, 2, 3}));
	}

	{
		const mu::Vec<glm::vec3> vertices{
			{2,4,0},
			{2,2,0},
			{3,2,0},
			{4,3,0},
			{4,4,0},
		};
		const auto indices = mu::Vec<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {3, 3, 0};
		mu_test(polygons_to_triangles(vertices, indices, center) == mu::Vec<uint32_t>({4, 0, 1, 4, 1, 2, 2, 3, 4}));
	}

	{
		const mu::Vec<glm::vec3> vertices{
			{0.19, -0.77, 0.82},
			{0.23, -0.75, 0.68},
			{0.20, -0.75, 0.00},
			{0.32, -0.71, 0.00},
			{0.31, -0.73, 0.96},
		};
		const auto indices = mu::Vec<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {0.25, -0.742, 0.492};
		mu_test(polygons_to_triangles(vertices, indices, center) == mu::Vec<uint32_t>({2, 3, 4, 1, 2, 4, 0, 1, 4}));
	}
}

template<typename T>
T clamp(T x, T lower_limit, T upper_limit) {
	if (x > upper_limit) {
		return upper_limit;
	}
	if (x < lower_limit) {
		return lower_limit;
	}
	return x;
}

struct LocalEulerAngles {
	float roll, pitch, yaw;
	glm::vec3 up {0, -1, 0}, front {0, 0, 1};
};

glm::mat4 local_euler_angles_matrix(const LocalEulerAngles& self, glm::vec3 pos) {
	const auto& up = self.up;
	const auto& front = self.front;
	const auto right = glm::cross(up, front);
	return glm::mat4{
		-right.x, -right.y, -right.z,   0.0f,
		-up.x,     -up.y,     -up.z,    0.0f,
		+front.x,  +front.y,  +front.z, 0.0f,
		+pos.x,    +pos.y,    +pos.z,   1.0f
	};
}

void local_euler_angles_rotate(LocalEulerAngles& self, float delta_yaw, float delta_pitch, float delta_roll) {
	auto right = glm::cross(self.up, self.front);

	const glm::mat3 yaw_m = glm::rotate(delta_yaw, self.up);
	right = yaw_m * right;
	const glm::mat3 pitch_m = glm::rotate(delta_pitch, right);
	self.front = pitch_m * yaw_m * self.front;
	const glm::mat3 roll_m = glm::rotate(delta_roll, self.front);
	right = roll_m * right;
	self.up = glm::cross(self.front, right);

	self.front = glm::normalize(self.front);
	self.up = glm::normalize(self.up);

	self.yaw += delta_yaw;
	self.pitch += delta_pitch;
	self.roll += delta_roll;
}

static LocalEulerAngles
local_euler_angles_from_attitude(glm::vec3 attitude) {
	LocalEulerAngles self {};
	local_euler_angles_rotate(self, attitude.z, attitude.y, attitude.x);
	return self;
}

// line segments: [0, 1, 2, 3]
// lines:         [0, 1, 1, 2, 2, 3]
mu::Vec<glm::vec2> line_segments_to_lines(const mu::Vec<glm::vec2>& line_segments) {
	if (line_segments.size() == 0) {
		return {};
	}
	if (line_segments.size() == 1) {
		mu::panic("can't be a single point");
	}

	mu::Vec<glm::vec2> lines;
	lines.reserve(2 + (line_segments.size() - 2)*2);

	lines.push_back(line_segments.front());
	for (int i = 1; i < line_segments.size()-1; i++) {
		lines.push_back(line_segments[i]);
		lines.push_back(line_segments[i]);
	}
	lines.push_back(line_segments.back());

	return lines;
}

void test_line_segments_to_lines() {
	mu_test_suite("test_line_segments_to_lines");

	constexpr glm::vec2 P[] = {
		glm::vec2{-3, 5},
		glm::vec2{3, 5},
		glm::vec2{3, -5},
		glm::vec2{0, 1},
	};

	mu_test(line_segments_to_lines(mu::Vec<glm::vec2>({})) == mu::Vec<glm::vec2>({}));
	mu_test(line_segments_to_lines(mu::Vec<glm::vec2>({P[0], P[1]})) == mu::Vec<glm::vec2>({P[0], P[1]}));
	mu_test(line_segments_to_lines(mu::Vec<glm::vec2>({P[0], P[1], P[2]})) == mu::Vec<glm::vec2>({P[0], P[1], P[1], P[2]}));
	mu_test(line_segments_to_lines(mu::Vec<glm::vec2>({P[0], P[1], P[2], P[3]})) == mu::Vec<glm::vec2>({P[0], P[1], P[1], P[2], P[2], P[3]}));
}

// f(x) = a*x^2 + b*x + c
// consts = {a,b,c}
using QuadraticFuncConsts = glm::vec3;

// using center and a point on parabola
QuadraticFuncConsts quad_func_new(glm::vec2 c, glm::vec2 p) {
	glm::vec2 t { c.x-p.x, p.y }; // 3rd point is mirrored
	glm::mat3 M {
		c.x*c.x, c.x, 1,
		p.x*p.x, p.x, 1,
		t.x*t.x, t.x, 1,
	};
	glm::vec3 y { c.y, p.y, t.y };
	return glm::inverse(glm::transpose(M)) * y;
}

// f(x)
float quad_func_eval(QuadraticFuncConsts c, float x) {
	return c[0]*x*x + c[1]*x + c[2];
}

// f(x) = a*x + b
// consts = {a,b}
using LinearFuncConsts = glm::vec2;

LinearFuncConsts linear_func_new(glm::vec2 p1, glm::vec2 p2) {
	glm::mat2 M {
		p1.x, 1,
		p2.x, 1,
	};
	glm::vec2 y { p1.y, p2.y };
	return glm::inverse(glm::transpose(M)) * y;
}

// f(x)
float linear_func_eval(LinearFuncConsts c, float x) {
	return c[0]*x + c[1];
}

namespace fmt {
	template<>
	struct formatter<glm::uvec2> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::uvec2 &v, FormatContext &ctx) {
			return fmt::format_to(ctx.out(), "glm::uvec2{{{}, {}}}", v.x, v.y);
		}
	};

	template<>
	struct formatter<glm::vec2> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec2 &v, FormatContext &ctx) {
			return fmt::format_to(ctx.out(), "glm::vec2{{{}, {}}}", v.x, v.y);
		}
	};

	template<>
	struct formatter<glm::vec3> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec3 &v, FormatContext &ctx) {
			return fmt::format_to(ctx.out(), "glm::vec3{{{}, {}, {}}}", v.x, v.y, v.z);
		}
	};

	template<>
	struct formatter<glm::vec4> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec4 &v, FormatContext &ctx) {
			return fmt::format_to(ctx.out(), "glm::vec4{{{}, {}, {}, {}}}", v.x, v.y, v.z, v.w);
		}
	};

	template<>
	struct formatter<glm::mat3> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::mat3 &m, FormatContext &ctx) {
			return fmt::format_to(
				ctx.out(),
				"glm::mat3{{\n"
				" {} {} {}\n"
				" {} {} {}\n"
				" {} {} {}\n"
				"}}",
				m[0][0], m[1][0], m[2][0],
				m[0][1], m[1][1], m[2][1],
				m[0][2], m[1][2], m[2][2]
			);
		}
	};

	template<>
	struct formatter<glm::mat4> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::mat4 &m, FormatContext &ctx) {
			return fmt::format_to(
				ctx.out(),
				"glm::mat4{{\n"
				" {} {} {} {}\n"
				" {} {} {} {}\n"
				" {} {} {} {}\n"
				" {} {} {} {}\n"
				"}}",
				m[0][0], m[0][1], m[0][2], m[0][3],
				m[1][0], m[1][1], m[1][2], m[1][3],
				m[2][0], m[2][1], m[2][2], m[2][3],
				m[3][0], m[3][1], m[3][2], m[3][3]
			);
		}
	};

	template<>
	struct formatter<AABB> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const AABB &s, FormatContext &ctx) {
			return fmt::format_to(ctx.out(), "AABB{{max: {}, min: {}}}", s.min, s.max);
		}
	};
}
