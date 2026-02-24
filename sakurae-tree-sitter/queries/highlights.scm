["func" "let" "if" "else" "while" "for" "return" "break" "continue"] @keyword

["=" "+=" "-=" "*=" "/=" "==" "!=" ">" "<" ">=" "<=" "||" "&&" "+" "-" "*" "/" "!" "++" "--" "->" "&"] @operator

(type_modifier) @type

(func_define_stmt
  (identifier) @function)

(calling_op) @function.call

(number) @number
(string) @string
(boolean) @constant.builtin
(char) @string.special

["(" ")" "[" "]" "{" "}" ":" ";"] @punctuation.bracket
"," @punctuation.delimiter

(identifier) @variable

(comment) @comment
