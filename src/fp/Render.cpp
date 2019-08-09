#include "Render.h"

#include "Primitive.h"
#include "Scene.h"
#include "math/Camera.h"
#include "util/ArrayOutput.h"

namespace fp {

constexpr auto FirstBounceNumUSamples = 6;
constexpr auto FirstBounceNumVSamples = 3;

// What is functional?
// * Immutable data structures. e.g. "SampledPixel" in arrayOutput doesn't
//   accumulate, but some final process should. (if we do accumulation)
// * Scene graph using variant!
// * TMP stuff (if can find a way)

struct IntersectionRecord {
  Hit hit;
  Material material;
};

struct IntersectVisitor {
  const Ray &ray;

  static std::optional<IntersectionRecord>
  unwrapWith(const std::optional<Hit> &hit, const Material &m) {
    if (!hit)
      return {};
    return IntersectionRecord{*hit, m};
  }

  std::optional<IntersectionRecord>
  operator()(const TrianglePrimitive &primitive) const {
    return unwrapWith(primitive.triangle.intersect(ray), primitive.material);
  }
  std::optional<IntersectionRecord>
  operator()(const SpherePrimitive &primitive) const {
    return unwrapWith(primitive.sphere.intersect(ray), primitive.material);
  }
};

std::optional<IntersectionRecord> intersect(const Primitive &primitive,
                                            const Ray &ray) {
  return std::visit(IntersectVisitor{ray}, primitive);
}

std::optional<IntersectionRecord> intersect(const Scene &scene,
                                            const Ray &ray) {
  std::optional<IntersectionRecord> nearest;
  for (auto &primitive : scene.primitives) {
    auto thisIntersection = intersect(primitive, ray);
    if (thisIntersection
        && (!nearest || thisIntersection->hit.distance < nearest->hit.distance))
      nearest = thisIntersection;
  }
  return nearest;
}

Vec3 radiance(const Scene &scene, std::mt19937 &rng, const Ray &ray, int depth,
              int numUSamples, int numVSamples, bool preview);

Vec3 singleRay(const Scene &scene, std::mt19937 &rng,
               const IntersectionRecord &intersectionRecord, const Ray &ray,
               const OrthoNormalBasis &basis, double u, double v, int depth,
               bool preview) {
  std::uniform_real_distribution<> unit(0, 1.0);
  const auto &mat = intersectionRecord.material;
  const auto &hit = intersectionRecord.hit;
  auto theta = 2 * M_PI * u;
  auto radiusSquared = v;
  auto radius = sqrt(radiusSquared);
  // Construct the new direction.
  const auto newDir =
      basis
          .transform(Vec3(cos(theta) * radius, sin(theta) * radius,
                          sqrt(1 - radiusSquared)))
          .normalised();
  double p = unit(rng);

  if (p < mat.reflectivity) {
    auto reflected =
        ray.direction() - hit.normal * 2 * hit.normal.dot(ray.direction());
    auto newRay = Ray::fromOriginAndDirection(hit.position, reflected);

    return mat.diffuse * radiance(scene, rng, newRay, depth, 1, 1, preview);
  } else {
    auto newRay = Ray::fromOriginAndDirection(hit.position, newDir);

    return mat.diffuse * radiance(scene, rng, newRay, depth, 1, 1, preview);
  }
}

Vec3 radiance(const Scene &scene, std::mt19937 &rng, const Ray &ray, int depth,
              int numUSamples, int numVSamples, bool preview) {
  auto intersectionRecord = intersect(scene, ray);
  if (!intersectionRecord)
    return scene.environment;

  const auto &mat = intersectionRecord->material;
  const auto &hit = intersectionRecord->hit;
  if (preview)
    return mat.diffuse;

  if (++depth > 5) {
    // TODO: "russian roulette"
    return mat.emission;
  }

  // Sample evenly with random offset.
  std::uniform_real_distribution<> unit(0, 1.0);
  // Create a coordinate system local to the point, where the z is the
  // normal at this point.
  const auto basis = OrthoNormalBasis::fromZ(hit.normal);
  Vec3 result;
  for (auto u = 0; u < numUSamples; ++u) {
    for (auto v = 0; v < numVSamples; ++v) {
      const auto sampleU = (static_cast<double>(u) + unit(rng))
                           / static_cast<double>(numUSamples);
      const auto sampleV = (static_cast<double>(v) + unit(rng))
                           / static_cast<double>(numVSamples);
      // TODO can we use accumulate here?
      result += mat.emission
                + singleRay(scene, rng, *intersectionRecord, ray, basis,
                            sampleU, sampleV, depth, preview);
    }
  }
  if (numUSamples == 1 && numVSamples == 1)
    return result;
  else
    return result * (1.0 / (numUSamples * numVSamples));
}

void render(const Camera &camera, const Scene &scene, ArrayOutput &output,
            int samplesPerPixel, bool preview,
            std::function<void()> updateFunc) {
  auto width = output.width();
  auto height = output.height();
  std::mt19937 rng(samplesPerPixel);
  std::uniform_real_distribution<> unit(0.0, 1.0);

  for (int sample = 0; sample < samplesPerPixel; ++sample) {
    for (auto y = 0; y < height; ++y) {
      for (auto x = 0; x < width; ++x) {
        auto u = unit(rng);
        auto v = unit(rng);
        auto yy = (2 * (static_cast<double>(y) + u + 0.5) / (height - 1)) - 1;
        auto xx = (2 * (static_cast<double>(x) + v + 0.5) / (width - 1)) - 1;
        auto ray = camera.ray(xx, yy, rng);
        output.plot(x, y,
                    radiance(scene, rng, ray, 0, FirstBounceNumUSamples,
                             FirstBounceNumVSamples, preview),
                    1);
      }
    }
    updateFunc();
  }
}

}