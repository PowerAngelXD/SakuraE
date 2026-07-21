[
  "if" "else" "for" "while"
  "return" "break" "continue"
  "let" "range" "repeat" "match" "default" "ref"
  "i32" "i64" "ui32" "ui64" "f32" "f64" "bool" "char" "string" "void"
] @keyword

(number) @constant.numeric
(string) @string
(char) @string.special
(boolean) @constant.builtin.boolean

(func_define_stmt (identifier) @function)
(atom_identifier_expr (identifier) @function.call (calling_op))
(identifier) @variable

(type_modifier (identifier)) @type

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
