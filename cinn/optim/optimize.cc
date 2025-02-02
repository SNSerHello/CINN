// Copyright (c) 2021 CINN Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cinn/optim/optimize.h"

#include "cinn/ir/ir_printer.h"
#include "cinn/ir/ir_schedule_util.h"
#include "cinn/optim/call_arg_list_to_pod_value.h"
#include "cinn/optim/cast_bool_to_int8.h"
#include "cinn/optim/cast_simplify.h"
#include "cinn/optim/eliminate_broadcast_in_forloop.h"
#include "cinn/optim/extern_call_process.h"
#include "cinn/optim/fold_cinn_call_arguments.h"
#include "cinn/optim/if_simplify.h"
#include "cinn/optim/insert_debug_log_callee.h"
#include "cinn/optim/ir_copy.h"
#include "cinn/optim/ir_simplify.h"
#include "cinn/optim/lower_function_call_bind_vars.h"
#include "cinn/optim/lower_intrin.h"
#include "cinn/optim/map_extern_call.h"
#include "cinn/optim/remove_nested_block.h"
#include "cinn/optim/remove_schedule_block.h"
#include "cinn/optim/replace_const_param_to_integer.h"
#include "cinn/optim/transform_gpu_forloop.h"
#include "cinn/optim/transform_polyfor_to_for.h"
#include "cinn/optim/unroll_loops.h"
#include "cinn/optim/vectorize_loops.h"

DECLARE_bool(cinn_ir_schedule);

namespace cinn {
namespace optim {

Expr Optimize(Expr e, Target target, bool runtime_debug_info, bool remove_gpu_for_loops) {
  CHECK(e.defined());
  auto copied = IRCopy(e);

  FoldCINNCallArguments(&copied);
  TransformPolyForToFor(&copied);
  ReplaceConstParamToInteger(&copied);
  CastSimplify(&copied);
  Simplify(&copied);
  UnrollLoop(&copied);
  VLOG(4) << "After Optimize UnrollLoop:" << copied;

  VectorizeLoops(&copied, target);
  VLOG(4) << "After Optimize VectorizeLoops:" << copied;
#ifdef CINN_WITH_CUDA
  if (FLAGS_cinn_ir_schedule && copied.as_lowered_func()) {
    ir::SetCudaAxisInfo(&copied);
  }
  if (remove_gpu_for_loops) {
    RemoveGpuForloopsAxis(&copied);
  }
  CudaSyncThreadsDropIfThenElse(&copied);
#endif

  RemoveNestedBlock(&copied);
  VLOG(4) << "After Optimize RemoveNestedBlock:" << copied;

  MapExternCall(&copied, target);
  VLOG(10) << "After Optimize MapExternCall:" << copied;

  ExternCallMultiOutputShallowStore(&copied);
  VLOG(10) << "After Optimize ExternCallMultiOutputShallowStore:" << copied;

  CastSimplify(&copied);
  VLOG(10) << "After Optimize CastSimplify:" << copied;

  Simplify(&copied);
  VLOG(10) << "After Optimize Simplify:" << copied;

  IfSimplify(&copied);
  VLOG(10) << "After Optimize IfSimplify:" << copied;

  if (runtime_debug_info) {
    LOG(WARNING) << "Turn on runtime debug information output";
    InsertDebugLogCallee(&copied);
  }
  return copied;
}

ir::Module Optimize(const ir::Module& module, const Target& target) {
  auto copied = IRCopy(Expr(module));
  if (FLAGS_cinn_ir_schedule) {
    UnrollLoop(&copied);
    VectorizeLoops(&copied, Target());
  }
  VLOG(10) << "After VectorizeLoops:" << copied.as_module_ref();
  RemoveScheduleBlock(&copied);
  VLOG(10) << "After RemoveScheduleBlock:" << copied.as_module_ref();
  LowerFunctionCallBindVars(&copied);
  VLOG(10) << "After LowerFunctionCallBindVars:" << copied.as_module_ref();
  CallArgListToPodValue(&copied);
  VLOG(10) << "After CallArgListToPodValue:" << copied.as_module_ref();
  LowerIntrin(&copied, target);
  VLOG(10) << "After LowerIntrin:" << copied.as_module_ref();

  return copied.as_module_ref();
}

}  // namespace optim
}  // namespace cinn
