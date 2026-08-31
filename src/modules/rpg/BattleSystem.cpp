#include "rpg/BattleSystem.h"

#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/TraitSystem.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <random>
#include <unordered_map>

namespace eve::rpg {

namespace {

std::unordered_map<std::string, SkillDamageSpec> &skillDamageTable() {
    static std::unordered_map<std::string, SkillDamageSpec> t;
    return t;
}

eve::Result<double> parseFormula(const std::string &formula, RPGActor *attacker, RPGActor *target,
                                 bool validateOnly);

}  // namespace

eve::Result<void> BattleSystem::registerSkillDamageChecked(const std::string &skillId,
                                                            const SkillDamageSpec &spec) {
    if (skillId.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "skill damage id must not be empty", "skillId"));
    if (!spec.formula.empty()) {
        auto formula = parseFormula(spec.formula, nullptr, nullptr, true);
        if (!formula) return eve::Result<void>::failure(formula.status());
    }
    skillDamageTable()[skillId] = spec;
    return eve::Result<void>::success();
}

void BattleSystem::registerSkillDamage(const std::string &skillId, const SkillDamageSpec &spec) {
    registerSkillDamageChecked(skillId, spec).ignore("legacy RPG damage registration facade");
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
    bool validateOnly;
    std::optional<eve::Diagnostic> error;

    FormulaParser(const std::string &str, RPGActor *atk, RPGActor *tgt, bool syntaxOnly)
        : s(str), a(atk), b(tgt), validateOnly(syntaxOnly) {}

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
                if (validateOnly) {
                    lhs = 1.0;
                } else if (rhs == 0.0) {
                    if (!error)
                        error = eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                       "damage formula cannot divide by zero",
                                                       "formula[" + std::to_string(pos) + "]");
                    lhs = 0.0;
                } else {
                    lhs /= rhs;
                }
            } else {
                break;
            }
        }
        return lhs;
    }

    double parseFactor() {
        skipSpace();
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
            const bool negate = s[pos++] == '-';
            const double value = parseFactor();
            return negate ? -value : value;
        }
        if (pos < s.size() && s[pos] == '(') {
            ++pos;
            double v = parseExpression();
            skipSpace();
            if (pos >= s.size() || s[pos] != ')') {
                fail("damage formula is missing a closing parenthesis");
                return 0.0;
            }
            ++pos;
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
            if (name.empty()) {
                fail("damage formula attribute name must not be empty");
                return 0.0;
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
            char *end = nullptr;
            const double value = std::strtod(num.c_str(), &end);
            if (end == num.c_str() || *end != '\0' || !std::isfinite(value)) {
                fail("damage formula contains an invalid number");
                return 0.0;
            }
            return value;
        }
        fail("damage formula expected a number, attribute, unary sign, or parenthesized expression");
        return 0.0;
    }

    void fail(std::string message) {
        if (error) return;
        error = eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message),
                                       "formula[" + std::to_string(pos) + "]");
    }
};

eve::Result<double> parseFormula(const std::string &formula, RPGActor *attacker, RPGActor *target,
                                 bool validateOnly) {
    if (formula.empty())
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "damage formula must not be empty", "formula"));
    FormulaParser parser(formula, attacker, target, validateOnly);
    const double value = parser.parseExpression();
    parser.skipSpace();
    if (parser.error) return eve::Result<double>::failure(std::move(*parser.error));
    if (parser.pos != formula.size())
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "damage formula contains trailing input",
            "formula[" + std::to_string(parser.pos) + "]"));
    if (!std::isfinite(value))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "damage formula result must be finite", "formula"));
    return eve::Result<double>::success(value);
}

}  // namespace

double BattleSystem::evaluateFormula(const std::string &formula, RPGActor *attacker,
                                     RPGActor *target) {
    return std::move(evaluateFormulaChecked(formula, attacker, target)).valueOr(0.0);
}

eve::Result<double> BattleSystem::evaluateFormulaChecked(const std::string &formula, RPGActor *attacker,
                                                          RPGActor *target) {
    return parseFormula(formula, attacker, target, false);
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
