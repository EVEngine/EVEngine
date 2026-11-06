// Native NpcAiWorld behavior: patrol <-> chase driven by vision memory signals.
// remember(..., sense="vision") queues perception.vision;
// forget / expiry queues perception.forgotten.vision.
function enemyVisionBehavior() {
    return @"{""id"":""enemy.vision"",""schemaVersion"":1,""initialState"":""patrol"",""blackboardSchema"":[{""key"":""target"",""type"":""String"",""required"":false}],""states"":[{""id"":""patrol"",""transitions"":[{""targetState"":""chase"",""signal"":""perception.vision"",""priority"":10,""conditions"":[{""key"":""target"",""op"":""Exists""}]}]},{""id"":""chase"",""enterConditions"":[{""key"":""target"",""op"":""Exists""}],""transitions"":[{""targetState"":""patrol"",""signal"":""perception.forgotten.vision"",""priority"":10}]}]}";
}
