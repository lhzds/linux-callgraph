#include <map>
#include <set>
#include <deque>
#include <fstream>
#include <iostream>

#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/DerivedUser.h>

static llvm::cl::opt<std::string> InputFile(
    "f",
    llvm::cl::desc("Input bc file list. Must be Absolute Path"),
    llvm::cl::init(""));


bool checkStructType(llvm::Type *type) {
  if (type->isOpaquePointerTy())
    return false;
  if (type->isPointerTy()) {
    auto pt = llvm::dyn_cast<llvm::PointerType>(type);
    return checkStructType(pt->getPointerElementType());
  }
  if (type->isAggregateType()) {
    bool flag = false;
    for (auto ty : type->subtypes()) {
      flag |= checkStructType(ty);
    }
    return flag;
  }
  return type->isFunctionTy();
}

bool checkStructInit(llvm::Constant *GV) {
  if (llvm::dyn_cast<llvm::Function>(GV->stripPointerCasts()))
    return true;

  unsigned i = 0;
  bool flag = false;
  while (llvm::Constant *sub = GV->getAggregateElement(i++)) {
    flag |= checkStructInit(sub);
  }
  return flag;
}

std::set<std::string> collectCB(llvm::Constant *GV) {
  if (llvm::Function *f = llvm::dyn_cast<llvm::Function>(GV->stripPointerCasts()))
    return std::set({f->getName().str()});

  unsigned i = 0;
  std::set<std::string> res;
  while (llvm::Constant *sub = GV->getAggregateElement(i++)) {
    res.merge(collectCB(sub));
  }
  return res;
}

llvm::Value *findBaseClass(llvm::Function *function, llvm::Instruction *i, llvm::Value *fp);

std::set<std::string> collectFP(llvm::Function *function, std::map<std::string, std::set<std::string>> &cbmap) {
  std::set<std::string> res;
  for (auto bb = function->begin(); bb != function->end(); bb++) {
    for (auto insn = bb->begin(); insn != bb->end(); insn++) {
      llvm::Instruction *i = &(*insn);
      switch (i->getOpcode()) {
      case llvm::Instruction::ICmp:
      case llvm::Instruction::Store:
      case llvm::Instruction::PtrToInt:
      case llvm::Instruction::BitCast:
      case llvm::Instruction::PHI:
        {
          //llvm::ICmpInst *inst = llvm::dyn_cast<llvm::ICmpInst>(i);
          for (auto v : i->operand_values()) {
            if (!v->getType()->isPointerTy())
              continue;
            llvm::Function *cb = llvm::dyn_cast<llvm::Function>(v->stripPointerCasts());
            if (cb) {
              res.insert(cb->getName().str());

              llvm::Value *base = findBaseClass(function, i, v);
              if (base && base != v && !llvm::isa<llvm::ConstantPointerNull>(base)) {
                if (llvm::Constant *gv = llvm::dyn_cast<llvm::Constant>(base)) {
                  std::string bname = gv->getName().str();
                  cbmap[bname].insert(cb->getName().str());
                }
              }
            }
          }
          break;
        }
      case llvm::Instruction::Call:
        {
          llvm::CallInst *call = llvm::dyn_cast<llvm::CallInst>(i);
          for (auto &v : call->args()) {
            if (!v->getType()->isPointerTy())
              continue;
            llvm::Function *cb = llvm::dyn_cast<llvm::Function>(v->stripPointerCasts());
            if (cb) {
              res.insert(cb->getName().str());
            }
          }
          break;
        }
      default:
        break;
      }
    }
  }
  return res;
}

llvm::Value *findBaseClass(llvm::Function *function, llvm::Instruction *i, llvm::Value *fp) {
  switch (i->getOpcode()) {
  case llvm::Instruction::PtrToInt:
  case llvm::Instruction::BitCast:
  case llvm::Instruction::PHI:
    {
      // Forward Propagation
      std::deque<std::pair<llvm::Value*, llvm::Value*>> wq;
      for (auto u : i->users()) {
        wq.push_back(std::make_pair(u, i));
      }
      llvm::Value *u = i;
      llvm::Value *prev = i;
      std::set<llvm::Value*> seen;
      while (!wq.empty()) {
        std::tie(u, prev) = wq.front();
        wq.pop_front();

        if (seen.find(u) != seen.end())
          continue;
        seen.insert(u);

        if (llvm::Instruction *ni = llvm::dyn_cast<llvm::Instruction>(u)) {
          if (ni->getFunction() != function)
            continue;
          if (ni->getOpcode() == llvm::Instruction::ICmp
              || ni->getOpcode() == llvm::Instruction::Store) {
            i = ni;
            fp = prev;
            goto fallthrough;
          }
          if (ni->isUnaryOp() || ni->isCast()) {
            for (auto nu : ni->users()) {
              wq.push_back(std::make_pair(nu, ni));
            }
          } else {
            // Skip
          }
        } else if (llvm::Constant *cst = llvm::dyn_cast<llvm::Constant>(u)) {
          return cst;
        } else if (llvm::Operator *op = llvm::dyn_cast<llvm::Operator>(u)) {
          assert(false);
        } else if (llvm::DerivedUser *drv = llvm::dyn_cast<llvm::DerivedUser>(u)) {
          assert(false);
        }
      }
      return nullptr;
fallthrough:
      do{}while(0);
    }
  case llvm::Instruction::ICmp:
  case llvm::Instruction::Store:
    {
      // Backward Propagation
      // def-use 链
      for (auto v : i->operand_values()) {
        if (v == fp)
          continue;
        std::deque<llvm::Value*> wq({v});
        llvm::Value *u = v;
        while (!wq.empty()) {
          u = wq.front();
          wq.pop_front();

          if (llvm::Instruction *ni = llvm::dyn_cast<llvm::Instruction>(u)) {
            if (ni->isCast() || ni->isUnaryOp()) {
              wq.push_back(ni->getOperand(0));
            } else if (ni->getOpcode() == llvm::Instruction::GetElementPtr) {
              wq.push_back(ni->getOperand(0));
            }
          } else if (llvm::Constant *cst = llvm::dyn_cast<llvm::Constant>(u)) {
            if (llvm::ConstantExpr *ce = llvm::dyn_cast<llvm::ConstantExpr>(cst)) {
              wq.push_back(ce->getAsInstruction());
            } else {
              return cst;
            }
          } else if (llvm::Operator *op = llvm::dyn_cast<llvm::Operator>(u)) {
            assert(false);
          } else if (llvm::DerivedUser *drv = llvm::dyn_cast<llvm::DerivedUser>(u)) {
            assert(false);
          }
        }
        return u;
      }
      break;
    }
  default:
    break;
  }
  return nullptr;
}

// 构建依赖图（比较通用性的程序，因为输入仅依赖内核的 bc 文件列表）
int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv, "global call graph\n");
  llvm::SMDiagnostic ctxerr;
  llvm::LLVMContext context;

  std::ifstream bclist(InputFile);
  for (std::string line; std::getline(bclist, line);) {
    std::cout << line << "\n";
    std::map<std::string, std::set<std::string>> import_table;

    std::map<std::string, std::set<std::string>> struct_def_cbs;
    std::map<std::string, std::set<std::string>> func_ext_constant_linkage;
    std::map<std::string, std::set<std::string>> func_ptr_refer;
    std::unique_ptr<llvm::Module> module = llvm::parseIRFile(line, ctxerr, context);
    if (!module) {
      std::ofstream nonbc(line.substr(0, line.rfind('.')) + ".nonbc");
      nonbc << "nonbc\n";
      continue;
    }

    std::set<llvm::Value*> allfunctions;
    for (auto func = module->begin(); func != module->end(); func++)
      allfunctions.insert(&(*func));

    // Check GLobal InitList
    for (auto &gv : module->globals()) {
      bool fp_seen = false; // 该 gv 里是否有函数
      bool def_seen = false; // 是否已经发现定义
      // 找出全局变量中的所有的函数指针（如果有结构体嵌套的函数指针，也找出其中嵌套的）
      std::string gv_name = gv.getName().str();
      std::set<std::string> nested_cbs;
      if (!gv.hasInitializer()) {
        fp_seen = checkStructType(gv.getType()) || true; // LLVM drops struct sub info, always check linkage
      } else {
        llvm::Constant *gi = gv.getInitializer();
        fp_seen = checkStructType(gi->getType()) || checkStructInit(gi);
        nested_cbs = collectCB(gi);
        def_seen = true;
        struct_def_cbs[gv_name].insert(nested_cbs.begin(), nested_cbs.end());
      }

      std::set<llvm::Constant*> seen;
      if (fp_seen) { // 如果 GV 中发现了函数指针
        std::deque<llvm::User*> wq(gv.user_begin(), gv.user_end());
        
        // 沿着 def-use 链开始查找，找出所有的对 GV 的调用
        while (!wq.empty()) {
          auto u = wq.front();
          wq.pop_front();

          if (llvm::Instruction *i = llvm::dyn_cast<llvm::Instruction>(u)) {
            //printf("DEBUG inst\n");
            // 获得当前指令所在的函数 fu
            std::string fu = i->getFunction()->getName().str();

            if (def_seen) { // 如果已经知晓全局变量的定义
              import_table[fu].insert( // 记录 fu 对 gv 包含的所有函数的 import 关系
                  nested_cbs.begin(), nested_cbs.end());
            } else { // 还没知道定义，标记记录使用了外部符号
              func_ext_constant_linkage[fu].insert(gv_name);
            }
            // 这里没进一步进行 wq.push_back(cu);
          } else if (llvm::Constant *cst = llvm::dyn_cast<llvm::Constant>(u)) {
            if (seen.find(cst)!=seen.end())
              continue;
            seen.insert(cst);
            //printf("DEBUG constant\n");
            
            // 广度优先搜索，往上查找 Inst
            for (auto cu : cst->users()) {
              wq.push_back(cu);
            }
          } else if (llvm::Operator *op = llvm::dyn_cast<llvm::Operator>(u)) {
            //printf("DEBUG Operator\n");
            assert(false);
          } else if (llvm::DerivedUser *drv = llvm::dyn_cast<llvm::DerivedUser>(u)) {
            assert(false);
          }
        }
      }
    }

    // 遍历所有代码，查找调用指令
    std::string mod = module->getName().str();
    for (auto func = module->begin(); func != module->end(); func++) { // 遍历所有函数
      for (auto bb = func->begin(); bb != func->end(); bb++) { // 遍历所有块
        for (auto inst = bb->begin(); inst != bb->end(); inst++) { // 遍历所有指令
          llvm::Instruction *i = &(*inst);
          if (llvm::isa<llvm::CallBase>(i)) { // CallBase 是 CallInst 和 InvokeInst 父类
            llvm::CallBase *call = llvm::dyn_cast<llvm::CallBase>(i);
            if (call->isInlineAsm())  continue;
            if (call->isIndirectCall())  continue; // 避免间接调用

            //  %call6 = call i32 null(ptr noundef %skb, ptr noundef %data.i)
            if (llvm::isa<llvm::ConstantPointerNull>(call->getCalledOperand())) continue;
            import_table[func->getName().str()].insert( // 建立函数和被调用函数导出关系图
                call->getCalledOperand()->getName().str());
          } else if (llvm::isa<llvm::StoreInst>(i)) { // 基于 StoreInst，记录被该函数间接调用的函数
            auto *si = llvm::dyn_cast<llvm::StoreInst>(i);
            auto *oprn = si->getValueOperand();
            if (allfunctions.find(oprn) != allfunctions.end()) {
              auto *ft = llvm::dyn_cast<llvm::Function>(oprn);
              auto fu = inst->getFunction()->getName().str();
              func_ptr_refer[fu].insert(ft->getName().str());
            }
          }
        }
      }

      // Collect Function Pointers
      import_table[func->getName().str()].merge(collectFP(&(*func), struct_def_cbs));
    }

    // 函数：其直接依赖的函数
    std::ofstream impout(line.substr(0, line.rfind('.')) + ".imptab");
    for (auto it : import_table) {
      for (auto imp : it.second) {
        impout << it.first << " : " << imp << "\n";
      }
    }

    // 全局变量：所包含的所有函数指针
    std::ofstream cbsout(line.substr(0, line.rfind('.')) + ".symcbs");
    for (auto it : struct_def_cbs) {
      for (auto cb : it.second) {
        cbsout << it.first << " : " << cb << "\n";
      }
    }

    // 函数：外部链接的常量符号的列表
    std::ofstream lnkout(line.substr(0, line.rfind('.')) + ".symlnk");
    for (auto it : func_ext_constant_linkage) {
      for (auto s : it.second) {
        lnkout << it.first << " : " << s << "\n";
      }
    }

    // 函数：函数体中间接调用的其他函数
    std::ofstream fptrout(line.substr(0, line.rfind('.')) + ".fptref");
    for (auto it : func_ptr_refer) {
      for (auto s : it.second) {
        fptrout << it.first << " : " << s << "\n";
      }
    }
  }

  return 0;
}
