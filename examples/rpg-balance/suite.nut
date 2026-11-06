function balanceSuiteSpec() {
    return {
        schema="evengine.rpg.balance.suite", version=1,
        builds=[
            {id="warrior",classId="balance.warrior",
                attributes={hp=240,mana=60,atk=18,def=10,mdf=6,speed=12,power=1.8},
                skills=["balance.strike","balance.guard"],
                equipment=[{slot="weapon",stats=[{attribute="atk",op="add",value=8}]}]},
            {id="mage",classId="balance.mage",
                attributes={hp=180,mana=95,atk=6,mat=20,def=5,mdf=12,speed=10,power=6.0},
                skills=["balance.fire","balance.frost","balance.heal"],
                equipment=[{slot="weapon",stats=[{attribute="mat",op="add",value=14}]}]},
            {id="ranger",classId="balance.ranger",
                attributes={hp=210,mana=65,atk=17,def=7,mdf=8,speed=14,power=1.5},
                skills=["balance.arrow","balance.poison"],
                equipment=[{slot="weapon",stats=[{attribute="atk",op="add",value=7}]}]},
            {id="mage_guard",classId="balance.mage",
                attributes={hp=205,mana=85,atk=6,mat=17,def=9,mdf=15,speed=9,power=2.2},
                skills=["balance.fire","balance.frost","balance.heal"],
                equipment=[{slot="weapon",stats=[{attribute="mat",op="add",value=7}]}]}
        ],
        strategies=[
            {id="burst",priority=["burst","attack","dot","heal","defense"],healBelow=0.25,reserveMana=0.0},
            {id="sustain",priority=["heal","defense","dot","attack","burst"],healBelow=0.6,reserveMana=0.2}
        ],
        scenarios=[
            {id="short_resource",healthScale=1.0,manaScale=0.6,manaRegen=0.0},
            {id="long_resource",healthScale=1.5,manaScale=1.0,manaRegen=2.0}
        ],
        bindings=[
            {parameter="warrior.power",build="warrior",attribute="power"},
            {parameter="mage.power",build="mage",attribute="power"},
            {parameter="ranger.power",build="ranger",attribute="power"},
            {parameter="mage.weapon",build="mage",slot="weapon",attribute="mat"}
        ],
        matchups=[
            {left="warrior",right="ranger",min=0.5,max=0.9},
            {left="mage",right="warrior",min=0.5,max=0.9},
            {left="ranger",right="mage",min=0.5,max=0.9},
            {left="mage_guard",right="mage",min=0.2,max=0.8}
        ]
    };
}
function balanceSearchSpec(targets) {
    return {
        schema="evengine.balance.problem",version=1,
        parameters=[
            {name="warrior.power",initial=1.8,min=1.0,max=4.0},
            {name="mage.power",initial=6.0,min=1.0,max=7.0},
            {name="ranger.power",initial=1.5,min=1.0,max=4.0},
            {name="mage.weapon",initial=14.0,min=3.0,max=18.0}
        ],
        targets=targets,candidates=8,steps=16,dt=0.5,searchSeed="7",changeWeight=0.005,
        trainingSeeds=[11,29],validationSeeds=[401,409]
    };
}
