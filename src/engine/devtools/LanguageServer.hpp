#pragma once

#include "common/Export.h"
#include "common/ScriptCompiler.h"
#include "devtools/LanguageIndex.h"
#include "devtools/LanguageText.h"

#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {

/** @brief One parameter in an LSP signature-help result. */
struct LanguageSignatureParameter {
    std::string label;
    std::string documentation;
};

/** @brief Binding-contract signature shown while typing a call. */
struct LanguageSignatureHelp {
    std::string                              label;
    std::string                              documentation;
    std::vector<LanguageSignatureParameter>  parameters;
    int                                      activeParameter = 0;
};

/**
 * @brief In-process EveScript language service used by `eve language-server`
 * and by unit tests. Owns a dedicated Runtime so it can compile without a
 * running game. JSON-RPC is optional; typed methods are the canonical API.
 */
class EVENGINE_API LanguageServer {
public:
    explicit LanguageServer(std::string projectRoot);
    ~LanguageServer();

    LanguageServer(const LanguageServer&)            = delete;
    LanguageServer& operator=(const LanguageServer&) = delete;

    /** @brief Absolute project root used to resolve `game:/` URIs. */
    const std::string& projectRoot() const noexcept;

    /** @brief Converts an on-disk path to the `file://` URI the server expects. */
    static std::string fileUriFromPath(std::string_view path);

    void openDocument(std::string uri, std::string text);
    /** @brief Replaces the whole document (full sync). */
    void changeDocument(std::string uri, std::string text);
    /** @brief Applies one incremental LSP range edit, then re-indexes. */
    void changeDocument(std::string uri, lsp::Range range, std::string text);
    void closeDocument(std::string uri);

    std::vector<script::ScriptDiagnostic> diagnosticsFor(std::string_view uri) const;
    std::vector<script::ScriptCompletion> complete(std::string_view uri, lsp::Position position) const;
    std::optional<script::ScriptHover>    hover(std::string_view uri, lsp::Position position) const;
    std::optional<lsp::Location>          definition(std::string_view uri, lsp::Position position) const;
    std::vector<lsp::Location>            references(std::string_view uri, lsp::Position position,
                                                     bool includeDeclaration) const;
    std::optional<std::vector<lsp::TextEdit>> rename(std::string_view uri, lsp::Position position,
                                                     std::string_view newName) const;
    std::vector<script::ScriptSymbolMetadata> documentSymbols(std::string_view uri) const;
    std::optional<LanguageSignatureHelp>      signatureHelp(std::string_view uri, lsp::Position position) const;
    /**
     * @brief Formats an open document (brace/bracket indent, trailing whitespace).
     * @param range when set, the returned edit covers those lines; indent still uses the whole file.
     */
    std::vector<lsp::TextEdit> formatDocument(std::string_view uri, lsp::FormatOptions options = {},
                                              const lsp::Range* range = nullptr) const;
    /** @brief Foldable brace, bracket, and block-comment ranges in an open document. */
    std::vector<lsp::FoldingRange> foldingRanges(std::string_view uri) const;
    /** @brief Semantic highlight tokens for an open document (class / function / variable / …). */
    std::vector<lsp::SemanticToken> semanticTokens(std::string_view uri) const;

    /**
     * @brief Dispatches one LSP JSON-RPC body and appends Content-Length frames.
     * @return false after an `exit` notification.
     */
    bool handleMessage(std::string_view jsonBody, std::ostream& output);

    /** @brief Stdio LSP host. Returns 0 after a clean shutdown. */
    int runStdio(std::istream& input, std::ostream& output);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::dev
