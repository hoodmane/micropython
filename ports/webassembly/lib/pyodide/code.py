from textwrap import dedent


def eval_code(code, globals=None, locals=None):
    code = dedent(code)
    print("exec\n", code, "\n\n")
    exec(code, globals, locals)
