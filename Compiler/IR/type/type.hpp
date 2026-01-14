#ifndef SAKURAE_TYPE_HPP
#define SAKURAE_TYPE_HPP

#include <map>
#include <vector>
#include <cstdint> // For uint64_t

// Forward-declare LLVM types to avoid including heavy headers in this file.
namespace llvm {
    class Type;
    class LLVMContext;
}

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
    private:
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
    };

    class VoidType : public Type {
    private:
        friend class Type;
        VoidType() : Type(VoidTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class FloatType : public Type {
    private:
        friend class Type;
        FloatType() : Type(FloatTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class IntegerType : public Type {
    private:
        friend class Type;
        unsigned bitWidth;

        explicit IntegerType(unsigned bw) : Type(IntegerTyID), bitWidth(bw) {}

    public:
        unsigned getBitWidth() const { return bitWidth; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class PointerType : public Type {
    private:
        friend class Type;
        Type* elementType;

        explicit PointerType(Type* elementTy) : Type(PointerTyID), elementType(elementTy) {}

    public:
        Type* getElementType() const { return elementType; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class ArrayType : public Type {
    private:
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
} 

#endif //! SAKURAE_TYPE_HPP
