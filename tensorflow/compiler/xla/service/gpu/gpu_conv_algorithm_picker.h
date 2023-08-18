/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_COMPILER_XLA_SERVICE_GPU_GPU_CONV_ALGORITHM_PICKER_H_
#define TENSORFLOW_COMPILER_XLA_SERVICE_GPU_GPU_CONV_ALGORITHM_PICKER_H_

#include <optional>
#include <string>
#include <variant>

#include "absl/time/time.h"
#include "tensorflow/compiler/xla/autotune_results.pb.h"
#include "tensorflow/compiler/xla/hlo/ir/hlo_instructions.h"
#include "tensorflow/compiler/xla/hlo/ir/hlo_module.h"
#include "tensorflow/compiler/xla/service/compiler.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_conv_runner.h"
#include "tensorflow/compiler/xla/service/hlo_pass_interface.h"
#include "tensorflow/compiler/xla/stream_executor/device_memory_allocator.h"
#include "tensorflow/compiler/xla/stream_executor/stream_executor.h"
#include "tensorflow/tsl/protobuf/autotuning.pb.h"

#if (defined(GOOGLE_CUDA) && GOOGLE_CUDA)
#include "tensorflow/compiler/xla/stream_executor/gpu/redzone_allocator.h"
#endif

namespace xla {
namespace gpu {

// Modifies CustomCalls to cudnn convolutions, choosing the best algorithm for
// each and adding explicit scratch space to the CustomCalls.
//
// It supports two modes: device and deviceless.
// In device mode, we run autotuning on the device and store autotune results.
//
// In deviceless mode, we pass in some information related to the device and
// use stored autotune results to rewrite convolutions. If the required autotune
// result is not stored, then the performance of convolution will be suboptimal.
class GpuConvAlgorithmPicker : public HloModulePass {
 public:
  static void ClearAutotuneResults();
  static Status WriteAutotuneResults(AutotuneResults* results);
  static Status LoadAutotuneResults(const AutotuneResults& results);

  struct DeviceConfig {
    se::StreamExecutor* stream_exec;  // never null

    // If the `allocator` parameter is not null, we will use it to allocate temp
    // memory while timing the various convolution algorithms.  If it's null,
    // we'll use the default allocator on the StreamExecutor.
    se::DeviceMemoryAllocator* allocator;  // may be null
  };

  struct DevicelessConfig {
    // Used as the key to search for autotune result for the device. Can be
    // found by stream_exec->GetDeviceDescription()->model_str().
    std::string device_description_str;
  };

  explicit GpuConvAlgorithmPicker(DeviceConfig device_config)
      : config_(device_config) {}
  explicit GpuConvAlgorithmPicker(DevicelessConfig deviceless_config)
      : config_(deviceless_config) {}

  absl::string_view name() const override {
    return "gpu-conv-algorithm-picker";
  }

  using HloPassInterface::Run;
  StatusOr<bool> Run(
      HloModule* module,
      const absl::flat_hash_set<absl::string_view>& execution_threads) override;

 private:
  StatusOr<bool> RunOnComputation(HloComputation* computation);
  StatusOr<bool> RunOnInstruction(HloInstruction* instr);
  StatusOr<tensorflow::AutotuneResult> PickBestAlgorithm(
      const HloCustomCallInstruction* instr);

#if (defined(GOOGLE_CUDA) && GOOGLE_CUDA)
  // Simple bundle of an algorithm and its output, for comparing results across
  // autotuned algorithms.
  struct ReferenceResult {
    stream_executor::dnn::AlgorithmDesc algorithm;
    stream_executor::DeviceMemoryBase buffer;
  };

  StatusOr<tensorflow::AutotuneResult> AutotuneOneConvRunner(
      const GpuConvConfig& config, const HloCustomCallInstruction* instr,
      se::DeviceMemoryAllocator* allocator,
      se::RedzoneAllocator* input_output_allocator, se::Stream* stream,
      MaybeFusedConvRunner* const runner,
      absl::Span<const stream_executor::DeviceMemoryBase> operand_buffers,
      stream_executor::DeviceMemoryBase result_buffer,
      std::optional<ReferenceResult>* reference_result,
      absl::Span<const stream_executor::dnn::AlgorithmDesc> disabled_algos);
  StatusOr<tensorflow::AutotuneResult> PickBestAlgorithmNoCacheCuda(
      const HloCustomCallInstruction* instr,
      se::DeviceMemoryAllocator* allocator, se::Stream* stream);
#endif

  StatusOr<tensorflow::AutotuneResult> PickBestAlgorithmNoCacheRocm(
      const HloCustomCallInstruction* instr,
      se::DeviceMemoryAllocator* allocator, se::Stream* stream);

  std::variant<DeviceConfig, DevicelessConfig> config_;
};

}  // namespace gpu
}  // namespace xla
#endif  // TENSORFLOW_COMPILER_XLA_SERVICE_GPU_GPU_CONV_ALGORITHM_PICKER_H_