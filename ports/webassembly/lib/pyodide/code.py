from textwrap import dedent


def eval_code(code, globals=None, locals=None):
    code = dedent(code)
    exec(code, globals, locals)
