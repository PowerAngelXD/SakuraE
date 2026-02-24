import XCTest
import SwiftTreeSitter
import TreeSittersakurae

final class TreeSittersakuraeTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_sakurae())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading sakurae grammar")
    }
}
