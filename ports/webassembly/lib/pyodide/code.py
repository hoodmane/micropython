def eval_code(code, globals=None, locals=None):
    exec(code, globals, locals)
