#ifndef SAKURAE_TYPE_HPP
#define SAKURAE_TYPE_HPP

#include <utility> 
#include <map>

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>

namespace sakuraE::IR {
    enum IRTypeID {
        VoidTyID,
        IntegerTyID,
        FloatTyID,
    
        PointerTyID,
        ArrayTyID,

        FunctionTyID,
        BlockTyID
    };

    class IRType {
        const IRTypeID irTypeID;
    protected:
        explicit IRType(IRTypeID id) : irTypeID(id) {}

    public:
        virtual ~IRType() = default;

        IRTypeID getIRTypeID() const { return irTypeID; }

        bool operator== (IRType* t) {
            return irTypeID == t->irTypeID;
        }

        bool operator!= (IRType* t) {
            return !operator==(t);
        }

        virtual llvm::Type* toLLVMType(llvm::LLVMContext& ctx) = 0;

        static IRType* getVoidTy();
        static IRType* getBoolTy();
        static IRType* getCharTy();
        static IRType* getInt32Ty();
        static IRType* getInt64Ty();
        static IRType* getIntNTy(unsigned bitWidth);
        static IRType* getFloatTy();
        static IRType* getPointerTo(IRType* elementType);
        static IRType* getArrayTy(IRType* elementType, uint64_t numElements);

        static IRType* getBlockTy();
        static IRType* getFunctionTy(IRType* returnType, std::vector<IRType*> params);
    };

    class VoidType : public IRType {
        friend class IRType;
        VoidType() : IRType(VoidTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class FloatType : public IRType {
        friend class IRType;
        FloatType() : IRType(FloatTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class IntegerType : public IRType {
        friend class IRType;
        unsigned bitWidth;

        explicit IntegerType(unsigned bw) : IRType(IntegerTyID), bitWidth(bw) {}

    public:
        unsigned getBitWidth() const { return bitWidth; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class PointerType : public IRType {
        friend class IRType;
        IRType* elementType;

        explicit PointerType(IRType* elementTy) : IRType(PointerTyID), elementType(elementTy) {}

    public:
        IRType* getElementType() const { return elementType; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class ArrayType : public IRType {
        friend class IRType;
        IRType* elementType;
        uint64_t numElements;

        // Private constructor
        ArrayType(IRType* elementTy, uint64_t num) 
            : IRType(ArrayTyID), elementType(elementTy), numElements(num) {}

    public:
        IRType* getElementType() const { return elementType; }
        uint64_t getNumElements() const { return numElements; }
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    // IR Inside
    class BlockType : public IRType {
        friend class IRType;

        explicit BlockType() : IRType(BlockTyID) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };

    class FunctionType : public IRType {
        friend class IRType;

        std::vector<IRType*> paramsType;
        IRType* returnType;

        explicit FunctionType(IRType* ret, std::vector<IRType*> params)
            : IRType(FunctionTyID), paramsType(params), returnType(ret) {}
    public:
        llvm::Type* toLLVMType(llvm::LLVMContext& ctx) override;
    };
} 

#endif //! SAKURAE_TYPE_HPP
