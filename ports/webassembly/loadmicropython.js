/**
 * A proxy around globals that falls back to checking for a builtin if has or
 * get fails to find a global with the given key. Note that this proxy is
 * transparent to js2python: it won't notice that this wrapper exists at all and
 * will translate this proxy to the globals dictionary.
 * @private
 */
function wrapPythonGlobals(globals_dict, builtins_dict) {
  return new Proxy(globals_dict, {
    get(target, symbol) {
      if (symbol === "get") {
        return (key) => {
          let result = target.get(key);
          if (result === undefined) {
            result = builtins_dict.get(key);
          }
          return result;
        };
      }
      if (symbol === "has") {
        return (key) => target.has(key) || builtins_dict.has(key);
      }
      return Reflect.get(target, symbol);
    },
  });
}

async function loadMicroPython(options) {
  const { heapsize } = Object.assign({ heapsize: 64 * 1024 }, options);
  const Module = {};
  const moduleLoaded = new Promise((r) => (Module.postRun = r));
  _createMicropythonModule(Module);
  await moduleLoaded;
  Module._mp_js_init(heapsize);
  function pyimport(name) {
    const nameid = Module.hiwire.new_value(name);
    const proxy_id = Module._pyimport(nameid);
    Module.hiwire.decref(nameid);
    Module.api.throw_if_error();
    return Module.hiwire.pop_value(proxy_id);
  }
  const main = pyimport("__main__");
  const main_dict = main.__dict__;
  const builtins = pyimport("builtins");
  const builtins_dict = builtins.__dict__;
  const globals = wrapPythonGlobals(main_dict, builtins_dict);
  builtins.destroy();
  main.destroy();
  const exec = globals.get("exec");

  function runPython(code, { globals, locals } = {}) {
    exec(code, globals, locals);
  }

  const dict = globals.get("dict");
  const tmp_dict = dict();
  dict.destroy();

  runPython("import sys", { globals: tmp_dict });
  runPython(
    "def register_js_module(name, mod): sys.modules.update({name: mod})",
    { globals: tmp_dict }
  );
  const register_js_module = tmp_dict.get("register_js_module");
  tmp_dict.destroy();

  function registerJsModule(name, mod) {
    register_js_module(name, mod);
  }

  return {
    _module: Module,
    FS: Module.FS,
    runPython,
    pyimport,
    globals,
    registerJsModule,
  };
}
