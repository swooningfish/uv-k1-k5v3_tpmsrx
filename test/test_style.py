#!/usr/bin/env python3
"""Doc-block and layout standard for the first-party sources.

sdk/ and build/ are third-party or generated, so they are out of scope.

C rules:
- Every file-scope function has a /** */ block immediately above it.
  A GNU __attribute__ may sit between the block and the signature.
- The block opens with /** and closes with */ on their own lines.
  Every other line starts with " *".
- The first sentence ends with a period.
- Parameters have @param lines, in order. A non-void function has one
  @return line. A void function has none.
- A definition's opening brace is alone on the following line.
- dectpms.c, dectpms.h, and the host test use snake_case.
  App sources (*_app.c) use camelCase, except memcpy, memset, app_main,
  and the __aeabi_ division helpers.
- No tabs, no trailing whitespace, and lines of at most 100 characters.
- At most one statement per line. A for header may include its one
  body statement.

Python rules:
- Every function has a docstring whose first line ends with a period.
- Each parameter has an @param line, in order.
- A function annotated as returning None has no @return.
  Any other annotation has one @return line.
"""
from __future__ import annotations

import ast
import os
import re
import sys
from typing import Optional

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAX_LINE = 100
C_FILES = (
    "dectpms.c",
    "dectpms.h",
    "tpmsrx_app.c",
    "tpmsdbg_app.c",
    "test/host_dectpms.c",
)
PY_FILES = (
    "gen_assets.py",
    "test/test_style.py",
    "test/test_variants.py",
    "tools/mock_lcd.py",
    "tools/fetch_sdk.py",
)
MIN_C = {
    "dectpms.c": 28,
    "dectpms.h": 9,
    "tpmsrx_app.c": 33,
    "tpmsdbg_app.c": 24,
    "test/host_dectpms.c": 10,
}
MIN_PY = {
    "gen_assets.py": 0,
    "test/test_style.py": 17,
    "test/test_variants.py": 9,
    "tools/mock_lcd.py": 19,
    "tools/fetch_sdk.py": 2,
}
KEYWORDS = {
    "if", "for", "while", "switch", "do", "defined", "sizeof", "return",
    "__attribute__", "case",
}
TYPE_WORDS = {
    "const", "volatile", "static", "inline", "extern", "unsigned", "signed",
    "struct", "enum", "void", "char", "short", "int", "long", "float",
    "double", "bool", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "size_t", "dectpms_t",
    "tpms_pkt_t", "app_api_t", "row_t",
}
APP_EXCEPTIONS = {"memcpy", "memset", "app_main"}
DOC_MIDDLE = re.compile(r"^ \*( |$)")
PARAM_TAG = re.compile(r"^@param\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S.*)\.$")
RETURN_TAG = re.compile(r"^@return\s+(\S.*)\.$")


def name_ok(relpath: str, name: str) -> bool:
    """Report whether a function name matches that file's rule.

    @param relpath Repository-relative path.
    @param name Function identifier.
    @return True when the name is allowed in this file.
    """
    if relpath.replace("\\", "/").endswith("_app.c"):
        if name in APP_EXCEPTIONS:
            return True
        if re.fullmatch(r"__aeabi_[a-z][a-z0-9_]*", name):
            return True
        return re.fullmatch(r"[a-z][a-z0-9]*([A-Z][a-z0-9]*)*", name) is not None
    return re.fullmatch(r"[a-z][a-z0-9_]*", name) is not None


def void_return(head: str) -> bool:
    """Report whether the text before the name is a void return.

    @param head Signature text before the function name.
    @return True when the return type is void and not a pointer.
    """
    cleaned = re.sub(r"\b(static|inline|extern)\b", "", head)
    cleaned = re.sub(r"\s+", " ", cleaned).strip()
    return cleaned == "void"


def split_args(src: str) -> list[str]:
    """Split a parameter list on commas that are not inside brackets.

    @param src Text between the parentheses.
    @return One string per parameter.
    """
    parts: list[str] = []
    buf: list[str] = []
    depth = 0
    for ch in src:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    if buf:
        parts.append("".join(buf))
    return parts


def param_names(src: str) -> Optional[list[str]]:
    """Collect parameter names from a C parameter list.

    @param src Text between the parentheses.
    @return Names in order, or None when a parameter has no name.
    """
    text = src.strip()
    if text == "" or text == "void":
        return []
    names = []
    for part in split_args(text):
        part = re.sub(r"\[.*", "", part).strip()
        idents = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", part)
        if not idents or all(ident in TYPE_WORDS for ident in idents):
            return None
        names.append(idents[-1])
    return names


def strip_line(line: str, in_block: bool) -> tuple[str, bool]:
    """Remove strings and comments so statement semicolons can be counted.

    @param line One source line.
    @param in_block True when this line continues a block comment.
    @return The code text, and whether a block comment continues.
    """
    out: list[str] = []
    i = 0
    if in_block:
        end = line.find("*/")
        if end < 0:
            return "", True
        i = end + 2
        in_block = False
    in_str = ""
    while i < len(line):
        ch = line[i]
        if in_str:
            if ch == "\\" and i + 1 < len(line):
                i += 2
                continue
            if ch == in_str:
                in_str = ""
            i += 1
            continue
        if ch in "\"'":
            in_str = ch
            i += 1
            continue
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            end = line.find("*/", i + 2)
            if end < 0:
                return "".join(out), True
            i = end + 2
            continue
        out.append(ch)
        i += 1
    return "".join(out), in_block


def statement_errors(relpath: str, lines: list[str]) -> list[str]:
    """Flag lines that hold more than one statement.

    @param relpath Repository-relative path.
    @param lines Source lines, without newlines.
    @return One message per offending line.
    """
    errors = []
    in_block = False
    for number, line in enumerate(lines, 1):
        code, in_block = strip_line(line, in_block)
        semis = code.count(";")
        if re.match(r"\s*for\s*\(", code):
            if semis > 3:
                errors.append(f"{relpath}:{number}: more than one statement")
        elif semis > 1:
            errors.append(f"{relpath}:{number}: more than one statement")
    return errors


def scan_signature(lines: list[str], start: int, paren_col: int):
    """Parse a signature from the opening parenthesis.

    @param lines Source lines, without newlines.
    @param start Line index of the signature.
    @param paren_col Column of the opening parenthesis.
    @return Kind, parameter text, end line, and whether the brace is legal.
    """
    depth = 0
    in_str = ""
    params: list[str] = []
    phase = "params"
    close_line = start
    for li in range(start, len(lines)):
        src = lines[li]
        col = paren_col if li == start else 0
        while col < len(src):
            ch = src[col]
            if in_str:
                if ch == "\\" and col + 1 < len(src):
                    col += 2
                    continue
                if ch == in_str:
                    in_str = ""
                col += 1
                continue
            if ch in "\"'":
                in_str = ch
                col += 1
                continue
            if phase == "params":
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        phase = "after"
                        close_line = li
                    else:
                        params.append(ch)
                elif depth >= 1:
                    params.append(ch)
            else:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                elif ch == "{" and depth == 0:
                    brace_ok = src.strip() == "{" and li == close_line + 1
                    return ("def", "".join(params), li, brace_ok)
                elif ch == ";" and depth == 0:
                    return ("proto", "".join(params), li, True)
            col += 1
        if phase == "params":
            params.append("\n")
    return "unterminated signature"


def payload_lines(block: list[str]) -> list[str]:
    """Return the text lines of a doc block, without the star margin.

    @param block Raw doc-block lines, including the opener and closer.
    @return Text lines. A blank star line becomes an empty string.
    """
    if len(block) == 1:
        text = block[0].strip()[3:-2].strip()
        return [text]
    out = []
    for line in block[1:-1]:
        text = line.strip()
        if text == "*":
            out.append("")
        else:
            out.append(text[1:].strip())
    return out


def doc_errors(lines: list[str], sig_i: int, params, is_void: bool, prefix: str) -> list[str]:
    """Check the doc block attached to the signature at sig_i.

    @param lines Source lines, without newlines.
    @param sig_i Zero-based line of the signature.
    @param params Parameter names in order, or None when one is unnamed.
    @param is_void True when the function returns void.
    @param prefix Path and line used on every message.
    @return Messages for a missing, malformed, or incomplete block.
    """
    j = sig_i - 1
    while j >= 0 and re.match(r"\s*__attribute__\s*\(", lines[j]):
        j -= 1
    if j < 0:
        return [f"{prefix}: missing doc block"]
    stripped = lines[j].strip()
    single = stripped.startswith("/**") and stripped.endswith("*/")
    if single and stripped != "/**/":
        block = [lines[j]]
    elif stripped == "*/":
        k = j
        while k >= 0 and not lines[k].strip().startswith("/**"):
            if j - k > 30:
                return [f"{prefix}: doc block is too long"]
            k -= 1
        if k < 0 or lines[k].strip() != "/**":
            return [f"{prefix}: missing doc block"]
        block = lines[k:j + 1]
        for line in block[1:-1]:
            if not DOC_MIDDLE.match(line):
                return [f'{prefix}: doc line must start with " *"']
    else:
        return [f"{prefix}: missing doc block"]

    body = payload_lines(block)
    nonempty = [line for line in body if line]
    if not nonempty or nonempty[0].startswith("@") or not nonempty[0].endswith("."):
        return [f"{prefix}: summary must end with a period"]
    seen_params = []
    returns = 0
    seen_tag = False
    for line in nonempty[1:]:
        if line.startswith("@"):
            seen_tag = True
            param = PARAM_TAG.match(line)
            ret = RETURN_TAG.match(line)
            if param:
                seen_params.append(param.group(1))
            elif ret:
                returns += 1
            else:
                return [f"{prefix}: bad doc tag"]
        elif seen_tag:
            return [f"{prefix}: doc text after a tag"]
    if params is None:
        return [f"{prefix}: unnamed parameter"]
    errors = []
    if seen_params != list(params):
        errors.append(f"{prefix}: @param names {seen_params} != {list(params)}")
    if is_void and returns:
        errors.append(f"{prefix}: void function has @return")
    if not is_void and returns != 1:
        errors.append(f"{prefix}: missing @return")
    return errors


def function_at(relpath: str, lines: list[str], index: int):
    """Lint one file-scope function when this line starts one.

    @param relpath Repository-relative path.
    @param lines Source lines, without newlines.
    @param index Zero-based line of a possible signature.
    @return Errors and the last line consumed, or None when it is not a function.
    """
    line = lines[index]
    if not line or line[0].isspace() or line.startswith("#") or line.startswith("/*"):
        return None
    if line.startswith("*") or line.startswith(" *"):
        return None
    match = re.match(r"^(?P<head>.*?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(", line)
    if not match or match.group("name") in KEYWORDS:
        return None
    name = match.group("name")
    scanned = scan_signature(lines, index, match.end() - 1)
    prefix = f"{relpath}:{index + 1}"
    if isinstance(scanned, str):
        return [f"{prefix}: {scanned}"], index
    kind, param_src, end, brace_ok = scanned
    errors = []
    if not name_ok(relpath, name):
        kind_name = "camelCase" if relpath.endswith("_app.c") else "snake_case"
        errors.append(f"{prefix}: {name} is not {kind_name}")
    if kind == "def" and not brace_ok:
        errors.append(f"{prefix}: opening brace must be alone on the next line")
    names = param_names(param_src)
    is_void = void_return(match.group("head"))
    errors.extend(doc_errors(lines, index, names, is_void, prefix))
    return errors, end


def lint_c(relpath: str, text: str) -> tuple[list[str], int]:
    """Apply the C standard to one source file.

    @param relpath Repository-relative path.
    @param text Full file text.
    @return Messages and the number of file-scope functions found.
    """
    errors = []
    if "\t" in text:
        errors.append(f"{relpath}: tab character")
    if "\r" in text:
        errors.append(f"{relpath}: carriage return")
    if not text.endswith("\n"):
        errors.append(f"{relpath}: missing final newline")
    lines = text.splitlines()
    for number, line in enumerate(lines, 1):
        if line != line.rstrip():
            errors.append(f"{relpath}:{number}: trailing whitespace")
        if len(line) > MAX_LINE:
            errors.append(f"{relpath}:{number}: line is {len(line)} characters (max {MAX_LINE})")
    errors.extend(statement_errors(relpath, lines))
    count = 0
    index = 0
    while index < len(lines):
        found = function_at(relpath, lines, index)
        if found is None:
            index += 1
            continue
        msgs, end = found
        errors.extend(msgs)
        count += 1
        index = end + 1
    return errors, count


def _none_annotation(node: Optional[ast.AST]) -> bool:
    """Report whether a return annotation is None.

    @param node Annotation node, or None when the function is unannotated.
    @return True only for an explicit None annotation.
    """
    if isinstance(node, ast.Constant):
        return node.value is None
    if isinstance(node, ast.Name):
        return node.id == "None"
    return False


def lint_py(relpath: str, text: str) -> tuple[list[str], int]:
    """Apply the Python standard to one source file.

    @param relpath Repository-relative path.
    @param text Full file text.
    @return Messages and the number of functions found.
    """
    errors = []
    if "\t" in text:
        errors.append(f"{relpath}: tab character")
    if "\r" in text:
        errors.append(f"{relpath}: carriage return")
    if text and not text.endswith("\n"):
        errors.append(f"{relpath}: missing final newline")
    lines = text.splitlines()
    for number, line in enumerate(lines, 1):
        if line != line.rstrip():
            errors.append(f"{relpath}:{number}: trailing whitespace")
        if len(line) > MAX_LINE:
            errors.append(f"{relpath}:{number}: line is {len(line)} characters (max {MAX_LINE})")
    try:
        tree = ast.parse(text)
    except SyntaxError as exc:
        errors.append(f"{relpath}:{exc.lineno}: {exc.msg}")
        return errors, 0
    count = 0
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        count += 1
        prefix = f"{relpath}:{node.lineno}"
        doc = ast.get_docstring(node, clean=True)
        if not doc:
            errors.append(f"{prefix}: missing doc block")
            continue
        summary = doc.splitlines()[0].strip()
        if not summary.endswith("."):
            errors.append(f"{prefix}: summary must end with a period")
        params = [arg.arg for arg in node.args.posonlyargs]
        params += [arg.arg for arg in node.args.args]
        if node.args.vararg:
            params.append(node.args.vararg.arg)
        params += [arg.arg for arg in node.args.kwonlyargs]
        if node.args.kwarg:
            params.append(node.args.kwarg.arg)
        params = [name for name in params if name != "self"]
        seen = []
        returns = 0
        for line in doc.splitlines():
            text_line = line.strip()
            if not text_line.startswith("@"):
                continue
            param = PARAM_TAG.match(text_line)
            ret = RETURN_TAG.match(text_line)
            if param:
                seen.append(param.group(1))
            elif ret:
                returns += 1
            else:
                errors.append(f"{prefix}: bad doc tag")
        if seen != params:
            errors.append(f"{prefix}: @param names {seen} != {params}")
        if _none_annotation(node.returns):
            if returns:
                errors.append(f"{prefix}: void function has @return")
        elif node.returns is not None and returns != 1:
            errors.append(f"{prefix}: missing @return")
    return errors, count


def expect_has(errors: list[str], needle: str, label: str) -> list[str]:
    """Fail when a fixture did not report the expected problem.

    @param errors Messages from a linter.
    @param needle Text that must appear in one message.
    @param label Fixture name.
    @return A single message when the needle is absent.
    """
    if any(needle in msg for msg in errors):
        return []
    return [f"{label}: expected {needle!r} in {errors}"]


def expect_clean(errors: list[str], label: str) -> list[str]:
    """Fail when a good fixture produced messages.

    @param errors Messages from a linter.
    @param label Fixture name.
    @return The messages, each marked with the fixture name.
    """
    return [f"{label}: {msg}" for msg in errors]


def run_meta() -> list[str]:
    """Run the standard against small fixtures, good and bad.

    @return Messages for fixtures that were accepted or rejected wrongly.
    """
    good = """\
/**
 * Add one.
 *
 * @param x Value to increment.
 * @return x plus one.
 */
static int add_one(int x)
{
    return x + 1;
}

/**
 * Count with a for header and its body on one line.
 *
 * @param n Limit, overwritten by the loop.
 */
static void count_up(int n)
{
    for (int i = 0; i < n; i++) n = i;
}

/**
 * Copy a span of bits.
 *
 * @param raw Packed source.
 * @param start First bit.
 * @param nraw Number of source bits.
 * @param out Packed destination.
 * @param max_bits Capacity in bits.
 * @return Bits written.
 */
static unsigned copy_bits(const uint8_t *raw, unsigned start, unsigned nraw,
                          uint8_t *out, unsigned max_bits)
{
    return 0;
}
"""
    missing = """\
static int add_one(int x)
{
    return x + 1;
}
"""
    plain = """\
/*
 * Stop.
 */
static void stop(void)
{
}
"""
    braced = """\
/**
 * Add one.
 *
 * @param x Value to increment.
 * @return x plus one.
 */
static int add_one(int x) {
    return x + 1;
}
"""
    two = """\
/**
 * Set two locals.
 */
static void set_two(void)
{
    int a = 1; int b = 2;
}
"""
    snake = """\
/**
 * Draw it.
 */
static void drawIt(void)
{
}
"""
    camel = """\
/**
 * Draw it.
 */
static void draw_it(void)
{
}
"""
    attributed = """\
/**
 * Divide.
 *
 * @param n Dividend.
 * @param d Divisor.
 * @return Quotient and remainder.
 */
__attribute__((noinline))
static uint64_t divmodBoth(uint32_t n, uint32_t d)
{
    return 0;
}
"""
    proto = """\
/**
 * Add one.
 *
 * @param x Value to increment.
 * @return x plus one.
 */
int add_one(int x);
"""
    alias = """\
/**
 * Divide.
 *
 * @param n Dividend.
 * @param d Divisor.
 * @return Quotient and remainder.
 */
uint64_t __aeabi_uidiv(uint32_t n, uint32_t d) __attribute__((alias("idiv")));
"""
    long_line = "/* " + ("x" * 98) + " */\n"
    out = []
    out += expect_clean(lint_c("dectpms.c", good)[0], "good C")
    out += expect_has(lint_c("dectpms.c", missing)[0], "missing doc block", "missing doc")
    out += expect_has(lint_c("dectpms.c", plain)[0], "missing doc block", "plain comment")
    out += expect_has(lint_c("dectpms.c", braced)[0], "opening brace", "same-line brace")
    out += expect_has(lint_c("dectpms.c", two)[0], "more than one statement", "two statements")
    out += expect_has(lint_c("dectpms.c", snake)[0], "snake_case", "camel in decoder")
    out += expect_has(lint_c("tpmsrx_app.c", camel)[0], "camelCase", "snake in app")
    out += expect_clean(lint_c("tpmsrx_app.c", attributed)[0], "attribute")
    out += expect_clean(lint_c("dectpms.h", proto)[0], "prototype")
    out += expect_clean(lint_c("tpmsrx_app.c", alias)[0], "alias")
    out += expect_has(lint_c("dectpms.c", long_line)[0], "max 100", "long line")
    good_py = '''\
def add(x: int, y: int) -> int:
    """Add two numbers.

    @param x Left operand.
    @param y Right operand.
    @return The sum.
    """
    return x + y
'''
    no_doc = "def add(x, y):\n    return x + y\n"
    no_param = '''\
def add(x: int, y: int) -> int:
    """Add two numbers.

    @param x Left operand.
    @return The sum.
    """
    return x + y
'''
    void_ret = '''\
def noop() -> None:
    """Do nothing.

    @return Nothing.
    """
    return None
'''
    out += expect_clean(lint_py("sample.py", good_py)[0], "good Python")
    out += expect_has(lint_py("sample.py", no_doc)[0], "missing doc block", "py missing")
    out += expect_has(lint_py("sample.py", no_param)[0], "@param", "py param")
    out += expect_has(lint_py("sample.py", void_ret)[0], "void function has @return", "py none")
    return out


def main() -> int:
    """Lint the fixtures and the first-party sources.

    @return Zero when the standard holds.
    """
    meta = run_meta()
    if meta:
        for msg in meta:
            print(f"FAIL  {msg}")
        print(f"{len(meta)} style fixture(s) failed")
        return 1
    print("  ok  style fixtures")
    failed = 0
    for rel in C_FILES:
        with open(os.path.join(ROOT, rel), encoding="utf-8") as handle:
            errors, count = lint_c(rel, handle.read())
        print(f"  {rel}: {count} functions")
        if count < MIN_C[rel]:
            errors.append(f"{rel}: found {count} functions, want at least {MIN_C[rel]}")
        for msg in errors:
            print(f"FAIL  {msg}")
            failed += 1
    for rel in PY_FILES:
        with open(os.path.join(ROOT, rel), encoding="utf-8") as handle:
            errors, count = lint_py(rel, handle.read())
        print(f"  {rel}: {count} functions")
        if count < MIN_PY[rel]:
            errors.append(f"{rel}: found {count} functions, want at least {MIN_PY[rel]}")
        for msg in errors:
            print(f"FAIL  {msg}")
            failed += 1
    if failed:
        print(f"{failed} style check(s) failed")
        return 1
    print("style checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
