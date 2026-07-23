#ifndef SAKURAE_TYPE_HPP
#define SAKURAE_TYPE_HPP

#include <llvm/IR/LLVMContext.h>
#include <map>

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <optional>

#include "includes/String.hpp"

namespace sakuraE::IR {
    class IRContext;
    enum IRTypeID {
        VoidTyID,
        Integer32TyID,
        Integer64TyID,
        IntegerNTyID,
        UInteger32TyID,
        UInteger64TyID,
        UIntegerNTyID,
        Float32TyID,
        Float64TyID,
        FloatNTyID,
        CharTyID,
        BoolTyID,
        TypeInfoTyID,
        StringTyID,
        // 复合类型
        RefTyID,
        PointerTyID,
        ArrayTyID,
        StructTyID,

        FunctionTyID,
        BlockTyID
    };

    static std::map<IRTypeID, int> rankList = {
        {BoolTyID, 1},
        {CharTyID, 2},
        {UInteger32TyID, 3},
        {Integer32TyID, 4},
        {UInteger64TyID, 5},
        {Integer64TyID, 6},
        {Float32TyID, 7},
        {Float64TyID, 8},
    };

    class IRType {
    protected:
        const IRTypeID irTypeID;
        explicit IRType(IRTypeID id) : irTypeID(id) {}

    public:
        virtual ~IRType() = default;

        IRType* unwrapPointer();
        IRType* getStorageType();
        IRTypeID getIRTypeID() const { return irTypeID; }
        bool isString() { return irTypeID == StringTyID; }
        bool isPointer() { return irTypeID == PointerTyID; }
        bool isRef() { return irTypeID == RefTyID; }
        bool isArray() { return irTypeID == ArrayTyID; }
        bool isStruct() { return irTypeID == StructTyID; }
        bool isComplexType() { return isString() || isPointer() || isArray() || isRef() || isStruct(); }
        bool isEqual(IRType* ty);

        virtual llvm::Type* toLLVMType(llvm::LLVMContext& ctx) = 0;
        virtual fzlib::String toString() = 0;

        static IRType* getVoidTy();
        static IRType* getBoolTy();
        static IRType* getCharTy();
        static IRType* getInt32Ty();
        static IRType* getInt64Ty();
        static IRType* getIntNTy(unsigned bitWidth);
        static IRType* getUInt32Ty();
        static IRType* getUInt64Ty();
        static IRType* getUIntNTy(unsigned bitWidth);
        static IRType* getFloat32Ty();
        static IRType* getFloat64Ty();
        static IRType* getTypeInfoTy();
        static IRType* getStringTy();
        static IRType* getPointerTo(IRType* elementType);
        static IRType* getRefTo(IRType* elementType);
        static IRType* getArrayTy(IRType* elementType, uint64_t numElements);

        static IRType* getBlockTy();
        static IRType* getFunctionTy(IRType* returnType, std::vector<IRType*> params);
    };

    class IRVoidType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRVoidType() : IRType(VoidTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRFloatType : public IRType {
        friend class IRType;
        friend class IRContext;
        unsigned bitWidth;

        IRFloatType(unsigned bw)
            : IRType([&]() -> IRTypeID{
                switch (bw) {
                    case 32: return IRTypeID::Float32TyID;
                    case 64: return IRTypeID::Float64TyID;
                    default: return IRTypeID::FloatNTyID;
                }
            }()), bitWidth(bw) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRIntegerType : public IRType {
        friend class IRType;
        friend class IRContext;
        bool isUnsigned = false;
        unsigned bitWidth;

        explicit IRIntegerType(unsigned bw, bool sign = true):
            IRType([&]()->IRTypeID {
                if (sign) {
                    switch (bw) {
                        case 1:
                            return IRTypeID::BoolTyID;
                        case 8:
                            return IRTypeID::CharTyID;
                        case 32:
                            return IRTypeID::Integer32TyID;
                        case 64:
                            return IRTypeID::Integer64TyID;
                        default:
                            return IRTypeID::IntegerNTyID;
                    }
                }
                else {
                    switch (bw) {
                        case 32:
                            return IRTypeID::UInteger32TyID;
                        case 64:
                            return IRTypeID::UInteger64TyID;
                        default:
                            return IRTypeID::UIntegerNTyID;
                    }
                }
            }()), bitWidth(bw) {}

    public:
        unsigned getBitWidth() const { return bitWidth; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRTypeInfoType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRTypeInfoType() : IRType(TypeInfoTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRStringType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRStringType() : IRType(StringTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRPointerType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRType* elementType;

        explicit IRPointerType(IRType* elementTy) : IRType(PointerTyID), elementType(elementTy) {}

    public:
        IRType* getElementType() const { return elementType; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRRefType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRType* elementType;

        explicit IRRefType(IRType* elementTy) : IRType(RefTyID), elementType(elementTy) {}

    public:
        IRType* getElementType() const { return elementType; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRArrayType : public IRType {
        friend class IRType;
        friend class IRContext;
        IRType* elementType;
        uint64_t numElements;

        // 私有构造函数
        IRArrayType(IRType* elementTy, uint64_t num)
            : IRType(ArrayTyID), elementType(elementTy), numElements(num) {}

    public:
        IRType* getElementType() const { return elementType; }
        uint64_t getNumElements() const { return numElements; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    // IR 内部实现
    class IRBlockType : public IRType {
        friend class IRType;
        friend class IRContext;

        explicit IRBlockType() : IRType(BlockTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
    };

    class IRFunctionType : public IRType {
        friend class IRType;
        friend class IRContext;

        std::vector<IRType*> paramsType;
        IRType* returnType;

        explicit IRFunctionType(IRType* ret, std::vector<IRType*> params)
            : IRType(FunctionTyID), paramsType(params), returnType(ret) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;
        IRType* getReturnType() { return returnType; }
    };

    class IRStructType : public IRType {
    public:
        struct FieldInfo {
            fzlib::String name;
            IRType* type;
        };

        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
        fzlib::String toString() override;

        const fzlib::String& getName() const;
        std::size_t getCount() const;
        std::optional<FieldInfo> findMember(const fzlib::String& target) const;
        const std::vector<FieldInfo>& getFields() const;
    private:
        friend class IRType;
        friend class NamingContext;
        friend class IRStructDecl;
        explicit IRStructType(fzlib::String modID, fzlib::String n, std::vector<FieldInfo> list):
            IRType(StructTyID), parentModID(modID), name(n), fields(std::move(list))
        {
            for (std::size_t i = 0; i < fields.size(); i ++) {
                fieldIndices[fields[i].name] = i;
            }
        }

        fzlib::String parentModID;
        fzlib::String name;
        std::map<fzlib::String, std::size_t> fieldIndices;
        std::vector<FieldInfo> fields;
    };

}

#endif /* !SAKURAE_TYPE_HPP */
