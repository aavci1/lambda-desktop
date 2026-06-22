#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <Lambda/Graphics/Styles.hpp>

#include "Graphics/Metal/MetalCanvasTypes.hpp"
#include "Graphics/PathFlattener.hpp"

#include <vector>

namespace lambda {

/**
 * CPU path → tessellated triangles for the Metal path pipeline.
 * Appends to `pathVerts` and, if any geometry was produced, pushes one `PathMesh` op onto `ops`.
 */
void metalPathRasterizeToMesh(Path const& path, FillStyle const& fill, StrokeStyle const& stroke,
                              Mat3 const& transform, float dpiScaleX, float dpiScaleY, float opacity,
                              float viewportW, float viewportH, std::vector<PathVertex>& pathVerts,
                              std::vector<MetalPathOp>& pathOps, std::vector<MetalOpRef>& opOrder,
                              BlendMode blendMode);

} // namespace lambda
