from unittest import TestCase

import tree_sitter_sakurae

from tree_sitter import Language, Parser


class TestLanguage(TestCase):
    def test_can_load_grammar(self):
        try:
            Parser(Language(tree_sitter_sakurae.language()))
        except Exception:
            self.fail("Error loading sakurae grammar")
