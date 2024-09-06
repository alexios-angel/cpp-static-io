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

    std::string EvaluateFilename(const Expr *fnameExpr) {
        fnameExpr = fnameExpr->IgnoreImpCasts();

        if (const StringLiteral *SL = dyn_cast<StringLiteral>(fnameExpr)) {
            return SL->getString().str();
        }

        if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(fnameExpr)) {
            if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                const Expr *InitExpr = VD->getInit();
                if (const StringLiteral *SL = dyn_cast<StringLiteral>(InitExpr)) {
                    return SL->getString().str();
                }
            }
        }

        llvm::errs() << "Filename expression could not be resolved.\n";
        return "";
    }

std::vector<uint8_t> EvaluateData(const Expr *dataExpr) {
    std::vector<uint8_t> byteArray;
    dataExpr = dataExpr->IgnoreImpCasts();

    llvm::errs() << "Evaluating data expression of type: " << dataExpr->getStmtClassName() << "\n";

    if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(dataExpr)) {
        if (const ValueDecl *VD = DRE->getDecl()) {
            return EvaluateValueDecl(VD);
        }
    }

    // If not a DeclRefExpr, proceed with general constant expression evaluation
    Expr::EvalResult Result;
    if (dataExpr->EvaluateAsConstantExpr(Result, *Context)) {
        byteArray = EvaluateConstantExpr(Result);
    } else {
        llvm::errs() << "Data expression could not be evaluated as a constant expression.\n";
    }

    llvm::errs() << "Evaluated byte array size: " << byteArray.size() << "\n";
    return byteArray;
}

std::vector<uint8_t> EvaluateValueDecl(const ValueDecl *VD) {
    llvm::errs() << "Evaluating ValueDecl: " << VD->getNameAsString() << "\n";

    if (const VarDecl *Var = dyn_cast<VarDecl>(VD)) {
        llvm::errs() << "VarDecl kind: " << Var->getKind() << "\n";
        llvm::errs() << "Is constexpr: " << Var->isConstexpr() << "\n";
        llvm::errs() << "Is static data member: " << Var->isStaticDataMember() << "\n";

        // Handle static data member of a template specialization
        if (Var->isStaticDataMember()) {
            return EvaluateStaticMember(Var);
        }

        if (Var->hasInit()) {
            llvm::errs() << "VarDecl has initializer\n";
            const Expr *InitExpr = Var->getInit();
            Expr::EvalResult Result;
            if (InitExpr->EvaluateAsConstantExpr(Result, *Context)) {
                return EvaluateConstantExpr(Result);
            } else {
                llvm::errs() << "Failed to evaluate initializer as constant expression\n";
            }
        } else {
            llvm::errs() << "VarDecl has no initializer\n";
        }

        // Try to evaluate the value
        if (const APValue *Value = Var->evaluateValue()) {
            llvm::errs() << "Successfully evaluated VarDecl value\n";
            Expr::EvalResult Result;
            Result.Val = *Value;
            return EvaluateConstantExpr(Result);
        } else {
            llvm::errs() << "Failed to evaluate VarDecl value\n";
        }
    } else {
        llvm::errs() << "ValueDecl is not a VarDecl\n";
    }

    llvm::errs() << "Failed to evaluate ValueDecl.\n";
    return {};
}

std::vector<uint8_t> EvaluateStaticMember(const VarDecl *Var) {
    const DeclContext *DC = Var->getDeclContext();
    while (DC) {
        if (const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(DC)) {
            if (const ClassTemplateSpecializationDecl *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
                llvm::errs() << "Found template specialization: " << CTSD->getNameAsString() << "\n";
                return EvaluateTemplateSpecialization(CTSD, Var);
            }
        }
        DC = DC->getParent();
    }
    llvm::errs() << "Failed to find template specialization\n";
    return {};
}

std::vector<uint8_t> EvaluateTemplateSpecialization(const ClassTemplateSpecializationDecl *CTSD, const VarDecl *Var) {
    llvm::errs() << "Evaluating template specialization: " << CTSD->getNameAsString() << "\n";
    llvm::errs() << "Static member name: " << Var->getNameAsString() << "\n";

    std::vector<uint8_t> byteArray;

    // Try to evaluate the static member directly
    llvm::errs() << "Attempting to evaluate static member directly...\n";
    Expr::EvalResult Result;
    if (Var->hasInit() && Var->getInit()->EvaluateAsConstantExpr(Result, *Context)) {
        llvm::errs() << "Successfully evaluated static member initialization\n";
        return EvaluateConstantExpr(Result);
    }

    llvm::errs() << "Direct evaluation of static member failed, falling back to template arguments\n";

    // If direct evaluation fails, try to use the template arguments
    const TemplateArgumentList &Args = CTSD->getTemplateArgs();
    llvm::errs() << "Template has " << Args.size() << " arguments\n";

    for (unsigned i = 0; i < Args.size(); ++i) {
        const TemplateArgument &Arg = Args[i];
        llvm::errs() << "Processing template argument " << i << ", kind: " << Arg.getKind() << "\n";

        if (Arg.getKind() == TemplateArgument::Integral) {
            llvm::APSInt Value = Arg.getAsIntegral();
            std::string Str;
            llvm::raw_string_ostream OS(Str);
            Value.print(OS, Value.isSigned());
            OS.flush();
            llvm::errs() << "Evaluated template argument " << i << " to: " << Str << "\n";
            byteArray.insert(byteArray.end(), Str.begin(), Str.end());
        } else {
            llvm::errs() << "Template argument " << i << " is not an integral\n";
        }
    }

    if (byteArray.empty()) {
        llvm::errs() << "Failed to evaluate template specialization\n";
    } else {
        llvm::errs() << "Successfully evaluated template specialization\n";
    }
    return byteArray;
}

std::vector<uint8_t> EvaluateConstantExpr(const Expr::EvalResult &Result) {
    std::vector<uint8_t> byteArray;
    if (Result.Val.isInt()) {
        llvm::APSInt Value = Result.Val.getInt();
        std::string Str;
        llvm::raw_string_ostream OS(Str);
        Value.print(OS, Value.isSigned());
        OS.flush();
        byteArray.assign(Str.begin(), Str.end());
        llvm::errs() << "Evaluated integer value: " << Str << "\n";
    } else if (Result.Val.isFloat()) {
        llvm::APFloat Value = Result.Val.getFloat();
        llvm::SmallString<32> Str;
        Value.toString(Str);
        byteArray.assign(Str.begin(), Str.end());
        llvm::errs() << "Evaluated float value: " << Str << "\n";
    } else if (Result.Val.isLValue()) {
        if (Result.Val.getLValueBase()) {
            if (const Expr *E = Result.Val.getLValueBase().dyn_cast<const Expr*>()) {
                if (const StringLiteral *SL = dyn_cast<StringLiteral>(E)) {
                    llvm::StringRef Str = SL->getString();
                    byteArray.assign(Str.begin(), Str.end());
                    llvm::errs() << "Evaluated string literal: " << Str << "\n";
                } else if (const ArrayType *AT = E->getType()->getAsArrayTypeUnsafe()) {
                    const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT);
                    if (CAT) {
                        uint64_t ArraySize = CAT->getSize().getZExtValue();
                        llvm::errs() << "Found array with size: " << ArraySize << "\n";
                        
                        // Try to evaluate the array expression
                        Expr::EvalResult ArrayResult;
                        if (E->EvaluateAsConstantExpr(ArrayResult, *Context)) {
                            if (ArrayResult.Val.isArray()) {
                                for (uint64_t i = 0; i < ArraySize; ++i) {
                                    const APValue &Element = ArrayResult.Val.getArrayInitializedElt(i);
                                    if (Element.isInt()) {
                                        byteArray.push_back(static_cast<uint8_t>(Element.getInt().getLimitedValue()));
                                    }
                                }
                                llvm::errs() << "Evaluated array with " << ArraySize << " elements\n";
                            } else {
                                llvm::errs() << "Array evaluation did not result in an array value\n";
                            }
                        } else {
                            llvm::errs() << "Failed to evaluate array expression\n";
                        }
                    } else {
                        llvm::errs() << "Array is not a constant array type\n";
                    }
                } else {
                    llvm::errs() << "LValue is neither a string literal nor an array\n";
                }
            } else {
                llvm::errs() << "LValue base is not an Expr\n";
            }
        } else {
            llvm::errs() << "LValue has no base\n";
        }
        if (byteArray.empty()) {
            llvm::errs() << "LValue could not be evaluated to a byte array.\n";
        }
    } else if (Result.Val.isArray()) {
        unsigned NumElements = Result.Val.getArrayInitializedElts();
        for (unsigned i = 0; i < NumElements; ++i) {
            const APValue &Element = Result.Val.getArrayInitializedElt(i);
            if (Element.isInt()) {
                byteArray.push_back(static_cast<uint8_t>(Element.getInt().getLimitedValue()));
            }
        }
        llvm::errs() << "Evaluated array with " << NumElements << " elements\n";
    } else {
        llvm::errs() << "Unsupported constant expression type\n";
    }
    return byteArray;
}

    void EvaluateStaticWrite(CallExpr *CE) {
        const Expr *fnameExpr = CE->getArg(0);
        const Expr *dataExpr = CE->getArg(1);

        llvm::errs() << "Evaluating static_write call\n";

        std::string fname = EvaluateFilename(fnameExpr);
        if (fname.empty()) {
            llvm::errs() << "Filename is not valid or could not be resolved.\n";
            return;
        }
        llvm::errs() << "Filename evaluated: " << fname << "\n";

        std::vector<uint8_t> byteArray = EvaluateData(dataExpr);

        llvm::errs() << "Byte array: {";
        for (const auto &c : byteArray) {
            llvm::errs() << (int)c << ",";
        }
        llvm::errs() << "}\n";

        if (!byteArray.empty()) {
            std::ofstream outFile(fname, std::ios::binary | std::ios::app);
            if (outFile.is_open()) {
                outFile.write(reinterpret_cast<const char *>(byteArray.data()), byteArray.size());
                outFile.close();
                llvm::errs() << "Data written to file: " << fname << "\n";
            } else {
                llvm::errs() << "Could not open file " << fname << " for writing.\n";
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
    X("static-write", "Evaluates constexpr byte array in static_write and writes to a file at compile time");