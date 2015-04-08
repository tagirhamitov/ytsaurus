#include "user_defined_functions.h"

#include "cg_fragment_compiler.h"
#include "plan_helpers.h"

#include <new_table_client/row_base.h>
#include <new_table_client/llvm_types.h>

#include <llvm/Object/ObjectFile.h>

#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/IRReader/IRReader.h>

#include <llvm/Linker/Linker.h>

using namespace llvm;

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(llvm::Type* tp)
{
    std::string str;
    llvm::raw_string_ostream stream(str);
    tp->print(stream);
    return Stroka(stream.str());
}

void PushExecutionContext(
    TCGContext& builder,
    std::vector<Value*>& argumentValues)
{
    auto fullContext = builder.GetExecutionContextPtr();
    auto baseContextType = StructType::create(
        builder.getContext(),
        "struct.TExecutionContext");
    auto contextStruct = builder.CreateBitCast(
        fullContext,
        PointerType::getUnqual(baseContextType));
    argumentValues.push_back(contextStruct);
}

////////////////////////////////////////////////////////////////////////////////

void PushArgument(
    TCGContext& builder,
    std::vector<Value*>& argumentValues,
    TCGValue argumentValue)
{
    argumentValues.push_back(argumentValue.GetData());
    if (IsStringLikeType(argumentValue.GetStaticType())) {
        argumentValues.push_back(argumentValue.GetLength());
    }
}

TCGValue PropagateNullArguments(
    std::vector<TCodegenExpression>& codegenArgs,
    std::vector<Value*>& argumentValues,
    std::function<Value*(std::vector<Value*>)> codegenBody,
    std::function<TCGValue(Value*)> codegenReturn,
    EValueType type,
    const Stroka& name,
    TCGContext& builder,
    Value* row)
{
    if (codegenArgs.empty()) {
        auto llvmResult = codegenBody(argumentValues);
        return codegenReturn(llvmResult);
    } else {
        auto currentArgValue = codegenArgs.back()(builder, row);
        codegenArgs.pop_back();

        PushArgument(builder, argumentValues, currentArgValue);

        return CodegenIf<TCGContext, TCGValue>(
            builder,
            currentArgValue.IsNull(),
            [&] (TCGContext& builder) {
                return TCGValue::CreateNull(builder, type);
            },
            [&] (TCGContext& builder) {
                return PropagateNullArguments(
                    codegenArgs,
                    argumentValues,
                    std::move(codegenBody),
                    std::move(codegenReturn),
                    type,
                    name,
                    builder,
                    row);
            },
            Twine(name.c_str()));
    }
}

TCodegenExpression TSimpleCallingConvention::MakeCodegenFunctionCall(
    std::vector<TCodegenExpression> codegenArgs,
    std::function<Value*(std::vector<Value*>, TCGContext&)> codegenBody,
    EValueType type,
    const Stroka& name) const
{
    return [
        this_ = MakeStrong(this),
        codegenArgs = std::move(codegenArgs),
        codegenBody = std::move(codegenBody),
        type,
        name
    ] (TCGContext& builder, Value* row) mutable {
        std::reverse(
            codegenArgs.begin(),
            codegenArgs.end());

        auto llvmArgs = std::vector<Value*>();
        PushExecutionContext(builder, llvmArgs);

        auto callUdf = [&] (std::vector<Value*> argValues) {
            return codegenBody(argValues, builder);
        };

        std::function<TCGValue(Value*)> codegenReturn;
        if (IsStringLikeType(type)) {
            auto resultPointer = builder.CreateAlloca(
                TDataTypeBuilder::get(
                    builder.getContext(),
                    EValueType::String));
            llvmArgs.push_back(resultPointer);

            auto resultLength = builder.CreateAlloca(
                TTypeBuilder::TLength::get(builder.getContext()));
            llvmArgs.push_back(resultLength);

            codegenReturn = [&] (Value* llvmResult) {
                return TCGValue::CreateFromValue(
                    builder,
                    builder.getFalse(),
                    builder.CreateLoad(resultLength),
                    builder.CreateLoad(resultPointer),
                    type,
                    Twine(name.c_str()));
            };
        } else {
            codegenReturn = [&] (Value* llvmResult) {
                    return TCGValue::CreateFromValue(
                        builder,
                        builder.getFalse(),
                        nullptr,
                        llvmResult,
                        type);
            };
        }

        return PropagateNullArguments(
            codegenArgs,
            llvmArgs,
            callUdf,
            codegenReturn,
            type,
            name,
            builder,
            row);
    };
}

void TSimpleCallingConvention::CheckResultType(
    Type* llvmType,
    EValueType resultType,
    TCGContext& builder) const
{
    auto expectedResultType = TDataTypeBuilder::get(
        builder.getContext(),
        resultType);
    if (IsStringLikeType(resultType) &&
        llvmType != builder.getVoidTy())
    {
        THROW_ERROR_EXCEPTION(
            "Wrong result type in LLVM bitcode: expected void, got %Qv",
            llvmType);
    } else if (!IsStringLikeType(resultType) &&
        llvmType != expectedResultType)
    {
        THROW_ERROR_EXCEPTION(
            "Wrong result type in LLVM bitcode: expected %Qv, got %Qv",
            expectedResultType,
            llvmType);
    }
}

////////////////////////////////////////////////////////////////////////////////

TCodegenExpression TUnversionedValueCallingConvention::MakeCodegenFunctionCall(
    std::vector<TCodegenExpression> codegenArgs,
    std::function<Value*(std::vector<Value*>, TCGContext&)> codegenBody,
    EValueType type,
    const Stroka& name) const
{
    return [=] (TCGContext& builder, Value* row) {
        auto unversionedValueType =
            llvm::TypeBuilder<TUnversionedValue, false>::get(builder.getContext());
        auto unversionedValueOpaqueType = StructType::create(
            builder.getContext(),
            "struct.TUnversionedValue");

        auto argumentValues = std::vector<Value*>();

        PushExecutionContext(builder, argumentValues);

        auto resultPtr = builder.CreateAlloca(unversionedValueType);
        auto castedResultPtr = builder.CreateBitCast(
            resultPtr,
            PointerType::getUnqual(unversionedValueOpaqueType));
        argumentValues.push_back(castedResultPtr);

        for (auto arg = codegenArgs.begin(); arg != codegenArgs.end(); arg++) {
            auto valuePtr = builder.CreateAlloca(unversionedValueType);
            auto cgValue = (*arg)(builder, row);
            cgValue.StoreToValue(builder, valuePtr, 0);

            auto castedValuePtr = builder.CreateBitCast(
                valuePtr,
                PointerType::getUnqual(unversionedValueOpaqueType));
            argumentValues.push_back(castedValuePtr);
        }

        codegenBody(argumentValues, builder);

        return TCGValue::CreateFromLlvmValue(
            builder,
            resultPtr,
            type);
    };
}

void TUnversionedValueCallingConvention::CheckResultType(
    Type* llvmType,
    EValueType resultType,
    TCGContext& builder) const
{
    if (llvmType != builder.getVoidTy()) {
        THROW_ERROR_EXCEPTION(
            "Wrong result type in LLVM bitcode: expected void, got %Qv",
            llvmType);
    }
}

////////////////////////////////////////////////////////////////////////////////

TUserDefinedFunction::TUserDefinedFunction(
    const Stroka& functionName,
    std::vector<EValueType> argumentTypes,
    EValueType resultType,
    TSharedRef implementationFile,
    ECallingConvention callingConvention)
    : TTypedFunction(
        functionName,
        std::vector<TType>(argumentTypes.begin(), argumentTypes.end()),
        resultType)
    , FunctionName_(functionName)
    , ImplementationFile_(implementationFile)
    , ResultType_(resultType)
    , ArgumentTypes_(argumentTypes)
{
    switch (callingConvention) {
        case ECallingConvention::Simple:
            CallingConvention_ = New<TSimpleCallingConvention>();
            break;
        case ECallingConvention::UnversionedValue:
            CallingConvention_ = New<TUnversionedValueCallingConvention>();
            break;
        default:
            YUNREACHABLE();
    }
}

void TUserDefinedFunction::CheckCallee(
    llvm::Function* callee,
    TCGContext& builder,
    std::vector<Value*> argumentValues) const
{
    if (!callee) {
        THROW_ERROR_EXCEPTION(
            "Could not find LLVM bitcode for %Qv",
            FunctionName_);
    } else if (callee->arg_size() != argumentValues.size()) {
        THROW_ERROR_EXCEPTION(
            "Wrong number of arguments in LLVM bitcode: expected %v, got %v",
            argumentValues.size(),
            callee->arg_size());
    }

    CallingConvention_->CheckResultType(
        callee->getReturnType(),
        ResultType_,
        builder);

    auto i = 1;
    auto expected = argumentValues.begin();
    for (
        auto actual = callee->arg_begin();
        expected != argumentValues.end();
        expected++, actual++, i++)
    {
        if (actual->getType() != (*expected)->getType()) {
            THROW_ERROR_EXCEPTION(
                "Wrong type for argument %v in LLVM bitcode: expected %Qv, got %Qv",
                i,
                (*expected)->getType(),
                actual->getType());
        }
    }
}

Function* TUserDefinedFunction::GetLlvmFunction(TCGContext& builder) const
{
    auto module = builder.Module->GetModule();
    auto callee = module->getFunction(StringRef(FunctionName_));
    if (!callee) {
        auto diag = SMDiagnostic();
        auto buffer = MemoryBufferRef(
            StringRef(ImplementationFile_.Begin(), ImplementationFile_.Size()),
            StringRef("impl"));
        auto implModule = parseIR(buffer, diag, builder.getContext());

        if (!implModule) {
            THROW_ERROR_EXCEPTION(
                "Error parsing LLVM bitcode")
                << TError(Stroka(diag.getMessage().str()));
        }

        Linker::LinkModules(module, implModule.get());
        callee = module->getFunction(StringRef(FunctionName_));
    }
    return callee;
}

TCodegenExpression TUserDefinedFunction::MakeCodegenExpr(
    std::vector<TCodegenExpression> codegenArgs,
    EValueType type,
    const Stroka& name) const
{
    auto codegenBody = [
        this_ = MakeStrong(this)
    ] (std::vector<Value*> argumentValues, TCGContext& builder) {

        auto callee = this_->GetLlvmFunction(builder);
        this_->CheckCallee(callee, builder, argumentValues);
        auto result = builder.CreateCall(callee, argumentValues);
        return result;
    };

    return CallingConvention_->MakeCodegenFunctionCall(
        codegenArgs,
        codegenBody,
        type,
        name);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
