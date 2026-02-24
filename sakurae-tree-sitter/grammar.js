const PREC = {
    ASSIGN: 1,
    BINARY: 2,
    LOGIC: 3,
    ADD: 4,
    MUL: 5,
    UNARY: 6,
    CALL: 7,
    MEMBER: 8,
};

module.exports = grammar({
    name: "sakurae",

    extras: ($) => [/\s/, $.comment],

    word: ($) => $._field,

    conflicts: ($) => [[$.if_stmt], [$.prim_expr, $.atom_identifier_expr]],

    rules: {
        source_file: ($) => repeat($.stmt),

        _field: ($) => /[a-zA-Z_][a-zA-Z0-9_]*/,

        identifier: ($) => $._field,

        comment: ($) =>
            token(
                choice(
                    seq("//", /.*/),
                    seq("/*", /[^*]*\*+([^/*][^*]*\*+)*/, "/"),
                ),
            ),

        number: ($) =>
            token(/\d+(\.\d+)?[f]?|0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+/),
        string: ($) => token(seq('"', repeat(/[^"\\]|\\./), '"')),
        char: ($) => token(/'([^'\\\\]|\\.)'/),
        boolean: ($) => choice("true", "false"),

        literal: ($) => choice($.number, $.string, $.boolean, $.char),

        whole_expr: ($) =>
            choice(
                $.assign_expr,
                $.binary_expr,
                $.logic_expr,
                $.add_expr,
                $.mul_expr,
                $.prim_expr,
            ),

        assign_expr: ($) =>
            prec.right(
                PREC.ASSIGN,
                seq(
                    $.identifier_expr,
                    choice("=", "+=", "-=", "/=", "*="),
                    $.whole_expr,
                ),
            ),

        binary_expr: ($) =>
            prec.left(
                PREC.BINARY,
                seq(
                    choice(
                        $.binary_expr,
                        $.logic_expr,
                        $.add_expr,
                        $.mul_expr,
                        $.prim_expr,
                    ),
                    choice("||", "&&"),
                    $.whole_expr,
                ),
            ),

        logic_expr: ($) =>
            prec.left(
                PREC.LOGIC,
                seq(
                    choice($.logic_expr, $.add_expr, $.mul_expr, $.prim_expr),
                    choice("==", "!=", ">", "<", "<=", ">="),
                    choice($.logic_expr, $.add_expr, $.mul_expr, $.prim_expr),
                ),
            ),

        add_expr: ($) =>
            prec.left(
                PREC.ADD,
                seq(
                    choice($.add_expr, $.mul_expr, $.prim_expr),
                    choice("+", "-"),
                    choice($.add_expr, $.mul_expr, $.prim_expr),
                ),
            ),

        mul_expr: ($) =>
            prec.left(
                PREC.MUL,
                seq(
                    choice($.mul_expr, $.prim_expr),
                    choice("*", "/"),
                    choice($.mul_expr, $.prim_expr),
                ),
            ),

        array_expr: ($) => seq("[", commaSep1($.whole_expr), "]"),

        prim_expr: ($) =>
            choice($.literal, $.identifier_expr, seq("(", $.whole_expr, ")")),

        identifier_expr: ($) =>
            prec(
                PREC.UNARY,
                seq(
                    optional(choice("!", "++", "--", "*", "&")),
                    $.atom_identifier_expr,
                    optional(seq(".", $.identifier)),
                    repeat(choice("++", "--")),
                ),
            ),

        atom_identifier_expr: ($) =>
            seq(
                choice($.identifier, seq("(", $.identifier_expr, ")")),
                repeat(choice($.index_op, $.calling_op)),
            ),

        index_op: ($) => seq("[", $.whole_expr, "]"),
        calling_op: ($) => seq("(", optional(commaSep($.whole_expr)), ")"),

        type_modifier: ($) =>
            seq(
                choice(
                    "i32",
                    "i64",
                    "ui32",
                    "ui64",
                    "f32",
                    "f64",
                    "bool",
                    "char",
                    "string",
                    $.identifier,
                ),
                repeat(seq("[", $.number, "]")),
                repeat("*"),
            ),

        stmt: ($) =>
            choice(
                $.declare_stmt,
                $.expr_stmt,
                $.block_stmt,
                $.if_stmt,
                $.while_stmt,
                $.for_stmt,
                $.func_define_stmt,
                $.return_stmt,
                $.break_stmt,
                $.continue_stmt,
            ),

        declare_stmt: ($) =>
            seq(
                "let",
                $.identifier,
                optional(seq(":", $.type_modifier)),
                "=",
                $.whole_expr,
                ";",
            ),

        expr_stmt: ($) => seq(choice($.identifier_expr, $.assign_expr), ";"),

        block_stmt: ($) => seq("{", repeat($.stmt), "}"),

        if_stmt: ($) =>
            prec.right(
                seq(
                    "if",
                    "(",
                    $.whole_expr,
                    ")",
                    $.stmt,
                    optional(seq("else", $.stmt)),
                ),
            ),

        while_stmt: ($) => seq("while", "(", $.whole_expr, ")", $.stmt),

        for_stmt: ($) =>
            seq(
                "for",
                "(",
                optional($.declare_stmt),
                optional($.whole_expr),
                ";",
                optional($.whole_expr),
                ")",
                $.stmt,
            ),

        func_define_stmt: ($) =>
            seq(
                "func",
                $.identifier,
                "(",
                commaSep(seq($.identifier, ":", $.type_modifier)),
                ")",
                "->",
                $.type_modifier,
                $.block_stmt,
            ),

        return_stmt: ($) => seq("return", optional($.whole_expr), ";"),
        break_stmt: ($) => seq("break", ";"),
        continue_stmt: ($) => seq("continue", ";"),
    },
});

function commaSep(rule) {
    return optional(commaSep1(rule));
}
function commaSep1(rule) {
    return seq(rule, repeat(seq(",", rule)));
}
