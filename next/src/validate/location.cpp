#include "validate/location.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

#include "ast/casting.hpp"
#include "ast/expr.hpp"
#include "ast/stmt.hpp"

namespace nitwit::validate {

namespace {

// A statement or an expression still to visit.
struct Node {
    const ast::Stmt* stmt = nullptr;
    const ast::Expr* expr = nullptr;
};

// The statement a block's evaluation point stands for: its first statement, in nested blocks too; null if empty.
const ast::Stmt* firstStatement(const ast::Stmt* stmt) {
    while (const auto* block = ast::dyn_cast<ast::CompoundStmt>(stmt)) {
        stmt = block->body().empty() ? nullptr : block->body().front();
    }
    return stmt;
}

// The sub-statements and expressions of a statement that run as part of it.
void pushChildren(const ast::Stmt* stmt, std::vector<Node>& pending) {
    const auto push = [&](const ast::Stmt* child) {
        if (child != nullptr) {
            pending.push_back({.stmt = child, .expr = nullptr});
        }
    };
    const auto pushExpr = [&](const ast::Expr* child) {
        if (child != nullptr) {
            pending.push_back({.stmt = nullptr, .expr = child});
        }
    };
    switch (stmt->kind()) {
    case ast::StmtKind::Compound:
        for (const ast::Stmt* item : ast::cast<ast::CompoundStmt>(stmt)->body()) {
            push(item);
        }
        return;
    case ast::StmtKind::Expression:
        return pushExpr(ast::cast<ast::ExpressionStmt>(stmt)->expr());
    case ast::StmtKind::Declaration:
        for (const ast::Decl* decl : ast::cast<ast::DeclarationStmt>(stmt)->decls()) {
            if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl)) {
                pushExpr(var->initializer());
            }
        }
        return;
    case ast::StmtKind::If: {
        const auto* ifStmt = ast::cast<ast::IfStmt>(stmt);
        pushExpr(ifStmt->condition());
        push(ifStmt->thenStmt());
        return push(ifStmt->elseStmt());
    }
    case ast::StmtKind::While:
        pushExpr(ast::cast<ast::WhileStmt>(stmt)->condition());
        return push(ast::cast<ast::WhileStmt>(stmt)->body());
    case ast::StmtKind::DoWhile:
        push(ast::cast<ast::DoWhileStmt>(stmt)->body());
        return pushExpr(ast::cast<ast::DoWhileStmt>(stmt)->condition());
    case ast::StmtKind::For: {
        const auto* forStmt = ast::cast<ast::ForStmt>(stmt);
        push(forStmt->init());
        pushExpr(forStmt->condition());
        pushExpr(forStmt->increment());
        return push(forStmt->body());
    }
    case ast::StmtKind::Switch:
        pushExpr(ast::cast<ast::SwitchStmt>(stmt)->condition());
        return push(ast::cast<ast::SwitchStmt>(stmt)->body());
    case ast::StmtKind::Case:
        return push(ast::cast<ast::CaseStmt>(stmt)->subStmt());
    case ast::StmtKind::Default:
        return push(ast::cast<ast::DefaultStmt>(stmt)->subStmt());
    case ast::StmtKind::Label:
        return push(ast::cast<ast::LabelStmt>(stmt)->subStmt());
    case ast::StmtKind::Return:
        return pushExpr(ast::cast<ast::ReturnStmt>(stmt)->value());
    case ast::StmtKind::Break:
    case ast::StmtKind::Continue:
    case ast::StmtKind::Goto:
    case ast::StmtKind::Null:
    case ast::StmtKind::Asm:
        return;
    }
    std::unreachable();
}

// The subexpressions that are evaluated with an expression (not those of sizeof and other unevaluated operands).
void pushChildren(const ast::Expr* expr, std::vector<Node>& pending) {
    const auto pushExpr = [&](const ast::Expr* child) {
        if (child != nullptr) {
            pending.push_back({.stmt = nullptr, .expr = child});
        }
    };
    switch (expr->kind()) {
    case ast::ExprKind::Paren:
        return pushExpr(ast::cast<ast::ParenExpr>(expr)->inner());
    case ast::ExprKind::Unary:
        return pushExpr(ast::cast<ast::UnaryExpr>(expr)->operand());
    case ast::ExprKind::Binary:
        pushExpr(ast::cast<ast::BinaryExpr>(expr)->lhs());
        return pushExpr(ast::cast<ast::BinaryExpr>(expr)->rhs());
    case ast::ExprKind::Conditional: {
        const auto* conditional = ast::cast<ast::ConditionalExpr>(expr);
        pushExpr(conditional->condition());
        pushExpr(conditional->trueExpr());
        return pushExpr(conditional->falseExpr());
    }
    case ast::ExprKind::Call:
        pushExpr(ast::cast<ast::CallExpr>(expr)->callee());
        for (const ast::Expr* argument : ast::cast<ast::CallExpr>(expr)->arguments()) {
            pushExpr(argument);
        }
        return;
    case ast::ExprKind::Member:
        return pushExpr(ast::cast<ast::MemberExpr>(expr)->base());
    case ast::ExprKind::ArraySubscript:
        pushExpr(ast::cast<ast::ArraySubscriptExpr>(expr)->base());
        return pushExpr(ast::cast<ast::ArraySubscriptExpr>(expr)->index());
    case ast::ExprKind::Cast:
        return pushExpr(ast::cast<ast::CastExpr>(expr)->operand());
    case ast::ExprKind::ImplicitCast:
        return pushExpr(ast::cast<ast::ImplicitCastExpr>(expr)->operand());
    case ast::ExprKind::CompoundLiteral:
        return pushExpr(ast::cast<ast::CompoundLiteralExpr>(expr)->initializer());
    case ast::ExprKind::InitList:
        for (const ast::Expr* element : ast::cast<ast::InitListExpr>(expr)->elements()) {
            pushExpr(element);
        }
        return;
    case ast::ExprKind::DesignatedInit:
        return pushExpr(ast::cast<ast::DesignatedInitExpr>(expr)->initializer());
    case ast::ExprKind::VaArg:
        return pushExpr(ast::cast<ast::VaArgExpr>(expr)->list());
    case ast::ExprKind::StatementExpr:
        pending.push_back({.stmt = ast::cast<ast::StatementExpr>(expr)->body(), .expr = nullptr});
        return;
    case ast::ExprKind::GenericSelection:
        return pushExpr(ast::cast<ast::GenericSelectionExpr>(expr)->selected());
    case ast::ExprKind::ChooseExpr:
        return pushExpr(ast::cast<ast::ChooseExpr>(expr)->selected());
    case ast::ExprKind::IntegerLiteral:
    case ast::ExprKind::FloatingLiteral:
    case ast::ExprKind::CharacterLiteral:
    case ast::ExprKind::StringLiteral:
    case ast::ExprKind::DeclRef:
    case ast::ExprKind::ImplicitZero:
    case ast::ExprKind::SizeOfAlignOf:
    case ast::ExprKind::OffsetOf:
    case ast::ExprKind::TypesCompatible:
    case ast::ExprKind::Result:
        return;
    }
    std::unreachable();
}

bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

// The number of leading bytes in which a line of the preprocessor's output agrees with its original line; blanks agree
// with blanks.
std::uint32_t agreeingPrefix(std::string_view physical, std::string_view original) {
    std::uint32_t length = 0;
    while (length < physical.size() && length < original.size()
           && (physical[length] == original[length] || (isBlank(physical[length]) && isBlank(original[length])))) {
        ++length;
    }
    return length;
}

// `#line 7 "a.c"`, or the GNU form `# 7 "a.c"` (C17 6.10.4).
bool isLineDirective(std::string_view line) {
    std::size_t i = 0;
    const auto skipBlanks = [&] {
        while (i < line.size() && isBlank(line[i])) {
            ++i;
        }
    };
    skipBlanks();
    if (i == line.size() || line[i] != '#') {
        return false;
    }
    ++i;
    skipBlanks();
    if (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        return true;
    }
    return line.substr(i).starts_with("line") && (i + 4 == line.size() || isBlank(line[i + 4]));
}

} // namespace

bool Place::contains(SourceLocation location) const {
    return location.file == file_ && std::ranges::contains(offsets_, location.offset);
}

LocationMap::LocationMap(const SourceManager& sources, FileId file, FileId originalFile,
                         const ast::TranslationUnit& unit)
    : sources_(sources), file_(file), originalFile_(originalFile) {
    for (const ast::Decl* decl : unit.decls) {
        if (const auto* function = ast::dyn_cast<ast::FunctionDecl>(decl); function != nullptr && function->body()) {
            collectPoints(*function);
        }
    }
    for (std::vector<Point>* points : {&statements_, &branchings_, &calls_}) {
        std::ranges::sort(*points, {}, &Point::offset);
    }
    std::ranges::sort(starts_);
    if (originalFile_ == FileId::None) {
        return;
    }
    for (std::uint32_t line = 1; line <= sources_.lineCount(originalFile_) && !lineDirective_; ++line) {
        if (isLineDirective(sources_.lineText(originalFile_, line))) {
            lineDirective_ = line;
        }
    }
    // A line marker applies to whole physical lines, so the presumed line of a line's first byte holds for the line.
    const std::string_view originalName = sources_.name(originalFile_);
    agreeing_.assign(sources_.lineCount(file_), 0);
    for (std::uint32_t line = 1; line <= sources_.lineCount(file_); ++line) {
        // GCC's cpp begins with `# 0 "file.c"`: line 0 holds no code.
        const PresumedLocation presumed = sources_.presumed(*sources_.location(file_, {line, 1}));
        if (presumed.fileName != originalName || presumed.line == 0) {
            continue;
        }
        physicalLines_[presumed.line].push_back(line);
        if (presumed.line <= sources_.lineCount(originalFile_)) {
            agreeing_[line - 1] =
                agreeingPrefix(sources_.lineText(file_, line), sources_.lineText(originalFile_, presumed.line));
        }
    }
}

// Iterative, since statements and expressions can nest as deeply as the parser allows.
void LocationMap::collectPoints(const ast::FunctionDecl& function) {
    std::vector<Node> pending = {{.stmt = function.body(), .expr = nullptr}};
    const auto add = [&](std::vector<Point>& points, SourceLocation location, SourceLocation evaluated) {
        if (location.file == file_) {
            points.push_back({.offset = location.offset, .evaluated = evaluated.offset, .function = &function});
        }
    };
    const auto addStart = [&](const ast::Expr* expr) {
        if (expr != nullptr && expr->location().file == file_) {
            starts_.push_back(expr->location().offset);
        }
    };
    while (!pending.empty()) {
        const Node node = pending.back();
        pending.pop_back();
        if (node.stmt != nullptr) {
            // Statements and full expressions (C17 6.8p4), where a target without a column can be.
            if (node.stmt->location().file == file_) {
                starts_.push_back(node.stmt->location().offset);
            }
            if (const auto* ifStmt = ast::dyn_cast<ast::IfStmt>(node.stmt)) {
                addStart(ifStmt->condition());
            } else if (const auto* whileStmt = ast::dyn_cast<ast::WhileStmt>(node.stmt)) {
                addStart(whileStmt->condition());
            } else if (const auto* doStmt = ast::dyn_cast<ast::DoWhileStmt>(node.stmt)) {
                addStart(doStmt->condition());
            } else if (const auto* forStmt = ast::dyn_cast<ast::ForStmt>(node.stmt)) {
                addStart(forStmt->condition());
                addStart(forStmt->increment());
            } else if (const auto* switchStmt = ast::dyn_cast<ast::SwitchStmt>(node.stmt)) {
                addStart(switchStmt->condition());
            } else if (const auto* returnStmt = ast::dyn_cast<ast::ReturnStmt>(node.stmt)) {
                addStart(returnStmt->value());
            } else if (const auto* declaration = ast::dyn_cast<ast::DeclarationStmt>(node.stmt)) {
                for (const ast::Decl* decl : declaration->decls()) {
                    if (const auto* var = ast::dyn_cast<ast::VarDecl>(decl)) {
                        addStart(var->initializer());
                    }
                }
            }
            // A block stands for its first statement, unless a jump to a label can enter the block there without
            // passing its `{` (goto, or the case labels of a switch body); an empty block has no event.
            const ast::Stmt* first = firstStatement(node.stmt);
            const bool isEnteredByJump = ast::isa<ast::CompoundStmt>(node.stmt) && first != nullptr
                                         && (ast::isa<ast::LabelStmt>(first) || ast::isa<ast::CaseStmt>(first)
                                             || ast::isa<ast::DefaultStmt>(first));
            if (first != nullptr && !isEnteredByJump) {
                add(statements_, node.stmt->location(), first->location());
            }
            if (ast::isa<ast::IfStmt>(node.stmt) || ast::isa<ast::WhileStmt>(node.stmt)
                || ast::isa<ast::ForStmt>(node.stmt) || ast::isa<ast::SwitchStmt>(node.stmt)) {
                add(branchings_, node.stmt->location(), node.stmt->location());
            } else if (const auto* doStmt = ast::dyn_cast<ast::DoWhileStmt>(node.stmt)) {
                // The format locates the branching at the first character of the statement, `do`; the condition is
                // evaluated at its `while`, which NITWIT 3.x used.
                add(branchings_, doStmt->location(), doStmt->whileLocation());
                add(branchings_, doStmt->whileLocation(), doStmt->whileLocation());
            }
            pushChildren(node.stmt, pending);
            continue;
        }
        if (const auto* conditional = ast::dyn_cast<ast::ConditionalExpr>(node.expr)) {
            add(branchings_, conditional->questionLocation(), conditional->questionLocation());
        } else if (const auto* call = ast::dyn_cast<ast::CallExpr>(node.expr)) {
            add(calls_, call->rightParenLocation(), call->rightParenLocation());
        }
        pushChildren(node.expr, pending);
    }
}

std::vector<std::uint32_t> LocationMap::physicalLinesOf(std::uint32_t line) const {
    if (originalFile_ == FileId::None) {
        if (line >= 1 && line <= sources_.lineCount(file_)) {
            return {line};
        }
        return {};
    }
    const auto found = physicalLines_.find(line);
    return found != physicalLines_.end() ? found->second : std::vector<std::uint32_t>();
}

Place::Line LocationMap::rangeOf(std::uint32_t physicalLine) const {
    const auto fileSize = static_cast<std::uint32_t>(sources_.contents(file_).size());
    const std::uint32_t begin = sources_.location(file_, {physicalLine, 1})->offset;
    const std::uint32_t end =
        physicalLine < sources_.lineCount(file_) ? sources_.location(file_, {physicalLine + 1, 1})->offset : fileSize;
    return {.begin = begin, .end = end};
}

const std::vector<LocationMap::Point>& LocationMap::pointsOf(witness::WaypointType type) const {
    switch (type) {
    case witness::WaypointType::Assumption:
        return statements_;
    case witness::WaypointType::Branching:
        return branchings_;
    case witness::WaypointType::FunctionEnter:
    case witness::WaypointType::FunctionReturn:
    case witness::WaypointType::Target:
        return calls_;
    }
    std::unreachable();
}

Place LocationMap::place(const witness::Location& location, witness::WaypointType type) const {
    Place place;
    place.file_ = file_;
    const bool isTarget = type == witness::WaypointType::Target;
    const std::vector<Point>& points = pointsOf(type);
    const auto addPoint = [&](const Point& point) {
        place.offsets_.push_back(point.evaluated);
        place.functions_.push_back(point.function);
    };
    for (const std::uint32_t line : physicalLinesOf(location.line)) {
        const Place::Line range = rangeOf(line);
        if (!location.column) {
            if (isTarget) {
                // Witness format 2.x: the first statement or full expression in the line; the error call must start it.
                const auto first = std::ranges::lower_bound(starts_, range.begin);
                if (first != starts_.end() && *first < range.end) {
                    place.offsets_.push_back(*first);
                    break;
                }
                continue;
            }
            // The first evaluation point of the type in the line.
            const auto first = std::ranges::lower_bound(points, range.begin, {}, &Point::offset);
            if (first != points.end() && first->offset < range.end) {
                addPoint(*first);
                break;
            }
            continue;
        }
        // Columns count from 1 and may be one past the line's text; compared without forming an offset that could
        // wrap around.
        const std::uint32_t column = *location.column;
        if (column == 0 || column - 1 >= range.end - range.begin) {
            continue;
        }
        if (originalFile_ != FileId::None) {
            const bool isInOriginalLine = location.line >= 1 && location.line <= sources_.lineCount(originalFile_)
                                          && column <= sources_.lineText(originalFile_, location.line).size();
            if (!isInOriginalLine) {
                continue;
            }
            if (column > agreeing_[line - 1]) {
                place.isAfterPreprocessorChange_ = true;
                continue;
            }
        }
        const std::uint32_t offset = range.begin + column - 1;
        if (isTarget) {
            place.offsets_.push_back(offset); // the start of the call, which calls_ does not hold
            continue;
        }
        // Only an evaluation point of the type: a column inside a construct matches nothing.
        const auto at = std::ranges::lower_bound(points, offset, {}, &Point::offset);
        if (at != points.end() && at->offset == offset) {
            addPoint(*at);
        }
    }
    return place;
}

std::string LocationMap::describe(SourceLocation location) const {
    const LineColumn position = sources_.lineColumn(location);
    if (originalFile_ == FileId::None) {
        return std::format("{}:{}", position.line, position.column);
    }
    const PresumedLocation presumed = sources_.presumed(location);
    if (presumed.fileName != sources_.name(originalFile_)) {
        return std::format("{}:{}:{}", presumed.fileName, presumed.line, presumed.column);
    }
    const bool isUnchanged =
        position.line >= 1 && position.line <= agreeing_.size() && position.column <= agreeing_[position.line - 1];
    if (!isUnchanged) {
        return std::format("{} (column {} after preprocessing)", presumed.line, position.column);
    }
    return std::format("{}:{}", presumed.line, position.column);
}

} // namespace nitwit::validate
