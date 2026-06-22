#pragma once

#import <Metal/Metal.h>

namespace lambda::detail {

/// Loads the embedded `LambdaShaders.metallib` once per `MTLDevice` (cached).
id<MTLLibrary> lambdaLoadShaderLibrary(id<MTLDevice> device);

} // namespace lambda::detail
