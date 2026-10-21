// Initializers (C17 6.7.9): the semantic form of initializer lists described in ast/expr.hpp, with designators and
// brace elision resolved, and arrays of unknown size completed from their initializer.
//
// A braced list is walked with a cursor over its elements. Each aggregate being initialized is built as a tree of
// slots first, because a later designator may change part of an aggregate initialized before (C17 6.7.9p19); the
// semantic InitListExprs are made from the finished tree.

#include <format>
#include <memory>
#include <optional>
#include <vector>

#include "ast/casting.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"
#include "support/assert.hpp"

namespace nitwit::sema::detail {

namespace {

// The number of array elements an initializer may reach. The semantic form has an element for each index up to the
// last one initialized, so a designator like `[60000000] = 1` would need hundreds of megabytes; it is rejected
// before any of them is made.
constexpr std::uint64_t maxInitializedElements = std::uint64_t{1} << 20;

} // namespace

class InitializerBuilder {
public:
    InitializerBuilder(Sema& sema, bool isStatic) : sema_(sema), context_(sema.context_), isStatic_(isStatic) {}

    ast::Expr* initialize(ast::QualType& type, ast::Expr* init);

private:
    struct Aggregate;

    struct Slot {
        ast::Expr* value = nullptr;           // a converted initializer
        std::unique_ptr<Aggregate> aggregate; // or a nested aggregate built here

        [[nodiscard]] bool isEmpty() const { return value == nullptr && aggregate == nullptr; }
    };

    struct Aggregate {
        ast::QualType type;
        ast::RecordDecl* record = nullptr;      // struct or union
        ast::QualType element;                  // array
        std::optional<std::uint64_t> arraySize; // array of known size
        std::vector<Slot> slots;                // struct, union: one per field; array: by index, as initialized
        ast::FieldDecl* unionField = nullptr;
        ast::InitListExpr* list = nullptr; // the braced list it comes from, reused for the semantic form
        SourceRange range;

        [[nodiscard]] bool isUnion() const { return record != nullptr && record->isUnion(); }
    };

    // The elements of one braced list. After a designator, `pending` is the value it designates.
    struct Cursor {
        std::span<ast::Expr*> elements;
        std::size_t index = 0;
        ast::Expr* pending = nullptr;

        [[nodiscard]] bool atEnd() const { return pending == nullptr && index >= elements.size(); }
        [[nodiscard]] ast::Expr* current() const { return pending != nullptr ? pending : elements[index]; }
        [[nodiscard]] bool atDesignator() const {
            return pending == nullptr && ast::isa<ast::DesignatedInitExpr>(elements[index]);
        }
        void next() {
            pending = nullptr;
            ++index;
        }
    };

    ast::Expr* braced(ast::QualType& type, ast::InitListExpr* list);
    ast::Expr* scalar(ast::QualType type, ast::Expr* value);
    ast::Expr* string(ast::QualType& type, ast::StringLiteralExpr* literal);
    [[nodiscard]] ast::StringLiteralExpr* stringFor(ast::QualType type, ast::Expr* value);
    [[nodiscard]] bool isScalarLike(ast::QualType type);
    ast::Expr* zero(ast::QualType type, SourceRange range);

    std::unique_ptr<Aggregate> makeAggregate(ast::QualType type, ast::InitListExpr* list, SourceRange range);
    void fill(Aggregate& aggregate, Cursor& cursor, bool braced, std::uint64_t position);
    std::uint64_t designate(Aggregate& aggregate, Cursor& cursor, std::span<const ast::Designator> designators,
                            std::size_t index);
    std::uint64_t designateField(Aggregate& aggregate, Cursor& cursor, std::span<ast::FieldDecl* const> path,
                                 std::span<const ast::Designator> designators, std::size_t index);
    std::uint64_t arrayIndex(const Aggregate& aggregate, ast::Expr* expr);
    void initializeSubobject(Aggregate& aggregate, std::uint64_t position, Cursor& cursor);
    void initializeRange(Aggregate& aggregate, std::uint64_t first, std::uint64_t last, Cursor& cursor);
    Aggregate& subAggregate(Aggregate& aggregate, std::uint64_t position, SourceLocation location);
    Slot& slot(Aggregate& aggregate, std::uint64_t position, SourceLocation location);
    [[nodiscard]] ast::QualType subobjectType(const Aggregate& aggregate, std::uint64_t position) const;
    [[nodiscard]] std::uint64_t skipUnnamedBitFields(const Aggregate& aggregate, std::uint64_t position) const;
    [[nodiscard]] bool isFull(const Aggregate& aggregate, std::uint64_t position) const;
    [[nodiscard]] std::uint64_t nextPosition(const Aggregate& aggregate, std::uint64_t position) const;
    ast::Expr* materialize(Aggregate& aggregate);

    Sema& sema_;
    ast::AstContext& context_;
    bool isStatic_; // the initializer of an object with static storage duration
};

ast::Expr* InitializerBuilder::initialize(ast::QualType& type, ast::Expr* init) {
    if (auto* list = ast::dyn_cast<ast::InitListExpr>(init)) {
        return braced(type, list);
    }
    if (ast::StringLiteralExpr* literal = stringFor(type, init)) {
        return string(type, literal);
    }
    const TypeClass typeClass = classify(context_, type);
    if (typeClass == TypeClass::Array) {
        sema_.expr(init);
        sema_.error(init->location(), "array initialized from non-constant array expression");
    }
    if (typeClass == TypeClass::Record) {
        sema_.requireComplete(type, init->location(), "variable has initializer but incomplete type");
    }
    return scalar(type, init);
}

bool InitializerBuilder::isScalarLike(ast::QualType type) {
    const TypeClass typeClass = classify(context_, type);
    return isScalarClass(typeClass) || typeClass == TypeClass::VaList || typeClass == TypeClass::Complex;
}

ast::Expr* InitializerBuilder::zero(ast::QualType type, SourceRange range) {
    auto* zero = context_.create<ast::ImplicitZeroExpr>(range);
    zero->setType(type, ast::ValueCategory::RValue);
    return zero;
}

ast::Expr* InitializerBuilder::scalar(ast::QualType type, ast::Expr* value) {
    if (auto* list = ast::dyn_cast<ast::InitListExpr>(value)) {
        return braced(type, list);
    }
    sema_.expr(value);
    const AssignmentContext context{.kind = AssignmentContext::Kind::Initialization,
                                    .location = value->location(),
                                    .argumentIndex = 0,
                                    .function = nullptr};
    return sema_.convertForAssignment(value, type, context);
}

// A string literal, possibly in parentheses, that initializes an array of character type (C17 6.7.9p14-15).
ast::StringLiteralExpr* InitializerBuilder::stringFor(ast::QualType type, ast::Expr* value) {
    const ast::ArrayType* array = arrayOf(type);
    auto* literal = ast::dyn_cast<ast::StringLiteralExpr>(ast::ignoreParens(value));
    if (array == nullptr || literal == nullptr) {
        return nullptr;
    }
    const TypeClass element = classify(context_, array->element());
    return element == TypeClass::Integer ? literal : nullptr;
}

ast::Expr* InitializerBuilder::string(ast::QualType& type, ast::StringLiteralExpr* literal) {
    sema_.expr(literal);
    const ast::ArrayType* array = arrayOf(type);
    const ast::QualType element = array->element();
    bool matches = false;
    switch (literal->encoding()) {
    case lex::CharEncoding::Plain:
    case lex::CharEncoding::Utf8:
        matches = isCharacterType(context_, element);
        break;
    case lex::CharEncoding::Wide:
        matches = compatibleUnqualified(context_, element, wcharType(context_));
        break;
    case lex::CharEncoding::Utf16:
        matches = compatibleUnqualified(context_, element, context_.builtin(ast::BuiltinKind::UnsignedShort));
        break;
    case lex::CharEncoding::Utf32:
        matches = compatibleUnqualified(context_, element, context_.builtin(ast::BuiltinKind::UnsignedInt));
        break;
    }
    if (!matches) {
        sema_.error(literal->location(), "array of inappropriate type initialized from string constant");
    }
    const std::uint64_t length = literal->codeUnits().size();
    switch (array->sizeKind()) {
    case ast::ArrayType::SizeKind::Incomplete:
        type = context_.constantArrayOf(element, length + 1).withQualifiers(type.qualifiers());
        break;
    case ast::ArrayType::SizeKind::Constant:
        // C17 6.7.9p14: the terminating zero is dropped if there is no room for it.
        if (length > array->size()) {
            sema_.warning(literal->location(), "initializer-string for array is too long");
        }
        break;
    default:
        sema_.error(literal->location(), "variable-sized object may not be initialized");
    }
    return literal;
}

ast::Expr* InitializerBuilder::braced(ast::QualType& type, ast::InitListExpr* list) {
    const std::span<ast::Expr*> elements = list->elements();
    if (isScalarLike(type)) {
        // C17 6.7.9p11: the initializer of a scalar is a single expression, optionally in braces. GNU: `{}` is zero.
        if (elements.empty()) {
            return zero(unqualified(context_, type), list->range());
        }
        if (ast::isa<ast::DesignatedInitExpr>(elements.front())) {
            sema_.error(elements.front()->location(), "designator in initializer for scalar type");
        }
        if (elements.size() > 1) {
            sema_.warning(elements[1]->location(), "excess elements in scalar initializer");
        }
        if (ast::isa<ast::InitListExpr>(elements.front())) {
            sema_.warning(elements.front()->location(), "braces around scalar initializer");
        }
        return scalar(type, elements.front());
    }
    if (elements.size() == 1) {
        if (ast::StringLiteralExpr* literal = stringFor(type, elements.front())) {
            return string(type, literal);
        }
    }
    const TypeClass typeClass = classify(context_, type);
    if (typeClass != TypeClass::Array && typeClass != TypeClass::Record) {
        sema_.error(list->location(), "invalid initializer");
    }
    const std::unique_ptr<Aggregate> aggregate = makeAggregate(type, list, list->range());
    Cursor cursor{.elements = elements, .index = 0, .pending = nullptr};
    fill(*aggregate, cursor, true, 0);
    ast::Expr* result = materialize(*aggregate);
    type = aggregate->type;
    return result;
}

std::unique_ptr<InitializerBuilder::Aggregate>
InitializerBuilder::makeAggregate(ast::QualType type, ast::InitListExpr* list, SourceRange range) {
    auto aggregate = std::make_unique<Aggregate>();
    aggregate->type = type;
    aggregate->list = list;
    aggregate->range = range;
    if (ast::RecordDecl* record = recordOf(type)) {
        sema_.requireComplete(type, range.begin, "variable has initializer but incomplete type");
        aggregate->record = record;
        aggregate->slots.resize(record->fields().size());
        return aggregate;
    }
    const ast::QualType desugared = desugar(type);
    const auto* array = ast::cast<ast::ArrayType>(desugared.type());
    aggregate->element = array->element().withQualifiers(desugared.qualifiers());
    switch (array->sizeKind()) {
    case ast::ArrayType::SizeKind::Constant:
        aggregate->arraySize = array->size();
        break;
    case ast::ArrayType::SizeKind::Incomplete:
        break;
    default:
        sema_.error(range.begin, "variable-sized object may not be initialized");
    }
    return aggregate;
}

// Fills `aggregate` from the cursor, starting at `position`. The aggregate of a braced list handles designators and
// runs to the end of the list; an aggregate whose braces are elided stops at a designator or when it is full.
void InitializerBuilder::fill(Aggregate& aggregate, Cursor& cursor, bool braced, std::uint64_t position) {
    while (!cursor.atEnd()) {
        if (cursor.atDesignator()) {
            if (!braced) {
                return;
            }
            auto* designated = ast::cast<ast::DesignatedInitExpr>(cursor.elements[cursor.index]);
            cursor.pending = designated->initializer();
            position = designate(aggregate, cursor, designated->designators(), 0);
            continue;
        }
        position = skipUnnamedBitFields(aggregate, position);
        if (isFull(aggregate, position)) {
            if (!braced) {
                return;
            }
            sema_.warning(cursor.current()->location(), "excess elements in initializer");
            cursor.pending = nullptr;
            cursor.index = cursor.elements.size();
            return;
        }
        initializeSubobject(aggregate, position, cursor);
        position = nextPosition(aggregate, position);
        if (aggregate.isUnion() && !braced) {
            return;
        }
    }
}

// C17 6.7.9p17-18: a designator list starts at the aggregate of the braces; after the designated subobject, the
// elements continue with the next subobject of the innermost aggregate the designators entered.
std::uint64_t InitializerBuilder::designate(Aggregate& aggregate, Cursor& cursor,
                                            std::span<const ast::Designator> designators, std::size_t index) {
    const ast::Designator& designator = designators[index];
    const bool isLast = index + 1 == designators.size();
    if (designator.kind == ast::Designator::Kind::Field) {
        if (aggregate.record == nullptr) {
            sema_.error(designator.location, std::format("field name not in record or union initializer"));
        }
        const std::vector<ast::FieldDecl*> path = sema_.findField(aggregate.record, designator.fieldName);
        if (path.empty()) {
            sema_.error(designator.location,
                        std::format("unknown field '{}' specified in initializer", designator.fieldName.text()));
        }
        return designateField(aggregate, cursor, path, designators, index);
    }
    if (aggregate.record != nullptr) {
        sema_.error(designator.location, "array index in non-array initializer");
    }
    const std::uint64_t first = arrayIndex(aggregate, designator.first);
    const std::uint64_t last =
        designator.kind == ast::Designator::Kind::Range ? arrayIndex(aggregate, designator.last) : first;
    if (last < first) {
        sema_.error(designator.location, "empty index range in initializer");
    }
    if (isLast) {
        initializeRange(aggregate, first, last, cursor);
        return last + 1;
    }
    if (first != last) {
        sema_.error(designator.location, "a designator after an index range is not supported");
    }
    Aggregate& sub = subAggregate(aggregate, first, designator.location);
    const std::uint64_t position = designate(sub, cursor, designators, index + 1);
    fill(sub, cursor, false, position);
    return first + 1;
}

// A field designator that names a member of an anonymous struct or union goes through the anonymous members.
std::uint64_t InitializerBuilder::designateField(Aggregate& aggregate, Cursor& cursor,
                                                 std::span<ast::FieldDecl* const> path,
                                                 std::span<const ast::Designator> designators, std::size_t index) {
    const std::uint64_t position = path.front()->index();
    const SourceLocation location = designators[index].location;
    if (path.size() > 1) {
        Aggregate& sub = subAggregate(aggregate, position, location);
        const std::uint64_t next = designateField(sub, cursor, path.subspan(1), designators, index);
        fill(sub, cursor, false, next);
    } else if (index + 1 == designators.size()) {
        initializeSubobject(aggregate, position, cursor);
    } else {
        Aggregate& sub = subAggregate(aggregate, position, location);
        const std::uint64_t next = designate(sub, cursor, designators, index + 1);
        fill(sub, cursor, false, next);
    }
    return nextPosition(aggregate, position);
}

std::uint64_t InitializerBuilder::arrayIndex(const Aggregate& aggregate, ast::Expr* expr) {
    sema_.expr(expr);
    if (!isIntegerClass(classify(context_, expr->type()))) {
        sema_.error(expr->location(), "array index in initializer not of integer type");
    }
    const Int128 value = sema_.integerConstant(expr, "array index in initializer");
    if (value < 0 || (aggregate.arraySize && value >= static_cast<Int128>(*aggregate.arraySize))) {
        sema_.error(expr->location(), "array index in initializer exceeds array bounds");
    }
    return static_cast<std::uint64_t>(value);
}

void InitializerBuilder::initializeSubobject(Aggregate& aggregate, std::uint64_t position, Cursor& cursor) {
    const ast::QualType type = subobjectType(aggregate, position);
    ast::Expr* value = cursor.current();
    Slot& target = slot(aggregate, position, value->location());
    target = Slot{};
    if (auto* list = ast::dyn_cast<ast::InitListExpr>(value)) {
        ast::QualType subobject = type;
        if (isScalarLike(type) || (list->elements().size() == 1 && stringFor(type, list->elements().front()))) {
            target.value = braced(subobject, list);
        } else {
            if (!isScalarLike(type) && classify(context_, type) != TypeClass::Array
                && classify(context_, type) != TypeClass::Record) {
                sema_.error(list->location(), "invalid initializer");
            }
            target.aggregate = makeAggregate(type, list, list->range());
            Cursor inner{.elements = list->elements(), .index = 0, .pending = nullptr};
            fill(*target.aggregate, inner, true, 0);
        }
        cursor.next();
        return;
    }
    if (isScalarLike(type)) {
        target.value = scalar(type, value);
        cursor.next();
        return;
    }
    if (ast::StringLiteralExpr* literal = stringFor(type, value)) {
        ast::QualType subobject = type;
        target.value = string(subobject, literal);
        cursor.next();
        return;
    }
    sema_.expr(value);
    if (classify(context_, type) == TypeClass::Record && compatibleUnqualified(context_, value->type(), type)) {
        target.value = scalar(type, value);
        cursor.next();
        return;
    }
    // C17 6.7.9p20: without braces, the subobject takes as many elements as it needs.
    target.aggregate = makeAggregate(type, nullptr, value->range());
    fill(*target.aggregate, cursor, false, 0);
}

// GNU `[first ... last] = value`: the value is converted once, and every element of the range shares it.
void InitializerBuilder::initializeRange(Aggregate& aggregate, std::uint64_t first, std::uint64_t last,
                                         Cursor& cursor) {
    const SourceLocation location = cursor.current()->location();
    initializeSubobject(aggregate, first, cursor);
    Slot& firstSlot = slot(aggregate, first, location);
    if (firstSlot.aggregate != nullptr) {
        firstSlot.value = materialize(*firstSlot.aggregate);
        firstSlot.aggregate.reset();
    }
    ast::Expr* shared = firstSlot.value;
    for (std::uint64_t i = first + 1; i <= last; ++i) {
        slot(aggregate, i, location) = Slot{.value = shared, .aggregate = nullptr};
    }
}

InitializerBuilder::Aggregate& InitializerBuilder::subAggregate(Aggregate& aggregate, std::uint64_t position,
                                                                SourceLocation location) {
    Slot& target = slot(aggregate, position, location);
    if (target.aggregate != nullptr) {
        return *target.aggregate;
    }
    if (target.value != nullptr) {
        sema_.error(location, "a designator into a subobject initialized by an expression is not supported");
    }
    const ast::QualType type = subobjectType(aggregate, position);
    const TypeClass typeClass = classify(context_, type);
    if (typeClass != TypeClass::Record && typeClass != TypeClass::Array) {
        sema_.error(location, "designator into a non-aggregate subobject");
    }
    target.aggregate = makeAggregate(type, nullptr, SourceRange{location, location});
    return *target.aggregate;
}

InitializerBuilder::Slot& InitializerBuilder::slot(Aggregate& aggregate, std::uint64_t position,
                                                   SourceLocation location) {
    if (aggregate.record != nullptr) {
        // GCC: only a static object can be made larger to hold the elements of a flexible array member.
        if (const ast::ArrayType* array = arrayOf(aggregate.record->fields()[position]->type());
            !isStatic_ && array != nullptr && array->sizeKind() == ast::ArrayType::SizeKind::Incomplete) {
            sema_.error(location, "non-static initialization of a flexible array member");
        }
        if (aggregate.isUnion()) {
            ast::FieldDecl* field = aggregate.record->fields()[position];
            if (aggregate.unionField != field) {
                for (Slot& other : aggregate.slots) {
                    other = Slot{};
                }
                aggregate.unionField = field;
            }
        }
        return aggregate.slots[position];
    }
    if (position >= maxInitializedElements) {
        sema_.error(location, "an array initializer with this many elements is not supported");
    }
    if (position >= aggregate.slots.size()) {
        aggregate.slots.resize(position + 1);
    }
    return aggregate.slots[position];
}

ast::QualType InitializerBuilder::subobjectType(const Aggregate& aggregate, std::uint64_t position) const {
    if (aggregate.record != nullptr) {
        return aggregate.record->fields()[position]->type();
    }
    return aggregate.element;
}

// C17 6.7.9p9: unnamed bit-fields do not take part in initialization. Anonymous structs and unions do, as in GCC.
std::uint64_t InitializerBuilder::skipUnnamedBitFields(const Aggregate& aggregate, std::uint64_t position) const {
    if (aggregate.record == nullptr) {
        return position;
    }
    const std::span<ast::FieldDecl* const> fields = aggregate.record->fields();
    while (position < fields.size() && fields[position]->isBitField() && fields[position]->name().empty()) {
        ++position;
    }
    return position;
}

bool InitializerBuilder::isFull(const Aggregate& aggregate, std::uint64_t position) const {
    if (aggregate.record != nullptr) {
        return position >= aggregate.record->fields().size();
    }
    return aggregate.arraySize && position >= *aggregate.arraySize;
}

std::uint64_t InitializerBuilder::nextPosition(const Aggregate& aggregate, std::uint64_t position) const {
    return aggregate.isUnion() ? aggregate.record->fields().size() : position + 1;
}

// The semantic InitListExpr (see ast/expr.hpp); completes an array of unknown size to the elements initialized.
ast::Expr* InitializerBuilder::materialize(Aggregate& aggregate) {
    ast::InitListExpr* list = aggregate.list;
    if (list == nullptr) {
        list = context_.create<ast::InitListExpr>(aggregate.range, std::span<ast::Expr*>());
    }
    std::vector<ast::Expr*> elements;
    ast::Expr* gap = nullptr; // the gaps of an array share one zero, like the elements of a range designator
    const auto element = [&](Slot& slot, ast::QualType type) {
        if (slot.aggregate != nullptr) {
            return materialize(*slot.aggregate);
        }
        if (slot.value != nullptr) {
            return slot.value;
        }
        if (aggregate.record != nullptr) {
            return zero(unqualified(context_, type), aggregate.range);
        }
        if (gap == nullptr) {
            gap = zero(unqualified(context_, type), aggregate.range);
        }
        return gap;
    };
    if (aggregate.record != nullptr) {
        const std::span<ast::FieldDecl* const> fields = aggregate.record->fields();
        if (aggregate.isUnion()) {
            if (aggregate.unionField != nullptr) {
                elements.push_back(
                    element(aggregate.slots[aggregate.unionField->index()], aggregate.unionField->type()));
            }
            list->setUnionField(aggregate.unionField);
        } else {
            for (std::size_t i = 0; i < fields.size(); ++i) {
                elements.push_back(element(aggregate.slots[i], fields[i]->type()));
            }
        }
    } else {
        if (!aggregate.arraySize) {
            const ast::QualType desugared = desugar(aggregate.type);
            const auto* array = ast::cast<ast::ArrayType>(desugared.type());
            aggregate.type = context_.constantArrayOf(array->element(), aggregate.slots.size())
                                 .withQualifiers(desugared.qualifiers());
        }
        for (Slot& slot : aggregate.slots) {
            elements.push_back(element(slot, aggregate.element));
        }
    }
    list->setElements(context_.copy<ast::Expr*>(elements));
    list->setSemanticForm();
    list->setType(aggregate.type, ast::ValueCategory::RValue);
    return list;
}

ast::Expr* Sema::initializer(ast::QualType& type, ast::Expr* init, bool isStatic) {
    InitializerBuilder builder(*this, isStatic);
    return builder.initialize(type, init);
}

} // namespace nitwit::sema::detail
