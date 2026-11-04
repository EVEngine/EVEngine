dofile("definitions.nut");
dofile("suite.nut");
balanceReport <- null;
balanceEvidence <- null;
function balanceValue(result) {
    if (!result.ok) throw result.diagnostics[0].message;
    return result.value;
}
eve_init <- function() {
    balanceRegisterDefinitions();
    local suite = eve.RPGBalanceSuite();
    local configured = suite.configure(balanceSuiteSpec());
    if (!configured.ok) throw configured.diagnostics[0].message;
    local spec = balanceSearchSpec(balanceValue(suite.targets()));
    local initial = {};
    foreach (p in spec.parameters) initial[p.name] <- p.initial;
    local before = balanceValue(suite.measure(initial,spec.validationSeeds,spec.dt,spec.steps));
    local search = eve.BalanceSearch();
    configured = search.configure(spec);
    if (!configured.ok) throw configured.diagnostics[0].message;
    local evaluator = {suite=suite, evaluate=function(parameters,seed,dt,steps) {
        return balanceValue(suite.measure(parameters,[seed],dt,steps)).metrics;
    }};
    balanceReport = balanceValue(search.run(evaluator));
    local after = balanceValue(suite.measure(balanceReport.parameters,spec.validationSeeds,spec.dt,spec.steps));
    balanceEvidence = {search=balanceReport,before=before,after=after};
    print(format("[rpg-balance] %s, training loss %.4f -> %.4f, holdout %.4f -> %.4f\n",
        balanceReport.status,balanceReport.baseline.loss,balanceReport.tuned.loss,
        balanceReport.baselineValidation.loss,balanceReport.validation.loss));
    foreach (i,left in after.builds) {
        local row = left + ":";
        foreach (j,right in after.builds) row += format(" %s=%.3f",right,after.matrix[i][j]);
        print(row + "\n");
    }
    print(format("[rpg-balance] %d strategy/scenario cells, %d holdout bouts; all production RPG settlements\n",
        after.cells.len(),after.matches));
};
function balanceVerify() {
    if (balanceReport == null) throw "No evidence";
    if (balanceReport.tuned.loss >= balanceReport.baseline.loss) throw "Training did not improve";
    if (balanceReport.validation.loss > balanceReport.baselineValidation.loss) throw "Holdout regressed";
    local matrix = balanceEvidence.after.matrix;
    for (local i=0;i<matrix.len();++i) {
        if (matrix[i][i] != 0.5) throw "Invalid diagonal";
        for (local j=0;j<matrix.len();++j)
            if (fabs(matrix[i][j]+matrix[j][i]-1.0)>0.00001) throw "Nonreciprocal matchup";
    }
    local mana = 0.0;
    local guard = 0, heal = 0, poison = 0, poisonTicks = 0;
    foreach (cell in balanceEvidence.after.cells) {
        mana += cell.meanManaSpent;
        if ("balance.guard" in cell.skills) guard += cell.skills["balance.guard"];
        if ("balance.heal" in cell.skills) heal += cell.skills["balance.heal"];
        if ("balance.poison" in cell.skills) poison += cell.skills["balance.poison"];
        foreach (event in cell.trace)
            if (event.skill == "balance.poison" && event.action == "damage" && event.amount > 0)
                poisonTicks++;
    }
    if (mana <= 0) throw "Skills did not pay canonical resource costs";
    if (guard <= 0 || heal <= 0 || poison <= 0 || poisonTicks <= 0)
        throw "Defense, healing, or periodic settlement was not exercised";
    print("[rpg-balance] verification passed\n");
}
eve_update <- function(dt) {};
eve_render <- function() {
    gfx.clear();
    if (balanceEvidence == null) return;
    foreach (i,row in balanceEvidence.after.matrix) foreach (j,score in row)
        gfx.drawSolidRect(180.0+j*130.0,70.0+i*105.0,112.0,88.0,
            0.15+0.65*(1.0-score),0.15+0.65*score,0.25,1.0);
};
