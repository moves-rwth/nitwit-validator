// Record layouts as GCC computes them for the System V ABI on i386 and x86-64 (gcc/stor-layout.cc, place_field and
// update_alignment_for_field, with PCC_BITFIELD_TYPE_MATTERS and x86_field_alignment).
//
// Per field, in bits from the start of the record:
// - A member is aligned to its type's alignment as a member (`_Alignof`: 4 for 8-byte scalars on i386), raised by an
//   `aligned` attribute that is at least the type's natural alignment, lowered to 1 byte by `packed` (unless the field
//   has an `aligned` attribute), and capped by `#pragma pack`.
// - A bit-field is placed at the next free bit. It moves to the next boundary of its type's alignment only if it would
//   otherwise span more alignment units than its type has (so `char a:4; long long b:40` share bytes), and never if it
//   is packed or `#pragma pack` is in effect. A named bit-field raises the record's alignment to its type's alignment.
// - A zero-width bit-field aligns the next field to its type's alignment, regardless of `packed` and `#pragma pack`,
//   and does not change the record's alignment.
// The record's alignment is the largest field alignment (at least its own `aligned` attribute), and its size is
// rounded up to it. Union members all start at offset 0.

#include <algorithm>
#include <vector>

#include "ast/casting.hpp"
#include "sema/sema_impl.hpp"
#include "sema/types.hpp"

namespace nitwit::sema::detail {

namespace {

std::uint64_t roundUp(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// GCC's excess_unit_span: whether a bit-field of `width` bits at `position` spans more units of `alignment` bits than
// its type of `typeBits` bits has.
bool spansTooManyUnits(std::uint64_t position, std::uint64_t width, std::uint64_t alignment, std::uint64_t typeBits) {
    const std::uint64_t offset = position % alignment;
    return (offset + width + alignment - 1) / alignment > typeBits / alignment;
}

} // namespace

const ast::RecordLayout* Sema::computeLayout(const ast::RecordDecl* record) {
    const std::span<ast::FieldDecl* const> fields = record->fields();
    const std::uint64_t maxFieldAlignment = record->maxFieldAlignment(); // bytes, 0 if no #pragma pack
    const bool isUnion = record->isUnion();
    std::uint64_t recordAlignment = std::max<std::uint64_t>(1, record->requestedAlignment());
    std::uint64_t position = 0;  // struct: the next free bit
    std::uint64_t unionSize = 0; // union: the largest member, in bytes
    std::vector<std::uint64_t> offsets(fields.size(), 0);
    std::vector<std::uint8_t> bits(fields.size(), 0);
    const auto capped = [&](std::uint64_t alignment) {
        return maxFieldAlignment != 0 ? std::min(alignment, maxFieldAlignment) : alignment;
    };

    for (std::size_t i = 0; i < fields.size(); ++i) {
        const ast::FieldDecl* field = fields[i];
        const ast::QualType type = field->type();
        const ast::ArrayType* array = arrayOf(type);
        const bool isFlexible = array != nullptr && array->sizeKind() == ast::ArrayType::SizeKind::Incomplete;
        const std::uint64_t typeSize = isFlexible ? 0 : sizeOf(context_, type);
        const std::uint64_t typeAlignment = alignOf(context_, type);
        const bool isPacked = record->isPacked() || field->isPacked();
        const std::uint64_t requested = field->requestedAlignment();

        if (!field->isBitField()) {
            const std::uint64_t alignment = fieldAlignment(context_, field);
            recordAlignment = std::max(recordAlignment, alignment);
            if (isUnion) {
                unionSize = std::max(unionSize, typeSize);
                continue;
            }
            position = roundUp(position, alignment * 8);
            offsets[i] = position / 8;
            position += typeSize * 8;
            continue;
        }

        const std::uint64_t width = field->bitWidth();
        if (width == 0) {
            if (!isUnion) {
                position = roundUp(position, typeAlignment * 8);
                offsets[i] = position / 8;
            }
            continue;
        }
        // Without an `aligned` attribute, a bit-field needs no alignment of its own (GCC: DECL_ALIGN of 1 bit).
        const std::uint64_t alignment = requested != 0 ? capped(requested) : 1;
        if (!field->name().empty()) {
            std::uint64_t named = typeAlignment;
            if (maxFieldAlignment != 0) {
                named = std::min(named, maxFieldAlignment);
            } else if (isPacked) {
                named = 1;
            }
            recordAlignment = std::max({recordAlignment, alignment, named});
        }
        if (isUnion) {
            unionSize = std::max(unionSize, (width + 7) / 8);
            continue;
        }
        if (requested != 0) {
            position = roundUp(position, alignment * 8);
        }
        if (!isPacked && maxFieldAlignment == 0
            && spansTooManyUnits(position, width, typeAlignment * 8, typeSize * 8)) {
            position = roundUp(position, typeAlignment * 8);
        }
        offsets[i] = position / 8;
        bits[i] = static_cast<std::uint8_t>(position % 8);
        position += width;
    }

    auto* layout = context_.create<ast::RecordLayout>();
    layout->alignment = recordAlignment;
    layout->size = roundUp(isUnion ? unionSize : (position + 7) / 8, recordAlignment);
    layout->fieldOffsets = context_.copy<std::uint64_t>(offsets);
    layout->bitOffsets = context_.copy<std::uint8_t>(bits);
    return layout;
}

} // namespace nitwit::sema::detail
