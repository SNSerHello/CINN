#include "cinn/backends/llvm/codegen_llvm.h"

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <iomanip>
#include <memory>
#include <utility>
#include <vector>

#include "cinn/backends/llvm/cinn_runtime_llvm_ir.h"
#include "cinn/ir/ir.h"
#include "cinn/lang/compute.h"
#include "cinn/lang/lower.h"
#include "cinn/lang/module.h"
#include "cinn/lang/placeholder.h"

namespace cinn {
namespace backends {

namespace {

auto CreateCodeGenLLVMTestLLVM() {
  auto context = std::make_unique<llvm::LLVMContext>();
  auto b       = std::make_unique<llvm::IRBuilder<>>(*context);
  auto m       = std::make_unique<llvm::Module>("test_codegen_llvm", *context);
  auto emitter = std::make_unique<CodeGenLLVM>(m.get(), b.get());

  return std::make_tuple(std::move(m), std::move(b), std::move(context), std::move(emitter));
}

auto CreateTensor() {
  ir::Expr M(3);
  ir::Expr N(2);
  lang::Placeholder<float> a("a", {M, N});
  lang::Placeholder<float> b("b", {M, N});
  auto c = lang::Compute(
      {M, N}, [&](auto i, auto j) { return a(i, j) + b(i, j); }, "c");

  lang::Buffer c_buf(common::Float(32));

  return std::make_tuple(std::move(a), std::move(b), std::move(c), std::move(c_buf));
}

auto CreateLLVMType(llvm::LLVMContext *context) {
  llvm::Type *i8  = llvm::Type::getInt8Ty(*context);
  llvm::Type *i32 = llvm::Type::getInt32Ty(*context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(*context);
  llvm::Type *u32 = llvm::Type::getInt32Ty(*context);
  llvm::Type *f32 = llvm::Type::getFloatTy(*context);

  return std::make_tuple(i8, i32, i64, u32, f32);
}

template <typename OT, typename NT1, typename T1, typename NT2 = NT1, typename T2 = T1>
auto CreateBinaryOp(common::Type t, T1 x, T2 y) {
  auto px = std::make_unique<NT1>(t, x);
  auto py = std::make_unique<NT2>(t, y);

  auto ex = ir::Expr(px.release());
  auto ey = ir::Expr(py.release());

  return std::make_unique<OT>(std::move(ex), std::move(ey));
}

auto CreateIrBuffer(common::Type t, std::string name, std::vector<int> shape, int data_alignment = 0) {
  CHECK_GE(data_alignment, 0);
  auto buffer = ir::_Buffer_::Make(std::move(name), std::move(t));

  if (data_alignment) {
    buffer->data_alignment = data_alignment;
  }

  for (auto i : shape) {
    auto pi = std::make_unique<ir::IntImm>(common::Int(32), i);
    buffer->shape.emplace_back(pi.release());
  }

  return buffer;
}

auto CreateIrTensor(std::string name, std::vector<int> shape) {
  std::vector<ir::Expr> shape_expr;
  for (auto i : shape) {
    auto pi = std::make_unique<ir::IntImm>(common::Int(32), i);
    shape_expr.emplace_back(pi.release());
  }

  auto tensor    = ir::_Tensor_::Make(std::move(name), std::move(shape_expr), {});
  tensor->domain = tensor->shape;
  return tensor;
}

auto CreateLoweredFunc() {
  //
}

}  // namespace

TEST(CodeGenLLVM, Imm) {
  return;
  auto context = std::make_unique<llvm::LLVMContext>();
  auto b       = std::make_unique<llvm::IRBuilder<>>(*context);
  auto m       = std::make_unique<llvm::Module>("test_codegen_llvm", *context);
  auto emitter = std::make_unique<CodeGenLLVM>(m.get(), b.get());

  llvm::Type *i32 = llvm::Type::getInt32Ty(*context);
  llvm::Type *u32 = llvm::Type::getInt32Ty(*context);
  llvm::Type *f32 = llvm::Type::getFloatTy(*context);

  llvm::Value *value = nullptr;

  ir::IntImm i32_imm(common::Int(32), 10);
  value = emitter->Visit(&i32_imm);
  ASSERT_EQ(value->getType(), i32);
  ASSERT_EQ(value, llvm::ConstantInt::get(i32, i32_imm.value, true));
  // value->print(llvm::outs(), false);

  ir::UIntImm u32_imm(common::UInt(32), 5);
  value = emitter->Visit(&u32_imm);
  ASSERT_EQ(value->getType(), u32);
  ASSERT_EQ(value, llvm::ConstantInt::get(u32, u32_imm.value, false));

  ir::FloatImm float_imm(common::Float(32), 2.5);
  value = emitter->Visit(&float_imm);
  ASSERT_EQ(value->getType(), f32);
  ASSERT_EQ(value, llvm::ConstantFP::get(f32, float_imm.value));
}

TEST(CodeGenLLVM, Expr) {
  return;
  auto context = std::make_unique<llvm::LLVMContext>();
  auto b       = std::make_unique<llvm::IRBuilder<>>(*context);
  auto m       = std::make_unique<llvm::Module>("test_binary_op", *context);
  auto emitter = std::make_unique<CodeGenLLVM>(m.get(), b.get());

  llvm::Type *i8  = llvm::Type::getInt8Ty(*context);
  llvm::Type *i32 = llvm::Type::getInt32Ty(*context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(*context);
  llvm::Type *u32 = llvm::Type::getInt32Ty(*context);
  llvm::Type *f32 = llvm::Type::getFloatTy(*context);

  llvm::Value *value        = nullptr;
  llvm::Value *expect_value = nullptr;

  std::string outs;
  llvm::raw_string_ostream ss(outs);

  // +
  do {
    int x   = 2;
    int y   = 3;
    auto op = CreateBinaryOp<ir::Add, ir::IntImm, int>(common::Int(32), x, y);

    expect_value = llvm::ConstantInt::get(i32, x + y);
    value        = emitter->Visit(op.get());
    ASSERT_EQ(value->getType(), i32);
    ASSERT_EQ(value, expect_value);
    // value->print(llvm::outs(), false);
    // value->print(ss, false);
    // LOG(INFO) << "xxx: " << ss.str();
  } while (false);

  // -
  do {
    float x = 2.5;
    float y = 3.5;
    auto op = CreateBinaryOp<ir::Sub, ir::FloatImm, float>(common::Float(32), x, y);

    expect_value = llvm::ConstantFP::get(f32, x - y);
    value        = emitter->Visit(op.get());
    ASSERT_EQ(value->getType(), f32);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // *
  do {
    int x        = 5;
    int y        = 3;
    auto op      = CreateBinaryOp<ir::Mul, ir::IntImm, float>(common::Int(64), x, y);
    expect_value = llvm::ConstantInt::get(i64, x * y);
    value        = emitter->Visit(op.get());
    ASSERT_EQ(value->getType(), i64);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // /
  do {
    float x      = 6;
    float y      = 4;
    auto op      = CreateBinaryOp<ir::Div, ir::FloatImm, float>(common::Float(32), x, y);
    expect_value = llvm::ConstantFP::get(f32, x / y);
    value        = emitter->Visit(op.get());
    ASSERT_EQ(value->getType(), f32);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // %
  do {
    int x        = 25;
    int y        = 7;
    auto op      = CreateBinaryOp<ir::Mod, ir::IntImm, int>(common::Int(32), x, y);
    expect_value = llvm::ConstantInt::get(i32, x % y);
    value        = emitter->Visit(op.get());
    ASSERT_EQ(value->getType(), i32);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // ==
  do {
    int x        = 3;
    int y        = 3;
    auto op      = CreateBinaryOp<ir::EQ, ir::IntImm, int>(common::Int(32), x, y);
    expect_value = llvm::ConstantInt::get(i8, 1);
    value        = emitter->Visit(op.get());
    ASSERT_EQ(value->getType(), i8);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // !=
  do {
    float x = 3;
    float y = 3;

    auto op      = CreateBinaryOp<ir::NE, ir::FloatImm, float>(common::Float(32), x, y);
    expect_value = llvm::ConstantInt::get(i8, 0);
    value        = emitter->Visit(op.get());
    ASSERT_EQ(value->getType(), i8);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // <
  do {
    int x        = 6;
    int y        = 6;
    auto op      = CreateBinaryOp<ir::LT, ir::IntImm, int>(common::Int(32), x, y);
    value        = emitter->Visit(op.get());
    expect_value = llvm::ConstantInt::get(i8, 0);
    ASSERT_EQ(value->getType(), i8);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // <=
  do {
    int x        = 6;
    int y        = 6;
    auto op      = CreateBinaryOp<ir::LE, ir::IntImm, int>(common::Int(32), x, y);
    value        = emitter->Visit(op.get());
    expect_value = llvm::ConstantInt::get(i8, 1);
    ASSERT_EQ(value->getType(), i8);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // >
  do {
    int x        = 6;
    int y        = 6;
    auto op      = CreateBinaryOp<ir::GT, ir::IntImm, int>(common::Int(32), x, y);
    value        = emitter->Visit(op.get());
    expect_value = llvm::ConstantInt::get(i8, 0);
    ASSERT_EQ(value->getType(), i8);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // >=
  do {
    int x        = 6;
    int y        = 6;
    auto op      = CreateBinaryOp<ir::GE, ir::IntImm, int>(common::Int(32), x, y);
    value        = emitter->Visit(op.get());
    expect_value = llvm::ConstantInt::get(i8, 1);
    ASSERT_EQ(value->getType(), i8);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // and, or
  do {
  } while (false);

  // min
  do {
    int x        = 2;
    int y        = 3;
    auto op      = CreateBinaryOp<ir::Min, ir::IntImm, int>(common::Int(32), x, y);
    value        = emitter->Visit(op.get());
    expect_value = llvm::ConstantInt::get(i32, std::min(x, y));
    ASSERT_EQ(value->getType(), i32);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // max
  do {
    float x      = 2;
    float y      = 3;
    auto op      = CreateBinaryOp<ir::Max, ir::FloatImm, float>(common::Float(32), x, y);
    value        = emitter->Visit(op.get());
    expect_value = llvm::ConstantFP::get(f32, std::max(x, y));
    ASSERT_EQ(value->getType(), f32);
    ASSERT_EQ(value, expect_value);
  } while (false);

  // minus
  // not

  // cast
  do {
    // i32 -> u32
    int v1       = 1;
    auto x1      = std::make_unique<ir::IntImm>(common::Int(32), v1);
    auto ex1     = ir::Expr(x1.release());
    auto op1     = ir::Cast::Make(common::UInt(32), std::move(ex1));
    value        = emitter->Visit(&op1);
    expect_value = llvm::ConstantInt::get(u32, v1);
    ASSERT_EQ(value->getType(), u32);
    ASSERT_EQ(value, expect_value);

    // i32 -> f32
    int v2       = 2;
    auto x2      = std::make_unique<ir::IntImm>(common::Int(32), v2);
    auto ex2     = ir::Expr(x2.release());
    auto op2     = ir::Cast::Make(common::Float(32), std::move(ex2));
    value        = emitter->Visit(&op2);
    expect_value = llvm::ConstantFP::get(f32, v2);
    ASSERT_EQ(value->getType(), f32);
    ASSERT_EQ(value, expect_value);

    // f32 -> i32
    float v3     = 3;
    auto x3      = std::make_unique<ir::FloatImm>(common::Float(32), v3);
    auto ex3     = ir::Expr(x3.release());
    auto op3     = ir::Cast::Make(common::Int(32), std::move(ex3));
    value        = emitter->Visit(&op3);
    expect_value = llvm::ConstantInt::get(i32, v3);
    ASSERT_EQ(value, expect_value);
  } while (false);
}

TEST(CodeGenLLVM, Statement) {
  return;
  std::string outs;
  llvm::raw_string_ostream ss(outs);

  do {
    auto [m, b, context, emitter]     = CreateCodeGenLLVMTestLLVM();    // NOLINT
    auto [i8, i32, i64, u32, f32]     = CreateLLVMType(context.get());  // NOLINT
    llvm::FunctionType *function_type = llvm::FunctionType::get(i32, {}, false);
    llvm::Function *function          = llvm::Function::Create(
        function_type, llvm::Function::ExternalLinkage, "codegen_llvm_test.Alloc_Store_Load_Free", m.get());

    std::string module_str;
    module_str += "; ModuleID = 'test_codegen_llvm'";
    module_str += "\nsource_filename = \"test_codegen_llvm\"\n";
    module_str += "\ndefine i32 @codegen_llvm_test.Alloc_Store_Load_Free()";

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context, "entry", function);
    b->SetInsertPoint(entry);

    module_str += " {\nentry:";

    // ir::Tensor
    auto tensor_op    = CreateIrTensor("x", {2, 3});
    tensor_op->buffer = CreateIrBuffer(common::Int(32), "", {2, 3});

    // ir::Alloc
    auto alloc_op         = std::make_unique<ir::Alloc>();
    alloc_op->destination = ir::Expr(tensor_op->buffer);

    // ir::Store
    auto store_op    = std::make_unique<ir::Store>();
    store_op->tensor = ir::Expr(tensor_op);
    for (int i : {1, 1}) {
      auto pi = std::make_unique<ir::IntImm>(common::Int(32), std::move(i));
      store_op->indices.emplace_back(pi.release());
    }
    auto store_value = std::make_unique<ir::IntImm>(common::Int(32), 5);
    store_op->value  = ir::Expr(store_value.release());

    // ir::Load
    auto load_op    = std::make_unique<ir::Load>();
    load_op->tensor = ir::Expr(tensor_op);
    for (int i : {1, 1}) {
      auto pi = std::make_unique<ir::IntImm>(common::Int(32), std::move(i));
      load_op->indices.emplace_back(pi.release());
    }

    // ir::Free
    auto free_op         = std::make_unique<ir::Free>();
    free_op->destination = ir::Expr(tensor_op->buffer);

    // ir::Call
    auto call_op  = std::make_unique<ir::Call>(common::Int(32));
    call_op->name = "codegen_llvm_test.Alloc_Store_Load_Free";

    // llvm::dyn_cast<llvm::AllocaInst>(buffer)->removeFromParent();

    // Emit llvm ir
    auto *alloc_inst = llvm::dyn_cast<llvm::AllocaInst>(emitter->Visit(alloc_op.get()));
    module_str += "\n  %0 = alloca [6 x i32]";
    auto *store_inst = llvm::dyn_cast<llvm::StoreInst>(emitter->Visit(store_op.get()));
    module_str += "\n  %1 = getelementptr [6 x i32], [6 x i32]* %0, i32 1";
    module_str += "\n  store i32 5, [6 x i32]* %1";
    auto *load_inst = llvm::dyn_cast<llvm::LoadInst>(emitter->Visit(load_op.get()));
    module_str += "\n  %2 = getelementptr [6 x i32], [6 x i32]* %0, i32 1";
    module_str += "\n  %3 = load [6 x i32], [6 x i32]* %2";
    // emitter->Visit(free_op.get());

    // auto *call_inst =
    // llvm::dyn_cast<llvm::CallInst>(emitter->Visit(call_op.get()));

    b->CreateRet(llvm::ConstantInt::get(i32, 1));

    // module_str += "\n  ret [6 x i32] %3";
    module_str += "\n  ret i32 1";
    module_str += "\n}\n";

    auto log_inst = [&ss, &outs](auto *inst) {
      inst->print(ss, false);
      LOG(INFO) << inst->getOpcodeName() << " instruction:" << ss.str();
      outs.clear();
    };

    log_inst(alloc_inst);
    log_inst(store_inst);
    log_inst(load_inst);

    ASSERT_EQ(module_str, ss.str());
  } while (false);
}

TEST(CodeGenLLVM, LowerFunc) {
  std::string outs;
  llvm::raw_string_ostream ss(outs);

  do {
    auto context = std::make_unique<llvm::LLVMContext>();
    // auto src_name = m->getSourceFileName();
    llvm::SMDiagnostic error;
    std::string runtime_ir(backends::kRuntimeLlvmIr);
    // NOTE: read ir string before IRBuilder create
    auto m = llvm::parseAssemblyString(runtime_ir, error, *context);
    auto b = std::make_unique<llvm::IRBuilder<>>(*context);

    auto emitter = std::make_unique<CodeGenLLVM>(m.get(), b.get());

    auto [i8, i32, i64, u32, f32] = CreateLLVMType(context.get());  // NOLINT
    auto [x, y, z, z_buf]         = CreateTensor();                 // NOLINT

    z->Bind(z_buf);

    auto function = lang::Lower("add1", {x, y, z});
    ir::Expr func_expr(function);

    auto ir_function = emitter->Visit(&func_expr);
    LOG(INFO) << "ir function: " << func_expr;

    auto func = m->getFunction("add1");
  } while (false);
}

}  // namespace backends
}  // namespace cinn
