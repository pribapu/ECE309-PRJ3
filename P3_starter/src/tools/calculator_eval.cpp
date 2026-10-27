// src/tools/calculator_eval.cpp — calc_evaluate: the two-stack algorithm
// (spec §4.6, worked example in Appendix C).
//
// tokens is guaranteed well-formed by calc_tokenize (numbers and operators
// alternate, parentheses balanced, not empty, minus-before-number already
// folded into the number). calc_evaluate still never crashes on malformed
// input: every stack access is guarded.
//
// Iterative by construction (no recursion), so a 20,000-term sum or 5,000
// levels of nesting only grows std::vector's heap storage, never the call
// stack.

#include "tools/calculator.h"

#include <vector>

namespace {

int precedence(CalcToken::Kind op) {
    return (op == CalcToken::Kind::Times || op == CalcToken::Kind::Divide) ? 2 : 1;
}

// Pops one operator and its two operands (right first, then left — getting
// this order backwards is the classic bug: spec §5 pitfall 5), computes the
// result, and pushes it back onto `values`.
bool apply_top(std::vector<double>* values, std::vector<CalcToken::Kind>* ops,
               std::string* error) {
    if (ops->empty() || values->size() < 2) {
        *error = "malformed expression";
        return false;
    }
    const CalcToken::Kind op = ops->back();
    ops->pop_back();
    const double right = values->back();
    values->pop_back();
    const double left = values->back();
    values->pop_back();

    double result = 0.0;
    switch (op) {
        case CalcToken::Kind::Plus:  result = left + right; break;
        case CalcToken::Kind::Minus: result = left - right; break;
        case CalcToken::Kind::Times: result = left * right; break;
        case CalcToken::Kind::Divide:
            if (right == 0.0) {
                *error = "division by zero";
                return false;
            }
            result = left / right;
            break;
        default:
            *error = "malformed expression";
            return false;
    }
    values->push_back(result);
    return true;
}

}  // namespace

bool calc_evaluate(const std::vector<CalcToken>& tokens, double* out, std::string* error) {
    std::vector<double>          values;
    std::vector<CalcToken::Kind> ops;

    for (const CalcToken& tok : tokens) {
        if (tok.kind == CalcToken::Kind::Number) {
            values.push_back(tok.value);
        } else if (tok.kind == CalcToken::Kind::LParen) {
            ops.push_back(CalcToken::Kind::LParen);
        } else if (tok.kind == CalcToken::Kind::RParen) {
            while (!ops.empty() && ops.back() != CalcToken::Kind::LParen) {
                if (!apply_top(&values, &ops, error)) return false;
            }
            if (ops.empty()) {
                *error = "unbalanced parentheses";
                return false;
            }
            ops.pop_back();   // discard the matching '('
        } else {
            // One of the four binary operators: apply anything already
            // waiting whose precedence is >= this one's (the ">=", not ">",
            // is what makes "10-4-3" evaluate left to right).
            while (!ops.empty() && ops.back() != CalcToken::Kind::LParen &&
                   precedence(ops.back()) >= precedence(tok.kind)) {
                if (!apply_top(&values, &ops, error)) return false;
            }
            ops.push_back(tok.kind);
        }
    }

    while (!ops.empty()) {
        if (!apply_top(&values, &ops, error)) return false;
    }

    if (values.size() != 1) {
        *error = "malformed expression";
        return false;
    }
    *out = values.back();
    return true;
}
