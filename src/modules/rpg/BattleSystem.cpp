#include "rpg/BattleSystem.h"

#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/TraitSystem.h"

#include <cctype>
#include <cstdlib>
#include <random>
#include <unordered_map>

namespace eve::rpg {

namespace {

std::unordered_map<std::string, SkillDamageSpec> &skillDamageTable() {
    static std::unordered_map<std::string, SkillDamageSpec> t;
    return t;
}

}  // namespace

void BattleSystem::registerSkillDamage(const std::string &skillId, const SkillDamageSpec &spec) {
    if (skillId.empty()) return;
    skillDamageTable()[skillId] = spec;
}

const SkillDamageSpec *BattleSystem::findSkillDamage(const std::string &skillId) {
    auto &t = skillDamageTable();
    auto it = t.find(skillId);
    return it == t.end() ? nullptr : &it->second;
}

void BattleSystem::clearSkillDamage() { skillDamageTable().clear(); }

namespace {

// ---- 极简表达式求值器：支持数字、a.<param>、b.<param>、+ - * / ( ) ----
struct FormulaParser {
    const std::string &s;
    size_t pos = 0;
    RPGActor *a;
    RPGActor *b;

    FormulaParser(const std::string &str, RPGActor *atk, RPGActor *tgt)
        : s(str), a(atk), b(tgt) {}

    void skipSpace() {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }

    double parseExpression() {
        double lhs = parseTerm();
        while (true) {
            skipSpace();
            if (pos >= s.size()) break;
            const char c = s[pos];
            if (c == '+') {
                ++pos;
                lhs += parseTerm();
            } else if (c == '-') {
                ++pos;
                lhs -= parseTerm();
            } else {
                break;
            }
        }
        return lhs;
    }

    double parseTerm() {
        double lhs = parseFactor();
        while (true) {
            skipSpace();
            if (pos >= s.size()) break;
            const char c = s[pos];
            if (c == '*') {
                ++pos;
                lhs *= parseFactor();
            } else if (c == '/') {
                ++pos;
                const double rhs = parseFactor();
                lhs = rhs == 0.0 ? 0.0 : lhs / rhs;
            } else {
                break;
            }
        }
        return lhs;
    }

    double parseFactor() {
        skipSpace();
        if (pos < s.size() && s[pos] == '(') {
            ++pos;
            double v = parseExpression();
            skipSpace();
            if (pos < s.size() && s[pos] == ')') ++pos;
            return v;
        }
        if (pos < s.size() && (s[pos] == 'a' || s[pos] == 'b') && pos + 1 < s.size() &&
            s[pos + 1] == '.') {
            const bool isA = s[pos] == 'a';
            pos += 2;
            std::string name;
            while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) ||
                                      s[pos] == '_')) {
                name.push_back(s[pos++]);
            }
            RPGActor *actor = isA ? a : b;
            return actor ? AttributeSystem::getFinal(actor, name) : 0.0;
        }
        if (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) {
            std::string num;
            while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) ||
                                      s[pos] == '.')) {
                num.push_back(s[pos++]);
            }
            return std::strtod(num.c_str(), nullptr);
        }
        return 0.0;
    }
};

}  // namespace

double BattleSystem::evaluateFormula(const std::string &formula, RPGActor *attacker,
                                     RPGActor *target) {
    FormulaParser parser(formula, attacker, target);
    return parser.parseExpression();
}

DamageResult BattleSystem::resolveHit(RPGActor *attacker, RPGActor *target,
                                      const SkillDamageSpec &spec, unsigned seed) {
    DamageResult result;
    if (!attacker || !target) return result;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    // 命中判定：攻击方 hit + (100 - 目标 evasion)
    double attackerHit = 100.0 + TraitSystem::getExParam(attacker, "hit");
    double targetEvasion = TraitSystem::getExParam(target, "evasion");
    double hitChance = spec.hitChance + attackerHit - targetEvasion;
    if (dist(rng) * 100.0 >= hitChance) {
        result.hit = false;
        return result;
    }

    // 公式（默认物理：a.atk - b.def，至少 1）
    double amount;
    if (spec.formula.empty()) {
        amount = AttributeSystem::getFinal(attacker, "attack") -
                 AttributeSystem::getFinal(target, "defense");
    } else {
        amount = evaluateFormula(spec.formula, attacker, target);
    }
    if (amount <= 0.0) amount = 1.0;

    // 元素耐性
    if (!spec.element.empty()) {
        result.elementRate = TraitSystem::getElementRate(target, spec.element);
        amount *= result.elementRate;
    }

    // 暴击（RPG Maker 默认 3x）
    double critChance = spec.critChance + TraitSystem::getExParam(attacker, "critRate");
    if (critChance > 0.0 && dist(rng) < critChance) {
        result.crit = true;
        amount *= 3.0;
    }

    result.amount = amount;
    return result;
}

}  // namespace eve::rpg