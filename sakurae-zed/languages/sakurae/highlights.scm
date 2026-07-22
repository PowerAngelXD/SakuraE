[
  "if" "else" "while" "for" "func"
  "return" "let" "const" "range"
  "break" "continue" "match" "repeat"
  "struct" "impl" "is" "typeof" "ref" "default" "sizeof"
] @keyword

[
  "i32" "i64" "ui32" "ui64" "f32" "f64" "bool" "char" "string" "void"
] @type

(number) @constant.numeric
(string) @string
(char) @string.special
(boolean) @constant.builtin.boolean

(identifier) @variable

((identifier) @keyword
  (#match? @keyword "^(const|struct|impl|is|typeof|sizeof)$"))

(func_define_stmt "func" @keyword)
(func_define_stmt (identifier) @function)
(atom_identifier_expr (identifier) @function.call (calling_op))

(type_modifier (identifier)) @type

(match_stmt "default" @keyword)

[
  "==" "!=" ">" "<" "<=" ">="
  "||" "&&" "!"
  "+" "-" "*" "/" "%"
  "=" "+=" "-=" "/=" "*="
  "->" "=>" "&" "++" "--"
] @operator

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
[";" "," ":" "."] @punctuation.delimiter

(comment) @comment
