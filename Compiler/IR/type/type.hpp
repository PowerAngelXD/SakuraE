#ifndef SAKURAE_TYPE_HPP
#define SAKURAE_TYPE_HPP

#include <utility> 
#include <map>

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>

namespace sakuraE::IR {
    enum TypeID {
        VoidTyID,
        IntegerTyID,
        FloatTyID,
    
        PointerTyID,
        ArrayTyID,

        FunctionTyID,
        BlockTyID
    };

    class Type {
        const TypeID typeID;
    protected:
        explicit Type(TypeID id) : typeID(id) {}

    public:
        virtual ~Type() = default;

        TypeID getTypeID() const { return typeID; }

        bool operator== (Type* t) {
            return typeID == t->typeID;
        }

        bool operator!= (Type* t) {
            return !operator==(t);
        }

        virtual llvm::Type* toLLVMType(llvm::LLVMContext& ctx) = 0;

        static Type* getVoidTy();
        static Type* getBoolTy();
        static Type* getCharTy();
        static Type* getInt32Ty();
        static Type* getInt64Ty();
        static Type* getIntNTy(unsigned bitWidth);
        static Type* getFloatTy();
        static Type* getPointerTo(Type* elementType);
        static Type* getArrayTy(Type* elementType, uint64_t numElements);

        static Type* getBlockTy();
        static Type* getFunctionTy(Type* returnType, std::vector<Type*> params);
    };

    class VoidType : public Type {
        friend class Type;
        VoidType() : Type(VoidTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class FloatType : public Type {
        friend class Type;
        FloatType() : Type(FloatTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class IntegerType : public Type {
        friend class Type;
        unsigned bitWidth;

        explicit IntegerType(unsigned bw) : Type(IntegerTyID), bitWidth(bw) {}

    public:
        unsigned getBitWidth() const { return bitWidth; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class PointerType : public Type {
        friend class Type;
        Type* elementType;

        explicit PointerType(Type* elementTy) : Type(PointerTyID), elementType(elementTy) {}

    public:
        Type* getElementType() const { return elementType; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class ArrayType : public Type {
        friend class Type;
        Type* elementType;
        uint64_t numElements;

        // Private constructor
        ArrayType(Type* elementTy, uint64_t num) 
            : Type(ArrayTyID), elementType(elementTy), numElements(num) {}

    public:
        Type* getElementType() const { return elementType; }
        uint64_t getNumElements() const { return numElements; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    // IR Inside
    class BlockType : public Type {
        friend class Type;

        explicit BlockType() : Type(BlockTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class FunctionType : public Type {
        friend class Type;

        std::vector<Type*> paramsType;
        Type* returnType;

        explicit FunctionType(Type* ret, std::vector<Type*> params)
            : Type(FunctionTyID), paramsType(params), returnType(ret) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };
} 

#endif //! SAKURAE_TYPE_HPP
