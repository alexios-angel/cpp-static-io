#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include <fstream>
#include <bit>

using namespace clang;

namespace {

class StaticFunctionVisitor : public RecursiveASTVisitor<StaticFunctionVisitor> {
public:
    explicit StaticFunctionVisitor(ASTContext *Context) : Context(Context) {}

    bool VisitCallExpr(CallExpr *CE) {
        if (FunctionDecl *FD = CE->getDirectCallee()) {
            if (FD->getNameInfo().getName().getAsString() == "static_write") {
                if (CE->getNumArgs() == 2) {
                    EvaluateStaticWrite(CE);
                }
            }
        }
        return true;
    }

private:
    ASTContext *Context;

    std::basic_string<uint8_t> GetString(const Expr *string_expr) {
        // Handle string literals
        if (const StringLiteral *SL = dyn_cast<StringLiteral>(string_expr)) {
            llvm::StringRef Str = SL->getString();
            // Convert the string to std::basic_string<uint8_t>
            return std::basic_string<uint8_t>(Str.bytes_begin(), Str.bytes_end());
        }

        // Initialize the result string with the array size
        std::basic_string<uint8_t> Result;

        if (const auto *Init = dyn_cast<InitListExpr>(string_expr)) {
            Result.reserve(Init->getNumInits());
            for (unsigned i = 0; i < Init->getNumInits(); ++i) {
                if (const Expr * I = Init->getInit(i)) {
                    const Expr * E = I->IgnoreImpCasts();
                    if (const auto *IntLit = dyn_cast<IntegerLiteral>(E)) {
                        llvm::APInt Value = IntLit->getValue();
                        const uint64_t num = Value.getZExtValue();
                        // This is where errors could occur!!
                        // This is where uint64_t is narrowed to uint8_t
                        Result.push_back(static_cast<uint8_t>(num));
                    } else if (const auto *CharLit = dyn_cast<CharacterLiteral>(E)) {
                        Result.push_back(static_cast<uint8_t>(CharLit->getValue()));
                    // If it is a std::array then it would be a InitListExprByte;
                    // Check if it is apart of InitListExpr and if so recurse down
                    // into GetString
                    } else if(const auto *ListExpr = dyn_cast<InitListExpr>(E)) {
                        return GetString(E);
                    } else {
                        llvm::errs() << "Unsupported array element type " << E->getStmtClassName();
                        return {};
                    }
                } else {
                    llvm::errs() << "This shouldn't trigger a bug but handling anyway\n";
                    return {};
                }
            }
        } else {
            llvm::errs() << "Unknown string expression type " << string_expr->getStmtClassName();
        }
        return Result;
    }

    std::basic_string<uint8_t> EvaluateString(const Expr *string_expr){
        string_expr = string_expr->IgnoreImpCasts();
        
        Expr::EvalResult Result;
        if (string_expr->EvaluateAsConstantExpr(Result, *Context)) {
            if (Result.Val.isLValue()) {
                if (const clang::APValue::LValueBase base = Result.Val.getLValueBase()) {
                    if(const ValueDecl *D = base.dyn_cast<const ValueDecl*>()){
                        if (const clang::VarDecl *VD = llvm::dyn_cast<clang::VarDecl>(D)) {
                            if (const clang::Expr *Init = VD->getInit()) {
                                return GetString(Init);
                            }
                        }
                    } else if (const Expr *E = base.dyn_cast<const Expr*>()) {
                        return GetString(E);
                    } else {
                        llvm::errs() << "Unknown LValueBase type\n";
                        return {};
                    }
                } else {
                    llvm::errs() << "LValue has no base\n";
                    return {};
                }
            } 
        } else {
            llvm::errs() << "Failed to evaluate as constant expression\n";
            return {};
        }

        return {};
    }

    void EvaluateStaticWrite(CallExpr *CE) {
        const Expr *fnameExpr = CE->getArg(0);
        const Expr *dataExpr = CE->getArg(1);

        auto fname = EvaluateString(fnameExpr);
        if (fname.empty()) {
            llvm::errs() << "Filename is not valid or could not be resolved.\n";
            return;
        }
        const char * fname_c_str = reinterpret_cast<const char *>(fname.data());
        llvm::errs() << "Filename evaluated: " << fname_c_str << "\n";

        auto byteArray = EvaluateString(dataExpr);

        llvm::errs() << "Byte array: {";
        for (const auto &c : byteArray) {
            llvm::errs() << (int)c << ",";
        }
        llvm::errs() << "}\n";

        return;

        if (!byteArray.empty()) {
            std::ofstream outFile(fname_c_str, std::ios::binary | std::ios::app);
            if (outFile.is_open()) {
                outFile.write(reinterpret_cast<const char *>(byteArray.data()), byteArray.size());
                outFile.close();
                llvm::errs() << "Data written to file: " << fname_c_str << "\n";
            } else {
                llvm::errs() << "Could not open file " << fname_c_str << " for writing.\n";
            }
        } else {
            llvm::errs() << "No data to write.\n";
        }
    }
};

class StaticWriteASTConsumer : public ASTConsumer {
public:
    explicit StaticWriteASTConsumer(ASTContext *Context)
        : Visitor(Context) {}

    virtual void HandleTranslationUnit(ASTContext &Context) override {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    StaticFunctionVisitor Visitor;
};

class StaticWriteAction : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
        return std::make_unique<StaticWriteASTConsumer>(&CI.getASTContext());
    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return PluginASTAction::AddAfterMainAction;
    }
};

} // namespace

// Register the plugin with Clang
static FrontendPluginRegistry::Add<StaticWriteAction>
    X("staticio", "Evaluates constexpr byte array and writes/reads a file at compile time");