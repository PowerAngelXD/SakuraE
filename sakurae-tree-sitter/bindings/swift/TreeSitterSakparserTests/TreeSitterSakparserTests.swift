import XCTest
import SwiftTreeSitter
import TreeSitterSakparser

final class TreeSitterSakparserTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_sakparser())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading Sakparser grammar")
    }
}
