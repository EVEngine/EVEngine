// Project-owned concept schemas. The engine does not ship Nation/City/Officer
// types — these annotated classes register through eve.Schema so content JSON
// can be validated before it enters Definitions / Social / Attributes.

class NationDef </ id="sk10.nation", version=1, title="Nation", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ required=true />
    name = ""
    </ required=true />
    color = ""
    </ required=true />
    capitalCityId = ""
}

class RegionDef </ id="sk10.region", version=1, title="Region / Theater", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ required=true />
    name = ""
    </ required=true />
    theater = ""
}

class CityDef </ id="sk10.city", version=1, title="City", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ required=true />
    name = ""
    </ required=true />
    regionId = ""
    </ min=40.0, max=1200.0 />
    mapX = 0.0
    </ min=40.0, max=700.0 />
    mapY = 0.0
    </ min=0, max=100 />
    agriculture = 50
    </ min=0, max=100 />
    commerce = 50
    </ min=0, max=100 />
    defense = 50
    </ min=0 />
    population = 10000
}

class OfficerDef </ id="sk10.officer", version=1, title="Officer", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ required=true />
    name = ""
    </ min=1, max=100 />
    leadership = 50
    </ min=1, max=100 />
    war = 50
    </ min=1, max=100 />
    intellect = 50
    </ min=1, max=100 />
    politics = 50
    </ min=1, max=100 />
    charm = 50
    </ min=1, max=100 />
    loyalty = 70
    </ min=16, max=90 />
    age = 30
}

class ArmyDef </ id="sk10.army", version=1, title="Army / Legion", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ required=true />
    name = ""
    </ min=100, max=50000 />
    troops = 5000
}

class TaskDef </ id="sk10.task", version=1, title="Domestic Task", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ required=true />
    name = ""
    </ values=["farm", "commerce", "recruit", "fortify"] />
    kind = "farm"
    </ min=1, max=12 />
    months = 1
}

function registerSangokushiConcepts(schemas) {
    if (!schemas.registerFromClass(NationDef).ok) return false;
    if (!schemas.registerFromClass(RegionDef).ok) return false;
    if (!schemas.registerFromClass(CityDef).ok) return false;
    if (!schemas.registerFromClass(OfficerDef).ok) return false;
    if (!schemas.registerFromClass(ArmyDef).ok) return false;
    if (!schemas.registerFromClass(TaskDef).ok) return false;
    return true;
}

function conceptJson(obj) {
    local encoded = eve.json.encode(obj);
    if (!encoded.ok) throw encoded.status.summary;
    return encoded.value;
}

function validateAndInsert(schemas, definitions, schemaId, typeName, id, obj) {
    local json = conceptJson(obj);
    local validated = schemas.validateJson(schemaId, json);
    if (!validated.ok) throw validated.status.summary;
    local inserted = definitions.insert(typeName, id, 1, json);
    if (!inserted.ok) throw inserted.status.summary;
    return json;
}
