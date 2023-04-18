def invalidate_caches():
    pass


def import_module(modname):
    mod = __import__(modname)
    for x in modname.split(".")[1:]:
        mod = getattr(mod, x)
    return mod
