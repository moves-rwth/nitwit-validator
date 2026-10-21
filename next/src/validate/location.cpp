#include "validate/location.hpp"

#include <algorithm>
#include <format>
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

} // namespace

bool Place::contains(SourceLocation location) const {
    if (location.file != file_) {
        return false;
    }
    return std::ranges::contains(offsets_, location.offset) || std::ranges::any_of(lines_, [&](const Line& line) {
               return line.begin <= location.offset && location.offset < line.end;
           });
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
    if (originalFile_ == FileId::None) {
        return;
    }
    // A line marker applies to whole physical lines, so the presumed line of a line's first byte holds for the line.
    const std::string_view originalName = sources_.name(originalFile_);
    for (std::uint32_t line = 1; line <= sources_.lineCount(file_); ++line) {
        const PresumedLocation presumed = sources_.presumed(*sources_.location(file_, {line, 1}));
        if (presumed.fileName == originalName) {
            physicalLines_[presumed.line].push_back(line);
        }
    }
}

// Iterative, since statements and expressions can nest as deeply as the parser allows.
void LocationMap::collectPoints(const ast::FunctionDecl& function) {
    std::vector<Node> pending = {{.stmt = function.body(), .expr = nullptr}};
    const auto add = [&](std::vector<Point>& points, SourceLocation location, SourceLocation evaluated) {
        if (location.file == file_) {
            points.push_back({.offset = location.offset, .evaluated = evaluated.offset});
        }
    };
    while (!pending.empty()) {
        const Node node = pending.back();
        pending.pop_back();
        if (node.stmt != nullptr) {
            if (const ast::Stmt* first = firstStatement(node.stmt)) {
                add(statements_, node.stmt->location(), first->location());
            }
            if (ast::isa<ast::IfStmt>(node.stmt) || ast::isa<ast::WhileStmt>(node.stmt)
                || ast::isa<ast::ForStmt>(node.stmt) || ast::isa<ast::SwitchStmt>(node.stmt)) {
                add(branchings_, node.stmt->location(), node.stmt->location());
            } else if (const auto* doStmt = ast::dyn_cast<ast::DoWhileStmt>(node.stmt)) {
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

std::vector<Place::Line> LocationMap::linesOf(const witness::Location& location) const {
    std::vector<std::uint32_t> physical;
    if (originalFile_ == FileId::None) {
        if (location.line >= 1 && location.line <= sources_.lineCount(file_)) {
            physical.push_back(location.line);
        }
    } else if (const auto found = physicalLines_.find(location.line); found != physicalLines_.end()) {
        const bool isInOriginalLine =
            !location.column
            || (location.line <= sources_.lineCount(originalFile_)
                && *location.column <= sources_.lineText(originalFile_, location.line).size());
        if (isInOriginalLine) {
            physical = found->second;
        }
    }
    std::vector<Place::Line> lines;
    const auto fileSize = static_cast<std::uint32_t>(sources_.contents(file_).size());
    for (const std::uint32_t line : physical) {
        const std::uint32_t begin = sources_.location(file_, {line, 1})->offset;
        const std::uint32_t end =
            line < sources_.lineCount(file_) ? sources_.location(file_, {line + 1, 1})->offset : fileSize;
        lines.push_back({.begin = begin, .end = end});
    }
    return lines;
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
    const std::vector<Place::Line> lines = linesOf(location);
    if (type == witness::WaypointType::Target && !location.column) {
        place.lines_ = lines;
        return place;
    }
    const std::vector<Point>& points = pointsOf(type);
    for (const Place::Line& line : lines) {
        const auto first = std::ranges::lower_bound(points, line.begin, {}, &Point::offset);
        if (!location.column) {
            // The first evaluation point of the type in the line.
            if (first != points.end() && first->offset < line.end) {
                place.offsets_.push_back(first->evaluated);
                break;
            }
            continue;
        }
        const std::uint32_t offset = line.begin + *location.column - 1;
        if (offset >= line.end) {
            continue;
        }
        const auto at = std::ranges::lower_bound(points, offset, {}, &Point::offset);
        const bool isPoint = at != points.end() && at->offset == offset;
        place.offsets_.push_back(isPoint && type == witness::WaypointType::Assumption ? at->evaluated : offset);
    }
    return place;
}

std::string LocationMap::describe(SourceLocation location) const {
    if (originalFile_ != FileId::None) {
        const PresumedLocation presumed = sources_.presumed(location);
        return std::format("{}:{}", presumed.line, presumed.column);
    }
    const LineColumn position = sources_.lineColumn(location);
    return std::format("{}:{}", position.line, position.column);
}

} // namespace nitwit::validate
