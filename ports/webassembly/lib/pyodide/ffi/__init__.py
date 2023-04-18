from _pyodide_core import create_proxy


def create_once_callable(x):
    return create_proxy(x)
