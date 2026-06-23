#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>

#include "Graphics/Metal/MetalShaderLibrary.hpp"

#include "LambdaUIShaders.metallib.h"

#include <stdexcept>

namespace lambdaui::detail {

id<MTLLibrary> lambdauiLoadShaderLibrary(id<MTLDevice> device) {
  static NSMutableDictionary* cache = nil;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    cache = [NSMutableDictionary dictionary];
  });

  NSNumber* key = @((uintptr_t)(__bridge void*)device);
  @synchronized(cache) {
    id<MTLLibrary> existing = cache[key];
    if (existing) {
      return existing;
    }

    NSError* err = nil;
    dispatch_data_t libData = dispatch_data_create(
        LambdaUIShaders_metallib,
        static_cast<size_t>(LambdaUIShaders_metallib_len),
        nil,
        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    id<MTLLibrary> lib = [device newLibraryWithData:libData error:&err];
    if (!lib) {
      NSLog(@"LambdaUI: failed to load embedded metallib: %@", err);
      throw std::runtime_error("lambdaui: embedded metallib load failed");
    }
    cache[key] = lib;
    return lib;
  }
}

} // namespace lambdaui::detail
