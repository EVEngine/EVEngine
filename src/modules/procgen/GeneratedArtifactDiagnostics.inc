// Private pure diagnostic builders shared by store and recipe projections.
eve::Result<GeneratedArtifact> rejectedArtifact(eve::DiagnosticCode code, std::string message) {
    return eve::Result<GeneratedArtifact>::failure(eve::Diagnostic::error(code, message, "generatedArtifact"));
}

eve::Result<ArtifactPart> rejectedPart(eve::DiagnosticCode code, std::string message) {
    return eve::Result<ArtifactPart>::failure(eve::Diagnostic::error(code, message, "generatedArtifact.part"));
}
