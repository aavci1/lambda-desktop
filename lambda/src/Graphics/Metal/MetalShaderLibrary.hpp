#pragma once

#import <Metal/Metal.h>

namespace lambdaui::detail {

/// Loads the embedded `LambdaUIShaders.metallib` once per `MTLDevice` (cached).
id<MTLLibrary> lambdauiLoadShaderLibrary(id<MTLDevice> device);

} // namespace lambdaui::detail
