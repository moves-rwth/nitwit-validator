// Execution: start-up (functions, string literals and objects with static storage), the call of main, the ways a run
// ends, and the limits.

#include "interp/execution.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "ast/casting.hpp"
#include "support/assert.hpp"

namespace nitwit::interp {

namespace {

std::string_view memoryErrorText(MemoryErrorKind kind) {
    switch (kind) {
    case MemoryErrorKind::NullPointer:
        return "null pointer";
    case MemoryErrorKind::InvalidAddress:
        return "invalid address";
    case MemoryErrorKind::OutOfBounds:
        return "out of bounds";
    case MemoryErrorKind::DeadObject:
        return "the object's lifetime has ended";
    case MemoryErrorKind::ReadOnlyWrite:
        return "read-only memory";
    case MemoryErrorKind::CodeAccess:
        return "the code of a function";
    case MemoryErrorKind::InvalidFree:
        return "not the start of a live heap block";
    case MemoryErrorKind::OutOfAddressSpace:
        return "out of address space";
    }
    return "memory error";
}

} // namespace

Execution::Execution(std::shared_ptr<const Program> program, InputOracle& oracle, ExecutionObserver& observer,
                     RunOptions options)
    : program_(std::move(program)), code_(*program_), context_(program_->context), types_(program_->types),
      oracle_(oracle), observer_(observer), options_(options), memory_(types_.target().pointerBits()) {}

Execution::~Execution() = default;

RunResult Execution::run() {
    RunResult result;
    try {
        allocateStaticStorage();
        initializeStaticStorage();
        const value::Value status = callMain();
        result.termination = Termination::Returned;
        result.exitStatus = status.kind() == value::Value::Kind::Integer ? static_cast<int>(status.signedBits()) : 0;
    } catch (const RunEnd& end) {
        result.termination = end.termination;
        result.exitStatus = end.exitStatus;
        result.message = end.message;
        result.location = end.location;
    }
    // The frames lived on the host stack, which the exception unwound.
    frames_.clear();
    frame_ = nullptr;
    result.steps = steps_;
    result.inputs = inputs_;
    return result;
}

// ---- Start-up ----

void Execution::allocateStaticStorage() {
    // Functions: one byte in the code region each, so that function pointers are distinct addresses.
    functionAddresses_.assign(code_.functions.size(), 0);
    for (FunctionId id = 0; id < code_.functions.size(); ++id) {
        const FunctionEntity& function = code_.functions[id];
        if (function.kind == FunctionKind::WeakMissing || function.kind == FunctionKind::Alias) {
            continue; // a weak function without definition has the address 0
        }
        functionAddresses_[id] = allocate(Region::Code, 1, 1, function.name.text(), nullptr);
        functionsAt_.emplace(functionAddresses_[id], id);
    }
    for (FunctionId id = 0; id < code_.functions.size(); ++id) {
        if (code_.functions[id].kind == FunctionKind::Alias) {
            const FunctionEntity& target = followAlias(code_.functions[id]);
            functionAddresses_[id] = functionAddresses_[static_cast<FunctionId>(&target - code_.functions.data())];
        }
    }

    stringAddresses_.reserve(code_.strings.size());
    for (const ast::StringLiteralExpr* string : code_.strings) {
        const std::uint64_t size = types_.describe(string->type()).size;
        const std::uint64_t alignment = types_.describe(TypeTable::array(string->type())->element()).size;
        const Address address =
            allocate(Region::ReadOnly, size, std::max<std::uint64_t>(alignment, 1), "string literal", string);
        memory_.initialize(address, stringBytes(string, size));
        stringAddresses_.push_back(address);
    }

    globalAddresses_.assign(code_.globals.size(), 0);
    for (GlobalId id = 0; id < code_.globals.size(); ++id) {
        const GlobalEntity& global = code_.globals[id];
        if (global.kind == GlobalEntity::Kind::Defined) {
            globalAddresses_[id] =
                allocate(Region::Global, global.layout.size, global.layout.alignment, global.name.text(), nullptr);
        } else if (global.kind == GlobalEntity::Kind::LibraryObject) {
            globalAddresses_[id] = objectAddress(global.name.text());
        }
    }
    for (GlobalId id = 0; id < code_.globals.size(); ++id) {
        const GlobalEntity* global = &code_.globals[id];
        for (std::size_t hops = 0; global->kind == GlobalEntity::Kind::Alias && hops < code_.globals.size(); ++hops) {
            global = &code_.globals[global->aliasTarget];
        }
        globalAddresses_[id] = globalAddresses_[static_cast<GlobalId>(global - code_.globals.data())];
    }

    fileScopeLiterals_.reserve(code_.fileScopeLiterals.size());
    for (const ObjectLayout& layout : code_.fileScopeLiteralLayouts) {
        fileScopeLiterals_.push_back(
            allocate(Region::Global, layout.size, layout.alignment, "compound literal", nullptr));
    }
}

// C17 6.7.9p10: objects with static storage duration are initialized before program startup, to zero unless they
// have an initializer, which is a constant expression.
void Execution::initializeStaticStorage() {
    for (GlobalId id = 0; id < code_.globals.size(); ++id) {
        const GlobalEntity& global = code_.globals[id];
        if (global.kind == GlobalEntity::Kind::Defined && global.initialized != nullptr) {
            initialize(globalAddresses_[id], global.decl->type(), global.initialized->initializer(), true);
        }
    }
}

// C17 5.1.2.2.1: main gets argc and argv, with argv[0] the program name ("prog", as in the difftest harness);
// 5.1.2.2.3: reaching the end of main returns 0.
value::Value Execution::callMain() {
    const FunctionEntity& main = code_.functions[code_.main];
    const std::span<ast::VarDecl* const> parameters = main.decl->parameters();
    Arguments arguments;
    const value::IntegerFormat pointer = types_.pointerFormat();
    if (!parameters.empty()) {
        arguments.values.push_back(value::Value::integer(1, types_.describe(parameters[0]->type()).integer));
        arguments.types.push_back(parameters[0]->type());
    }
    const std::uint64_t pointerSize = types_.target().layout(ScalarType::Pointer).size;
    if (parameters.size() >= 2) {
        static constexpr std::string_view programName = "prog";
        const Address name = allocate(Region::Global, programName.size() + 1, 1, "argv[0]", nullptr);
        writeBytes(name, std::as_bytes(std::span(programName.data(), programName.size())), nullptr);
        // argv[0] and the null pointer argv[1]; the global region starts zeroed.
        const Address argv = allocate(Region::Global, 2 * pointerSize, pointerSize, "argv", nullptr);
        std::vector<std::byte> first(pointerSize);
        value::encodeInteger(value::Value::integer(name, pointer), pointer, first);
        writeBytes(argv, first, nullptr);
        arguments.values.push_back(value::Value::integer(argv, pointer));
        arguments.types.push_back(parameters[1]->type());
    }
    if (parameters.size() >= 3) {
        const Address environment = allocate(Region::Global, pointerSize, pointerSize, "envp", nullptr);
        arguments.values.push_back(value::Value::integer(environment, pointer));
        arguments.types.push_back(parameters[2]->type());
    }
    const std::optional<value::Value> status = invoke(main, arguments, nullptr);
    const ast::FunctionType* type = TypeTable::function(main.decl->type());
    if (!status || types_.describe(type->returnType()).representation != Representation::Integer) {
        return value::Value::integer(0, types_.intFormat());
    }
    return *status;
}

// ---- Ending the run ----

void Execution::stop(Termination termination, std::string message, SourceLocation location) const {
    throw RunEnd{.termination = termination, .exitStatus = 0, .message = std::move(message), .location = location};
}

void Execution::undefined(std::string message, const ast::Expr* where) const {
    stop(Termination::UndefinedBehavior, std::move(message), locationOf(where));
}

void Execution::memoryError(const MemoryError& error, bool isWrite, const ast::Expr* where) const {
    if (error.kind == MemoryErrorKind::OutOfAddressSpace) {
        stop(Termination::OutOfMemory, "out of address space", locationOf(where));
    }
    std::string message = std::format("invalid {} of {} bytes at 0x{:x}: {}", isWrite ? "write" : "read", error.size,
                                      error.address, memoryErrorText(error.kind));
    if (const std::optional<AllocationInfo> allocation = memory_.find(error.address)) {
        message +=
            std::format(" ({} '{}')", allocation->region == Region::Heap ? "heap block" : "object", allocation->name);
    }
    undefined(std::move(message), where);
}

void Execution::countStep() {
    ++steps_;
    if (options_.maxSteps != 0 && steps_ > options_.maxSteps) {
        stop(Termination::LimitReached, std::format("the step limit of {} was reached", options_.maxSteps), location_);
    }
}

Address Execution::allocate(Region region, std::uint64_t size, std::uint64_t alignment, std::string_view name,
                            const ast::Expr* where) {
    const bool isLimited = region == Region::Stack || region == Region::Heap;
    if (isLimited && size > options_.memoryLimit - std::min(memory_.liveBytes(), options_.memoryLimit)) {
        // Refused before the host allocates anything.
        stop(Termination::OutOfMemory,
             std::format("an object of {} bytes exceeds the memory limit of {} bytes", size, options_.memoryLimit),
             locationOf(where));
    }
    const std::expected<Address, MemoryError> address = memory_.allocate(region, size, alignment, name);
    if (!address) {
        memoryError(address.error(), true, where);
    }
    return *address;
}

// ---- LibraryContext ----

std::expected<Address, MemoryError> Execution::allocateHeap(std::uint64_t size, bool zeroed) {
    const MemoryError exhausted{.kind = MemoryErrorKind::OutOfAddressSpace, .address = 0, .size = size};
    if (size > options_.memoryLimit || memory_.liveBytes() > options_.memoryLimit - size) {
        return std::unexpected(exhausted);
    }
    return zeroed ? memory_.allocateZeroed(Region::Heap, size, types_.target().maxAlign(), "heap")
                  : memory_.allocate(Region::Heap, size, types_.target().maxAlign(), "heap");
}

Address Execution::objectAddress(std::string_view name) {
    if (const auto found = libraryObjects_.find(std::string(name)); found != libraryObjects_.end()) {
        return found->second;
    }
    const std::optional<LibraryObject> object = libraryObject(name, types_.target());
    NITWIT_ASSERT(object.has_value(), "objectAddress of an object the library does not provide");
    // The map's key outlives the Memory's use of the name; it is inserted before the object is initialized, since
    // the initializer may ask for the address of another object, or of this one.
    const auto entry = libraryObjects_.emplace(std::string(name), 0).first;
    const std::expected<Address, MemoryError> address = memory_.allocate(
        object->readOnly ? Region::ReadOnly : Region::Global, object->size, object->alignment, entry->first);
    NITWIT_ASSERT(address.has_value(), "no address space for a library object");
    entry->second = *address;
    if (object->initialize != nullptr) {
        object->initialize(*this, *address);
    }
    return *address;
}

} // namespace nitwit::interp
