// Initialization (C17 6.7.9) from the semantic initializer lists of sema, string literals and compound literals.

#include <algorithm>

#include "ast/casting.hpp"
#include "interp/execution.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

void Execution::initialize(Address address, ast::QualType type, const ast::Expr* initializer, bool isZeroed) {
    initializer = ast::ignoreParens(initializer);
    const TypeInfo info = types_.describe(type);
    if (const auto* list = ast::dyn_cast<ast::InitListExpr>(initializer)) {
        if (info.representation == Representation::Array) {
            return initializeArray(address, type, list, isZeroed);
        }
        if (info.representation == Representation::Record) {
            return initializeRecord(address, type, list, isZeroed);
        }
        // A scalar compound literal keeps its list: the converted value, or nothing for zero (sema/README.md).
        if (list->elements().empty()) {
            (void)store({.address = address}, type, zeroOf(info), initializer);
            return;
        }
        return initialize(address, type, list->elements().front(), isZeroed);
    }
    if (ast::isa<ast::ImplicitZeroExpr>(initializer)) {
        if (!isZeroed) {
            (void)store({.address = address}, type, zeroOf(info), initializer);
        }
        return;
    }
    if (const auto* string = ast::dyn_cast<ast::StringLiteralExpr>(initializer);
        string != nullptr && info.representation == Representation::Array) {
        return initializeFromString(address, sizeOf(type, initializer), string);
    }
    if (info.isBytes()) {
        return storeRecord(address, info.size, initializer);
    }
    (void)store({.address = address}, type, rvalue(initializer), initializer);
}

// C17 6.7.9p21: elements without an initializer are zero. The list's own type has the length, also for a flexible
// array member that GNU lets static initializers fill.
void Execution::initializeArray(Address address, ast::QualType type, const ast::Expr* initializer, bool isZeroed) {
    const auto* list = ast::cast<ast::InitListExpr>(initializer);
    const ast::QualType arrayType = list->type().isNull() ? type : list->type();
    const ast::QualType element = TypeTable::array(arrayType)->element();
    const std::uint64_t elementSize = sizeOf(element, initializer);
    if (!isZeroed) {
        if (const auto filled = memory_.fill(address, std::byte{0}, sizeOf(arrayType, initializer)); !filled) {
            memoryError(filled.error(), true, initializer);
        }
    }
    const std::span<ast::Expr* const> elements = list->elements();
    if (elements.size() == 1 && types_.describe(element).representation != Representation::Array) {
        // `char s[] = {"abc"}`: the braced string initializes the whole array (C17 6.7.9p14).
        if (const auto* string = ast::dyn_cast<ast::StringLiteralExpr>(ast::ignoreParens(elements.front()))) {
            return initializeFromString(address, sizeOf(arrayType, initializer), string);
        }
    }
    for (std::size_t i = 0; i < elements.size(); ++i) {
        initialize(address + i * elementSize, element, elements[i], true);
    }
}

void Execution::initializeRecord(Address address, ast::QualType type, const ast::InitListExpr* list, bool isZeroed) {
    const ast::RecordDecl* record = TypeTable::record(type);
    const ast::RecordLayout& layout = sema::layoutOf(record);
    if (!isZeroed) {
        // Also zeroes the padding, as GCC does for a partly initialized aggregate.
        if (const auto filled = memory_.fill(address, std::byte{0}, layout.size); !filled) {
            memoryError(filled.error(), true, list);
        }
    }
    const auto member = [&](const ast::FieldDecl* field, const ast::Expr* initializer) {
        if (ast::isa<ast::ImplicitZeroExpr>(ast::ignoreParens(initializer))) {
            return;
        }
        const Address memberAddress = address + layout.fieldOffsets[field->index()];
        if (field->isBitField()) {
            (void)store({.address = memberAddress,
                         .bitWidth = field->bitWidth(),
                         .bitOffset = layout.bitOffsets[field->index()]},
                        field->type(), rvalue(initializer), initializer);
            return;
        }
        initialize(memberAddress, field->type(), initializer, true);
    };
    const std::span<ast::Expr* const> elements = list->elements();
    if (record->isUnion()) {
        if (list->unionField() != nullptr && !elements.empty()) {
            member(list->unionField(), elements.front());
        }
        return;
    }
    const std::span<ast::FieldDecl* const> fields = record->fields();
    for (std::size_t i = 0; i < elements.size() && i < fields.size(); ++i) {
        member(fields[i], elements[i]);
    }
}

// C17 6.7.9p14: successive characters initialize the elements; the terminating zero only if there is room. The rest
// of the array is zero.
void Execution::initializeFromString(Address address, std::uint64_t size, const ast::StringLiteralExpr* string) {
    writeBytes(address, stringBytes(string, size), string);
}

std::vector<std::byte> Execution::stringBytes(const ast::StringLiteralExpr* string, std::uint64_t size) const {
    const std::uint64_t unitSize = types_.describe(TypeTable::array(string->type())->element()).size;
    std::vector<std::byte> bytes(size);
    const std::span<const std::uint32_t> units = string->codeUnits();
    for (std::size_t i = 0; i < units.size() && (i + 1) * unitSize <= size; ++i) {
        for (std::uint64_t b = 0; b < unitSize; ++b) {
            bytes[i * unitSize + b] = static_cast<std::byte>((units[i] >> (8 * b)) & 0xFF); // little-endian
        }
    }
    return bytes;
}

Address Execution::stringAddress(const ast::StringLiteralExpr* string) {
    const auto found = code_.stringIds.find(string);
    if (found == code_.stringIds.end()) {
        stop(Termination::Unsupported, "a string literal outside the program", string->location());
    }
    return stringAddresses_[found->second];
}

// C17 6.5.2.5p5: a compound literal is an object, initialized each time the expression is evaluated; at file scope it
// has static storage, in a function it lives in the frame (see FunctionCode::compoundLiterals).
LValue Execution::compoundLiteral(const ast::CompoundLiteralExpr* literal) {
    Address address = 0;
    if (const auto found = code_.fileScopeLiteralIds.find(literal); found != code_.fileScopeLiteralIds.end()) {
        address = fileScopeLiterals_[found->second];
    } else if (frame_ != nullptr) {
        const auto local = frame_->body->compoundLiteralIds.find(literal);
        if (local != frame_->body->compoundLiteralIds.end()) {
            address = frame_->literals[local->second];
        }
    }
    if (address == 0) {
        stop(Termination::Unsupported, "a compound literal outside the program", literal->location());
    }
    if (isEvaluatingConstraint_) {
        stop(Termination::Unsupported, "a constraint must not contain a compound literal", literal->location());
    }
    initialize(address, literal->type(), literal->initializer(), false);
    return {.address = address};
}

} // namespace nitwit::interp
