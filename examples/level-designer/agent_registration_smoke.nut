// Diagnostic contract check; run after level_agent.nut and level_transactions.nut.
// Restore the real eve table even when a check fails. No real MCP tools change.
local originalEve = eve;
local registrations = {};
try {
    eve = {};
    whiteboxRegisterAgentTools(); // UI-only startup must work without DevTools.
    eve = {mcp = {tool = function(name, specification) {
        if (name in registrations) throw "duplicate agent tool: " + name;
        registrations[name] <- specification;
        return true;
    }}};
    whiteboxRegisterAgentTools();
    if (registrations.len() != 9 || !("whitebox_transaction_v1" in registrations))
        throw "MCP startup must register all nine whitebox tools";
    eve.mcp.tool = function(name, specification) { return false; };
    local refused = false;
    try { whiteboxRegisterAgentTools(); } catch (error) { refused = true; }
    if (!refused) throw "An enabled MCP registration failure must remain fatal";
} catch (error) {
    eve = originalEve;
    throw error;
}
eve = originalEve;
print("WHITEBOX_AGENT_REGISTRATION_PASS\n");
