package tree_sitter_sakurae_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_sakurae "github.com/powerangelxd/sakurae/tree/master/sakurae-tree-sitter/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_sakurae.Language())
	if language == nil {
		t.Errorf("Error loading sakurae grammar")
	}
}
