#include "parser.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility> // for std::move

// --- Parser 构造函数 ---
Parser::Parser(const std::string& source) 
    : lexer(source), tokens(lexer.tokenize()), cursor(0) {}


// --- Utility Functions (Token Management) ---

Token Parser::peek() const {
    if (cursor < tokens.size()) {
        return tokens[cursor];
    }
    // Return EOF token if out of bounds
    return tokens.back(); 
}

Token Parser::next() {
    if (cursor < tokens.size()) {
        return tokens[cursor++];
    }
    return tokens.back();
}

void Parser::reportError(const std::string& message) const {
    const Token& t = peek();
    std::stringstream ss;
    ss << "Parse Error at line " << t.info.line << ", column " << t.info.column << ": " << message
       << " Found token: <" << t.content << ", " << t.typeToString() << ">";
    throw std::runtime_error(ss.str());
}

std::shared_ptr<Token> Parser::eat(TokenType type, const std::string& error_msg) {
    if (peek().type == type) {
        return std::make_shared<Token>(next());
    } else {
        reportError(error_msg);
        return nullptr;
    }
}

std::shared_ptr<Token> Parser::eat(const std::string& content, const std::string& error_msg) {
    if (peek().content == content) {
        return std::make_shared<Token>(next());
    } else {
        reportError(error_msg);
        return nullptr;
    }
}

// --- Predicate Functions (isXXXNode) ---

bool Parser::isLiteralNode() const {
    return peek().type == TokenType::LITERAL;
}

bool Parser::isIdentifierExprStart() const {
    return peek().type == TokenType::IDENTIFIER;
}

bool Parser::isPrimExprStart() const {
    return isLiteralNode() || isIdentifierExprStart() || peek().content == "(";
}

bool Parser::isAssignExprStart() {
    std::size_t tmp = cursor;
    parseIdentifierExprNode();
    if (peek().content == "=") {
        cursor = tmp;
        return true;
    }
    else {
        cursor = tmp;
        return false;
    }
}

bool Parser::isArrayExprStart() const {
    return peek().content == "[";
}

bool Parser::isWholeExprStart() const { 
    return isPrimExprStart() || isArrayExprStart(); 
}

// Statement Predicates 

bool Parser::isLetStmtStart() const {
    return peek().content == "let" || peek().content == "const";
}

bool Parser::isExprStmtStart() const {
    // 表达式语句通常以 WholeExprStart 开始 (例如赋值或函数调用)
    return isWholeExprStart(); 
}

bool Parser::isReturnStmtStart() const {
    return peek().content == "return";
}

bool Parser::isBlockStmtStart() const {
    return peek().content == "{";
}

bool Parser::isIfStmtStart() const {
    return peek().content == "if";
}

bool Parser::isElseStmtStart() const {
    return peek().content == "else";
}

bool Parser::isWhileStmtStart() const {
    return peek().content == "while";
}

bool Parser::isForStmtStart() const {
    return peek().content == "for";
}

bool Parser::isFuncDefineStmtStart() const {
    return peek().content == "func";
}


// --- Helper Functions (TDOP) ---

int Parser::getPrecedence(const std::string& op) const {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=") return 3;
    if (op == ">" || op == "<" || op == "<=" || op == ">=") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/" || op == "%") return 6;
    return 0; // 不是二元运算符
}

bool Parser::isBinaryOp() const {
    return getPrecedence(peek().content) > 0;
}


// --- Type Modifier Parsing ---

std::shared_ptr<BasicTypeModifierNode> Parser::parseBasicTypeModifierNode() {
    std::shared_ptr<Token> type_name = std::make_shared<Token>(next());
    if (type_name->type != TokenType::IDENTIFIER && type_name->type != TokenType::KEYWORD) {
        reportError("Expected a basic type name (identifier or keyword)");
    }
    return std::make_shared<BasicTypeModifierNode>(std::move(type_name));
}

std::shared_ptr<ArrayTypeModifierNode> Parser::parseArrayTypeModifierNode() {
    // 期望格式: [num]Type 或 []Type
    eat("[", "Expected '[' to start array type modifier.");
    std::shared_ptr<Token> number = nullptr;
    if (peek().content != "]") {
        number = std::make_shared<Token>(next());
        if (number->type != TokenType::LITERAL || number->info.details != "integer") {
            reportError("Expected an integer literal for array size, or ']' for dynamic array.");
        }
    }
    eat("]", "Expected ']' to close array type modifier.");
    
    std::shared_ptr<BasicTypeModifierNode> basic_type = parseBasicTypeModifierNode();

    return std::make_shared<ArrayTypeModifierNode>(std::move(number), std::move(basic_type));
}

std::shared_ptr<TypeModifierNode> Parser::parseTypeModifierNode() {
    if (peek().content == "[") {
        // 如果以 '[' 开头，则是 ArrayTypeModifierNode
        return std::make_shared<TypeModifierNode>(parseArrayTypeModifierNode());
    } 
    // 否则，它必须是 BasicTypeModifierNode
    return std::make_shared<TypeModifierNode>(parseBasicTypeModifierNode());
}

// --- Expression Parsing ---

std::shared_ptr<LiteralNode> Parser::parseLiteralNode() {
    if (!isLiteralNode()) {
        reportError("Expected a literal (number or string).");
    }
    return std::make_shared<LiteralNode>(std::make_shared<Token>(next()));
}

std::shared_ptr<IndexOpNode> Parser::parseIndexOpNode() {
    eat("[", "Expected '[' for index operator.");
    std::shared_ptr<WholeExprNode> index_expr = parseWholeExprNode();
    eat("]", "Expected ']' to close index operator.");
    return std::make_shared<IndexOpNode>(std::move(index_expr));
}

std::shared_ptr<CallingOpNode> Parser::parseCallingOpNode() {
    eat("(", "Expected '(' for function call.");
    std::vector<std::shared_ptr<WholeExprNode>> arguments;
    if (peek().content != ")") {
        do {
            arguments.push_back(parseWholeExprNode());
            if (peek().content != ",") break;
            next(); // eat ','
        } while (true);
    }
    eat(")", "Expected ')' to close function call.");
    return std::make_shared<CallingOpNode>(std::move(arguments));
}

std::shared_ptr<AtomIdentifierNode> Parser::parseAtomIdentifierNode() {
    std::shared_ptr<Token> field_token = std::make_shared<Token>(next());
    if (field_token->type != TokenType::IDENTIFIER) {
        reportError("Expected an identifier field or function name.");
    }
    
    std::shared_ptr<IndexOpNode> index_op = nullptr;
    std::shared_ptr<CallingOpNode> calling_op = nullptr;

    // 检查 IndexOp 或 CallingOp (可以串联)
    while (peek().content == "[" || peek().content == "(") {
        if (peek().content == "[") {
            if (index_op) reportError("Cannot have two index operators on the same atom.");
            index_op = parseIndexOpNode();
        } else if (peek().content == "(") {
            if (calling_op) reportError("Cannot have two calling operators on the same atom.");
            calling_op = parseCallingOpNode();
        }
    }
    
    return std::make_shared<AtomIdentifierNode>(
        std::move(field_token), std::move(index_op), std::move(calling_op)
    );
}

std::shared_ptr<IdentifierNode> Parser::parseIdentifierNode() {
    std::shared_ptr<AtomIdentifierNode> first_atom = parseAtomIdentifierNode();
    std::vector<std::shared_ptr<AtomIdentifierNode>> chain;
    
    while (peek().content == ".") {
        next(); // eat '.'
        chain.push_back(parseAtomIdentifierNode());
    }

    return std::make_shared<IdentifierNode>(std::move(first_atom), std::move(chain));
}

std::shared_ptr<IdentifierExprNode> Parser::parseIdentifierExprNode() {
    return std::make_shared<IdentifierExprNode>(parseIdentifierNode());
}

std::shared_ptr<PrimExprNode> Parser::parsePrimExprNode() {
    if (isLiteralNode()) {
        return std::make_shared<PrimExprNode>(parseLiteralNode());
    }
    else if (isIdentifierExprStart()) {
        return std::make_shared<PrimExprNode>(parseIdentifierExprNode());
    }
    else if (isArrayExprStart()) {
        return std::make_shared<PrimExprNode>(parseArrayExprNode());
    }
    else if (peek().content == "(") {
        next();
        std::shared_ptr<WholeExprNode> grouped_expr = parseWholeExprNode();
        eat(")", "Expected ')' to close grouped expression.");
        return std::make_shared<PrimExprNode>(std::move(grouped_expr));
    }
    reportError("Expected a primary expression (literal, identifier, or grouped expression).");
    return nullptr;
}

std::shared_ptr<ASTNode> Parser::parseBinaryExprNode(std::shared_ptr<PrimExprNode> lhs, int min_precedence) {
    std::vector<BinaryExprNode::ChainLink> links;

    while (isBinaryOp() && getPrecedence(peek().content) >= min_precedence) {
        std::shared_ptr<Token> op_token = std::make_shared<Token>(next());
        int precedence = getPrecedence(op_token->content);
        
        std::shared_ptr<ASTNode> rhs_base = parsePrimExprNode();
        std::shared_ptr<PrimExprNode> rhs = std::static_pointer_cast<PrimExprNode>(rhs_base);

        while (isBinaryOp() && getPrecedence(peek().content) > precedence) {
            rhs = std::static_pointer_cast<PrimExprNode>(parseBinaryExprNode(rhs, getPrecedence(peek().content)));
        }
        
        links.push_back({std::move(op_token), std::move(rhs)});
    }
    
    if (links.empty()) {
        return lhs;
    }
    
    return std::make_shared<BinaryExprNode>(std::move(lhs), std::move(links));
}


std::shared_ptr<ArrayExprNode> Parser::parseArrayExprNode() {
    std::shared_ptr<TypeModifierNode> type_modifier = parseTypeModifierNode();
    eat("[", "Expected '[' to start array literal.");
    
    std::vector<std::shared_ptr<WholeExprNode>> elements;
    if (peek().content != "]") {
        do {
            elements.push_back(parseWholeExprNode());
            if (peek().content != ",") break;
            next();
        } while (true);
    }
    eat("]", "Expected ']' to close array literal.");

    return std::make_shared<ArrayExprNode>(std::move(type_modifier), std::move(elements));
}

std::shared_ptr<AssignExprNode> Parser::parseAssignExprNode(std::shared_ptr<IdentifierExprNode> target) {
    std::shared_ptr<Token> assign_op = std::make_shared<Token>(next()); // Consumes '=' or compound op
    if (assign_op->content != "=" && assign_op->content != "+=" && assign_op->content != "-=") {
        reportError("Expected an assignment operator ('=', '+=', '-=' etc.).");
    }
    
    std::shared_ptr<WholeExprNode> value = parseWholeExprNode();

    return std::make_shared<AssignExprNode>(std::move(target), std::move(assign_op), std::move(value));
}

std::shared_ptr<WholeExprNode> Parser::parseWholeExprNode() {
    if (isAssignExprStart())
        return std::make_shared<WholeExprNode>(parseAssignExprNode(parseIdentifierExprNode()));
    else
        return std::make_shared<WholeExprNode>(parsePrimExprNode());
}

// --- Statement Parsing ---

std::shared_ptr<LetStmtNode> Parser::parseLetStmtNode() {
    std::shared_ptr<Token> mark = std::make_shared<Token>(next()); // let or const
    if (mark->content != "let" && mark->content != "const") {
        reportError("Expected 'let' or 'const'.");
    }
    
    std::shared_ptr<Token> field = eat(TokenType::IDENTIFIER, "Expected identifier after 'let'/'const'.");
    
    std::shared_ptr<TypeModifierNode> type_modifier = nullptr;
    if (peek().content == ":") {
        next(); // eat ':'
        type_modifier = parseTypeModifierNode();
    }
    
    std::shared_ptr<WholeExprNode> initial_value = nullptr;
    if (peek().content == "=") {
        next(); // eat '='
        initial_value = parseWholeExprNode();
    }
    
    std::shared_ptr<Token> semicolon = eat(";", "Expected ';' after let/const statement.");

    return std::make_shared<LetStmtNode>(
        std::move(mark), std::move(field), std::move(type_modifier), std::move(initial_value), std::move(semicolon)
    );
}

std::shared_ptr<ExprStmtNode> Parser::parseExprStmtNode() {
    std::shared_ptr<ASTNode> expression = parseWholeExprNode(); // 可能是 AssignExprNode 或 BinaryExprNode
    
    std::shared_ptr<Token> semicolon = eat(";", "Expected ';' after expression statement.");

    return std::make_shared<ExprStmtNode>(std::move(expression), std::move(semicolon)); 
}

std::shared_ptr<ReturnStmtNode> Parser::parseReturnStmtNode() {
    std::shared_ptr<Token> mark = eat("return", "Expected 'return'.");
    
    std::shared_ptr<WholeExprNode> expression = nullptr;
    if (peek().content != ";") {
        expression = parseWholeExprNode();
    }
    
    std::shared_ptr<Token> semicolon = std::make_shared<Token>(next());
    eat(";", "Expected ';' after return statement.");

    return std::make_shared<ReturnStmtNode>(std::move(mark), std::move(expression), std::move(semicolon));
}

std::shared_ptr<BlockStmtNode> Parser::parseBlockStmtNode() {
    std::shared_ptr<Token> left_brace = eat("{", "Expected '{' to start block statement.");

    std::vector<std::shared_ptr<ContainableStmtNode>> statements;
    while (peek().content != "}" && peek().type != TokenType::__EOF) {
        statements.push_back(parseContainableStmtNode());
    }

    std::shared_ptr<Token> right_brace = std::make_shared<Token>(next());
    eat("}", "Expected '}' to close block statement.");

    return std::make_shared<BlockStmtNode>(std::move(left_brace), std::move(statements), std::move(right_brace));
}

std::shared_ptr<IfStmtNode> Parser::parseIfStmtNode() {
    std::shared_ptr<Token> mark = eat("if", "Expected 'if'.");
    
    eat("(", "Expected '(' after 'if'.");
    std::shared_ptr<WholeExprNode> condition = parseWholeExprNode();
    eat(")", "Expected ')' after if condition.");
    
    std::shared_ptr<ASTNode> true_stmt = parseStmt(); // 递归调用 parseStmt，可以解析 BlockStmtNode 或 ContainableStmtNode
    
    std::shared_ptr<ASTNode> else_stmt = nullptr;
    if (isElseStmtStart()) {
        else_stmt = parseElseStmtNode();
    }
    
    return std::make_shared<IfStmtNode>(std::move(mark), std::move(condition), std::move(true_stmt), std::move(else_stmt));
}

std::shared_ptr<ElseStmtNode> Parser::parseElseStmtNode() {
    std::shared_ptr<Token> mark = eat("else", "Expected 'else'.");
    
    std::shared_ptr<ASTNode> false_stmt = parseStmt();

    return std::make_shared<ElseStmtNode>(std::move(mark), std::move(false_stmt));
}

std::shared_ptr<WhileStmtNode> Parser::parseWhileStmtNode() {
    std::shared_ptr<Token> mark = eat("while", "Expected 'while'.");

    eat("(", "Expected '(' after 'while'.");
    std::shared_ptr<WholeExprNode> condition = parseWholeExprNode();
    eat(")", "Expected ')' after while condition.");
    
    std::shared_ptr<ASTNode> body_stmt = parseStmt(); 

    return std::make_shared<WhileStmtNode>(std::move(mark), std::move(condition), std::move(body_stmt));
}

std::shared_ptr<ForStmtNode> Parser::parseForStmtNode() {
    std::shared_ptr<Token> mark = eat("for", "Expected 'for'.");
    
    eat("(", "Expected '(' after 'for'.");
    
    // Init Statement (必须是 let 语句，且必须有分号)
    std::shared_ptr<LetStmtNode> init_stmt = parseLetStmtNode();
    // parseLetStmtNode 内部已经消耗了 ';'
    
    // Condition Expression
    std::shared_ptr<WholeExprNode> condition = parseWholeExprNode();
    eat(";", "Expected ';' after for condition.");
    
    // Update Expression
    std::shared_ptr<WholeExprNode> update_expr = parseWholeExprNode();
    
    eat(")", "Expected ')' after for loop update expression.");
    
    std::shared_ptr<ASTNode> body_stmt = parseStmt();

    return std::make_shared<ForStmtNode>(
        std::move(mark), std::move(init_stmt), std::move(condition), std::move(update_expr), std::move(body_stmt)
    );
}

std::shared_ptr<FuncDefineStmtNode> Parser::parseFuncDefineStmtNode() {
    std::shared_ptr<Token> mark = eat("func", "Expected 'func'.");
    
    std::shared_ptr<Token> function_name_token = eat(TokenType::IDENTIFIER, "Expected function name.");
    
    eat("(", "Expected '(' after function name.");
    
    std::vector<FuncDefineStmtNode::Param> parameters;
    if (peek().content != ")") {
        do {
            std::string field = next().content;
            eat(":", "Expected ':' after parameter name.");
            std::shared_ptr<TypeModifierNode> type = parseTypeModifierNode();
            parameters.push_back({field, type});

            if (peek().content != ",") break;
            next(); // eat ','
        } while (true);
    }
    eat(")", "Expected ')' after function parameters.");
    
    eat("->", "Expected '->' for return type.");
    std::shared_ptr<TypeModifierNode> return_type = parseTypeModifierNode();

    std::shared_ptr<BlockStmtNode> body = parseBlockStmtNode();

    return std::make_shared<FuncDefineStmtNode>(
        std::move(mark), std::move(function_name_token), std::move(parameters), std::move(return_type), std::move(body)
    );
}


std::shared_ptr<ContainableStmtNode> Parser::parseContainableStmtNode() {
    if (isLetStmtStart()) {
        return std::make_shared<ContainableStmtNode>(parseLetStmtNode());
    } else if (isReturnStmtStart()) {
        return std::make_shared<ContainableStmtNode>(parseReturnStmtNode());
    } else if (isBlockStmtStart()) {
        return std::make_shared<ContainableStmtNode>(parseBlockStmtNode());
    } else if (isIfStmtStart()) {
        return std::make_shared<ContainableStmtNode>(parseIfStmtNode());
    } else if (isWhileStmtStart()) {
        return std::make_shared<ContainableStmtNode>(parseWhileStmtNode());
    } else if (isForStmtStart()) {
        return std::make_shared<ContainableStmtNode>(parseForStmtNode());
    } else if (isExprStmtStart()) {
        return std::make_shared<ContainableStmtNode>(parseExprStmtNode());
    } 
    
    reportError("Expected a valid statement (let, return, expr, block, if, while, for).");
    return nullptr;
}


std::shared_ptr<StmtNode> Parser::parseStmt() {
    if (isFuncDefineStmtStart())  {
        return std::make_shared<StmtNode>(parseFuncDefineStmtNode());
    }
    else {
        return std::make_shared<StmtNode>(parseContainableStmtNode());
    }
}


// --- Main Parsing Function ---

std::vector<std::shared_ptr<StmtNode>> Parser::parseProgram() {
    std::vector<std::shared_ptr<StmtNode>> program_statements;
    while (peek().type != TokenType::__EOF) {
        try {
            // 顶层的 else 语句不合法
            if (isElseStmtStart()) {
                reportError("ElseStmt cannot be the first statement in a program or stand alone.");
            }
            
            program_statements.push_back(parseStmt());

        } catch (const std::exception& e) {
            std::cerr << "Parsing failed: " << e.what() << std::endl;
            // 简单错误恢复: 尝试前进到下一个潜在的语句开始或 EOF
            while (peek().type != TokenType::__EOF && 
                   !isLetStmtStart() && !isFuncDefineStmtStart() && !isIfStmtStart() &&
                   !isWhileStmtStart() && !isForStmtStart()) {
                next();
            }
            if (peek().type == TokenType::__EOF) break;
        }
    }
    return program_statements;
}