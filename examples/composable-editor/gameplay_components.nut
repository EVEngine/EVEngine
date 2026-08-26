// Project-owned adapters combine generic Schema/Definitions with Card, Crowd,
// terrain and ECS. None of these policies or UI choices live in C++.
//
// Annotation-style schema: class-level `</ ... />` carries the metadata,
// member-level `</ ... />` carries the per-field constraints, and each member's
// default value infers its type and JSON default. The schema classes live at
// file scope: Squirrel only supports class-level `</ ... />` attributes on
// top-level classes.
class CardDefinition </ id="card.definition", version=1, title="Card Definition", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ required=true />
    name = ""
    </ values=["creature","spell"] />
    kind = "creature"
    </ min=0, max=20 />
    cost = 0
    </ min=0, max=99 />
    attack = 0
    </ min=0, max=99 />
    health = 0
}
class RtsUnit </ id="rts.unit", version=1, title="RTS Unit", additionalProperties=false /> {
    </ required=true />
    id = ""
    </ min=0, max=20 />
    speed = 0.0
    </ min=0.1, max=5 />
    radius = 0.0
}

class GameplayEditorComponents {
    schemas = null
    definitions = null
    cards = null
    crowdSim = null
    deck = null
    units = null

    constructor() {
        schemas = eve.Schema();
        definitions = eve.Definitions().newRegistry();
        cards = eve.Card();
        crowdSim = eve.Crowd();
        units = [];

        if (!schemas.registerFromClass(CardDefinition) ||
            !schemas.registerFromClass(RtsUnit)) return;

        local cardJson = @"{""id"":""card.scout"",""name"":""Terrain Scout"",""kind"":""creature"",""cost"":2,""attack"":3,""health"":2}";
        if (schemas.validateJson("card.definition", cardJson)) {
            definitions.registerDefinition("card", "card.scout", 1, cardJson);
            cards.registerCardsFromJson(cardJson);
        }
        deck = cards.newDeck();
    }

    // Convert the live terrain into movement cost. The gameplay simulator and
    // terrain editor therefore share data without either depending on a UI.
    function bindTerrain(heightmap, cellSize) {
        crowdSim.clearAgents();
        crowdSim.resizeField(heightmap.getWidth(), heightmap.getHeight(), cellSize, 0.0, 0.0);
        for (local y = 0; y < heightmap.getHeight(); ++y)
            for (local x = 0; x < heightmap.getWidth(); ++x)
                crowdSim.setCellCost(x, y, 1.0 + abs(heightmap.height(x, y)) * 3.0);
        crowdSim.buildFlowField(heightmap.getWidth() / 2, heightmap.getHeight() / 2);
    }

    function createCardInstance() {
        local instance = cards.newCard("card.scout");
        if (instance == null) return null;
        deck.push(instance);
        cards.capturePresentation();
        return instance;
    }

    function createRtsUnit(stableId, entity, x, z) {
        local unitJson = "{\"id\":\"" + stableId + "\",\"speed\":6,\"radius\":0.35}";
        if (!schemas.validateJson("rts.unit", unitJson)) return false;
        definitions.registerDefinition("rts.unit", stableId, 1, unitJson);
        local index = crowdSim.addNamedAgent(stableId, x, z, 0.0, 0.35);
        if (index < 0) return false;
        crowdSim.setAgentSpeed(index, 6.0);
        units.push({ id=stableId, entity=entity });
        return true;
    }

    function update(dt) {
        crowdSim.step(dt);
        foreach (unit in units) {
            local index = crowdSim.getNamedAgentIndex(unit.id);
            if (index < 0) continue;
            local value = crowdSim.getAgentState(index);
            unit.entity.position.x = value.x;
            unit.entity.position.z = value.y;
        }
    }
}
