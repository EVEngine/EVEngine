#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

int main() {
    eve::asset_import::UnityProjectImportRequest request;
    eve::asset_import::UnitySourceAsset          source;
    source.path = "Assets/mesh.fbx";
    auto result = eve::asset_import::prepareUnityFbx(request, source);
    return !result && result.error()->code() == eve::DiagnosticCode::Unsupported ? 0 : 1;
}
