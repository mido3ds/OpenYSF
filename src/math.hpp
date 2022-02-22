#pragma once

#include <mn/Log.h>

#include <glm/vec3.hpp> // glm::vec3
#include <glm/vec4.hpp> // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/gtc/type_ptr.hpp> // glm::value_ptr

// YS angle format, degrees(0->360): YS(0x0000->0xFFFF), extracted from ys blender scripts
constexpr float YS_MAX      = 0xFFFF;
constexpr float RADIANS_MAX = 6.283185307179586f;
constexpr float DEGREES_MAX = 360.0f;

namespace fmt {
	template<>
	struct formatter<glm::uvec2> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::uvec2 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::uvec2{{{}, {}}}", v.x, v.y);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::vec2> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec2 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::vec2{{{}, {}}}", v.x, v.y);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::vec3> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec3 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::vec3{{{}, {}, {}}}", v.x, v.y, v.z);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::vec4> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::vec4 &v, FormatContext &ctx) {
			return format_to(ctx.out(), "glm::vec4{{{}, {}, {}, {}}}", v.x, v.y, v.z, v.w);
		}
	};
}

namespace fmt {
	template<>
	struct formatter<glm::mat4> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const glm::mat4 &m, FormatContext &ctx) {
			return format_to(
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
}

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

namespace fmt {
	template<>
	struct formatter<AABB> {
		template <typename ParseContext>
		constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

		template <typename FormatContext>
		auto format(const AABB &s, FormatContext &ctx) {
			return format_to(ctx.out(), "AABB{{max: {}, min: {}}}", s.min, s.max);
		}
	};
}

// no intersection if separated along an axis
// overlapping on all axes means AABBs are intersecting
bool aabbs_intersect(const AABB& a, const AABB& b) {
	return glm::all(glm::greaterThanEqual(a.max, b.min)) && glm::all(glm::greaterThanEqual(b.max, a.min));
}

void test_aabbs_intersection() {
	{
		const AABB x {
			.min={0.0f, 0.0f, 2.0f},
			.max={1.0f, 1.0f, 5.0f},
		};
		const AABB y {
			.min={0.5f, 0.5f, 3.0f},
			.max={3.0f, 3.0f, 4.0f},
		};
		mn_assert(aabbs_intersect(x, y));
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
		mn_assert(aabbs_intersect(x, y) == false);
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
		mn_assert(aabbs_intersect(x, y));
	}

	mn::log_debug("test_aabbs_intersection: all passed");
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

mn::Buf<uint32_t>
polygons_to_triangles(const mn::Buf<glm::vec3>& vertices, const mn::Buf<uint32_t>& orig_indices, const glm::vec3& center) {
	// dbl_indices -> orig_indices -> vertices
	// vertex = vertices[orig_indices[dbl_indices[i]]]
	// indices to indices to vertices
	// sort dbl_indices from farthest from center to nearst
	auto dbl_indices = mn::buf_with_allocator<size_t>(mn::memory::tmp());
	for (size_t i = 0; i < orig_indices.count; i++) {
		mn::buf_push(dbl_indices, i);
	}
	auto dist_from_center = mn::buf_with_allocator<double>(mn::memory::tmp());
	for (const auto& v : vertices) {
		mn::buf_push(dist_from_center, glm::distance(center, v));
	}
	std::sort(mn::begin(dbl_indices), mn::end(dbl_indices), [&](size_t a, size_t b) {
		return dist_from_center[orig_indices[a]] > dist_from_center[orig_indices[b]];
	});

	mn::Buf<uint32_t> out {};
	auto indices = mn::buf_clone(orig_indices, mn::memory::tmp());

	// limit no of iterations to avoid inf loop
	size_t k = indices.count + 1;
	while (k > 0 && indices.count > 3) {
		k--;

		for (size_t j = 0; j < dbl_indices.count; j++) {
			auto i = dbl_indices[j];

			// indices
			const uint32_t iv0 = indices[mod(i-1, indices.count)];
			const uint32_t iv2 = indices[mod(i+1, indices.count)];

			bool is_ear = true;

			// segment: (v0, v2) must not intersect with any other edge in polygon
			// for edge in edges:
			//   if not share_vertex(segment, edge):
			//     if intersects(segment, edge): return false
			for (size_t j = 0; j < indices.count; j++) {
				// edge
				const uint32_t jv0 = indices[j];
				const uint32_t jv1 = indices[mod(j+1, indices.count)];

				// don't test the edge if it shares a vertex with it
				if ((jv0 != iv0 && jv0 != iv2) && (jv1 != iv0 && jv1 != iv2)) {
					if (lines_intersect(vertices[jv0], vertices[jv1], vertices[iv0], vertices[iv2])) {
						is_ear = false;
						break;
					}
				}
			}

			if (is_ear) {
				mn::buf_push(out, indices[mod(i-1, indices.count)]);
				mn::buf_push(out, indices[i]);
				mn::buf_push(out, indices[mod(i+1, indices.count)]);

				mn::buf_remove_ordered(indices, i);
				mn::buf_remove_ordered(dbl_indices, j);

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

	if (indices.count != 3) {
		mn::log_error("failed to tesselate");
	}
	mn::buf_concat(out, indices);
	return out;
}

mn::Buf<uint32_t>
polygons2d_to_triangles(const mn::Buf<glm::vec2>& vertices, mn::Allocator allocator = mn::allocator_top()) {
	glm::vec2 center {};
	for (const auto& vertex : vertices) {
		center += vertex;
	}
	center /= vertices.count;

	auto orig_indices = mn::buf_with_allocator<uint32_t>(mn::memory::tmp());
	mn::buf_reserve(orig_indices, vertices.count);
	for (int i = 0; i < vertices.count; i++) {
		mn::buf_push(orig_indices, i);
	}

	// dbl_indices -> orig_indices -> vertices
	// vertex = vertices[orig_indices[dbl_indices[i]]]
	// indices to indices to vertices
	// sort dbl_indices from farthest from center to nearst
	auto dbl_indices = mn::buf_with_allocator<size_t>(mn::memory::tmp());
	for (size_t i = 0; i < orig_indices.count; i++) {
		mn::buf_push(dbl_indices, i);
	}
	auto dist_from_center = mn::buf_with_allocator<double>(mn::memory::tmp());
	for (const auto& v : vertices) {
		mn::buf_push(dist_from_center, glm::distance(center, v));
	}
	std::sort(mn::begin(dbl_indices), mn::end(dbl_indices), [&](size_t a, size_t b) {
		return dist_from_center[orig_indices[a]] > dist_from_center[orig_indices[b]];
	});

	auto out = mn::buf_with_allocator<uint32_t>(allocator);
	auto indices = mn::buf_clone(orig_indices, mn::memory::tmp());

	// limit no of iterations to avoid inf loop
	size_t k = indices.count + 1;
	while (k > 0 && indices.count > 3) {
		k--;

		for (size_t j = 0; j < dbl_indices.count; j++) {
			auto i = dbl_indices[j];

			// indices
			const uint32_t iv0 = indices[mod(i-1, indices.count)];
			const uint32_t iv2 = indices[mod(i+1, indices.count)];

			bool is_ear = true;

			// segment: (v0, v2) must not intersect with any other edge in polygon
			// for edge in edges:
			//   if not share_vertex(segment, edge):
			//     if intersects(segment, edge): return false
			for (size_t j = 0; j < indices.count; j++) {
				// edge
				const uint32_t jv0 = indices[j];
				const uint32_t jv1 = indices[mod(j+1, indices.count)];

				// don't test the edge if it shares a vertex with it
				if ((jv0 != iv0 && jv0 != iv2) && (jv1 != iv0 && jv1 != iv2)) {
					if (lines2d_intersect(vertices[jv0], vertices[jv1], vertices[iv0], vertices[iv2])) {
						is_ear = false;
						break;
					}
				}
			}

			if (is_ear) {
				mn::buf_push(out, indices[mod(i-1, indices.count)]);
				mn::buf_push(out, indices[i]);
				mn::buf_push(out, indices[mod(i+1, indices.count)]);

				mn::buf_remove_ordered(indices, i);
				mn::buf_remove_ordered(dbl_indices, j);

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

	if (indices.count != 3) {
		mn::log_error("failed to tesselate");
	}
	mn::buf_concat(out, indices);
	return out;
}

template<typename T>
bool buf_equal(const mn::Buf<T>& a, const mn::Buf<T> b) {
	if (a.count != b.count) {
		return false;
	}
	for (size_t i = 0; i < a.count; i++) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

void test_polygons_to_triangles() {
	mn::allocator_push(mn::memory::tmp());
	mn_defer(mn::allocator_pop());

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{2,4,0},
			{2,2,0},
			{3,2,0},
			{4,3,0},
			{4,4,0},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {3, 3, 0};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({4, 0, 1, 4, 1, 2, 2, 3, 4})));
	}

	{
		// shouldn't intersect
		glm::vec3 a {2,4,0};
		glm::vec3 b {4,4,0};
		glm::vec3 c {4,3,0};
		glm::vec3 d {3,2,0};

		mn_assert(lines_intersect(a, b, c, d) == false);
	}

	{
		// shouldn't intersect
		glm::vec3 a {1.311345,  0.627778,  1.068002};
		glm::vec3 b {1.311345, -0.000053, -1.472697};
		glm::vec3 c {1.311345, -0.000053,  1.717336};
		glm::vec3 d {1.311345,  0.512254,  2.414495};

		mn_assert(lines_intersect(a, b, c, d) == false);
	}

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{4,4,0},
			{5,3,0},
			{4,2,0},
			{3,3,0},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3});
		const glm::vec3 center {4, 3, 0};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({3, 0, 1, 1, 2, 3})));
	}

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{2,4,0},
			{2,2,0},
			{3,2,0},
			{4,3,0},
			{4,4,0},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {3, 3, 0};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({4, 0, 1, 4, 1, 2, 2, 3, 4})));
	}

	{
		const auto vertices = mn::buf_lit<glm::vec3>({
			{0.19, -0.77, 0.82},
			{0.23, -0.75, 0.68},
			{0.20, -0.75, 0.00},
			{0.32, -0.71, 0.00},
			{0.31, -0.73, 0.96},
		});
		const auto indices = mn::buf_lit<uint32_t>({0,1,2,3,4});
		const glm::vec3 center {0.25, -0.742, 0.492};
		mn_assert(buf_equal(polygons_to_triangles(vertices, indices, center), mn::buf_lit<uint32_t>({2, 3, 4, 1, 2, 4, 0, 1, 4})));
	}

	mn::log_debug("test_polygons_to_triangles: all passed");
}
