#!/usr/bin/env python3
"""A generated header's function-like macro, evaluated (the converter's chains of choices).
    cmacro.py HEADER NAME ARG...      e.g. cmacro.py build/gen/game_data.h JOB_MASK 1 0
"""
import re
import sys


def value(header, name, *args):
    text = open(header, encoding="utf-8").read()
    m = re.search(r"#define " + name + r"\(([^)]*)\)\t(.*)", text)
    if not m:
        raise SystemExit(f"cmacro.py: {name}(...) is not in {header}")
    params = [p.strip() for p in m.group(1).split(",")]
    body = m.group(2)
    for p, a in zip(params, args):
        body = re.sub(r"\b" + p + r"\b", str(int(a)), body)
    tokens = re.findall(r"-?\d+|==|[()?:]", body)
    pos = 0

    def peek():
        return tokens[pos] if pos < len(tokens) else None

    def take():
        nonlocal pos
        pos += 1
        return tokens[pos - 1]

    def atom():
        if peek() == "(":
            take()
            v = expr()
            take()   # ")"
            return v
        return int(take())

    def compare():
        v = atom()
        if peek() == "==":
            take()
            v = int(v == atom())
        return v

    def expr():
        v = compare()
        if peek() == "?":
            take()
            a = expr()
            take()   # ":"
            b = expr()
            return a if v else b
        return v

    return expr()


if __name__ == "__main__":
    print(value(sys.argv[1], sys.argv[2], *sys.argv[3:]))
