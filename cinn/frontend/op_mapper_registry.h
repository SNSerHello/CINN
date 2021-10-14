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

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

#include "cinn/common/common.h"
#include "cinn/common/macros.h"
#include "cinn/common/target.h"
#include "cinn/common/type.h"
#include "cinn/frontend/net_builder.h"
#include "cinn/frontend/paddle/cpp/op_desc.h"
#include "cinn/frontend/paddle/cpp/var_desc.h"
#include "cinn/frontend/syntax.h"
#include "cinn/hlir/framework/scope.h"
#include "cinn/utils/registry.h"

namespace cinn {
namespace frontend {

class OpMapperContext {
 public:
  OpMapperContext(hlir::framework::Scope* scope,
                  const common::Target& target,
                  NetBuilder* builder,
                  absl::flat_hash_map<std::string, Variable>* var_map,
                  absl::flat_hash_map<std::string, std::string>* var_model_to_program_map)
      : scope_(scope),
        target_(target),
        builder_(builder),
        var_map_(var_map),
        var_model_to_program_map_(var_model_to_program_map) {}

  hlir::framework::Scope* scope_{nullptr};
  const common::Target& target_;
  NetBuilder* builder_{nullptr};

  void AddVar(const std::string& origin_name, const Variable& var, bool replace = false) const {
    const auto& name = cinn::utils::TransValidVarName(origin_name);
    CheckVarNameValid(name);
    if (replace == false) {
      CHECK(!var_map_->count(name)) << "Duplicate variable [" << name << "] found";
    }
    (*var_map_)[name] = var;
  }

  void AddVarModelToProgramMap(const std::string& name, const std::string& id) const {
    (*var_model_to_program_map_)[name] = id;
  }

  Variable GetVar(const std::string& origin_name) const {
    const auto& name = cinn::utils::TransValidVarName(origin_name);
    CheckVarNameValid(name);

    auto it = var_map_->find(name);
    if (it != var_map_->end()) return it->second;

    auto* var = scope_->FindVar(name);
    if (var) {
      auto& tensor = absl::get<hlir::framework::Tensor>(*var);
      Variable var;
      var.set_id(name);
      var->shape = tensor->shape().data();
      var->type  = tensor->type();
      AddVar(name, var);
      return var;
    }

    LOG(FATAL) << "No var called [" << name << "] exists";
    return Variable();
  }

  void AddVarDesc(const std::string& name, paddle::cpp::VarDesc* desc) { var_desc_map_[name] = desc; }

  paddle::cpp::VarDesc* GetVarDesc(const std::string& name) const {
    if (var_desc_map_.count(name)) {
      return var_desc_map_.at(name);
    }
    return nullptr;
  }

 private:
  absl::flat_hash_map<std::string, Variable>* var_map_{nullptr};
  // map from var in Paddle model to var name in program.
  absl::flat_hash_map<std::string, std::string>* var_model_to_program_map_{nullptr};
  // map from var name to var desc
  absl::flat_hash_map<std::string, paddle::cpp::VarDesc*> var_desc_map_;
};

class OpMapper {
 public:
  using OpMapperFunc = std::function<void(const paddle::cpp::OpDesc&, const OpMapperContext&)>;

  OpMapper() = default;

  OpMapper& Set(const OpMapperFunc& kernel) {
    kernel_ = kernel;
    return *this;
  }
  void Run(const paddle::cpp::OpDesc& op_desc, const OpMapperContext& ctx) const { kernel_(op_desc, ctx); }

  std::string name;

 private:
  OpMapperFunc kernel_;
};

class OpMapperRegistry : public Registry<OpMapper> {
 public:
  OpMapperRegistry() = default;

 private:
  CINN_DISALLOW_COPY_AND_ASSIGN(OpMapperRegistry);
};

#define UNIQUE_OPMAPPER_NAME(OpName) static ::cinn::frontend::OpMapper& __op_mapper_registrar_##OpName

#define CINN_REGISTER_OP_MAPPER(OpName, Kernel)                \
  CINN_STR_CONCAT(UNIQUE_OPMAPPER_NAME(OpName), __COUNTER__) = \
      ::cinn::frontend::OpMapperRegistry::Global()->__REGISTER_OR_GET__(#OpName).Set(Kernel);

}  // namespace frontend
}  // namespace cinn