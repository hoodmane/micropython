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
  const { heapsize } = Object.assign({ heapsize: 1024 * 1024 }, options);
  const Module = {};
  const moduleLoaded = new Promise((r) => (Module.postRun = r));
  _createMicropythonModule(Module);
  await moduleLoaded;
  Module._mp_js_init(heapsize);
  function bootstrap_pyimport(name) {
    const nameid = Module.hiwire.new_value(name);
    const proxy_id = Module._pyimport(nameid);
    Module.hiwire.decref(nameid);
    Module.api.throw_if_error();
    return Module.hiwire.pop_value(proxy_id);
  }

  
  const main = bootstrap_pyimport("__main__");
  const main_dict = main.__dict__;
  const builtins = bootstrap_pyimport("builtins");
  const builtins_dict = builtins.__dict__;
  const globals = wrapPythonGlobals(main_dict, builtins_dict);
  builtins.destroy();
  main.destroy();


  const dict = globals.get("dict");
  const tmp_dict = dict();
  dict.destroy();

  const exec = globals.get("exec");

  function runPython(code, { globals, locals } = {}) {
    exec(code, globals, locals);
  }

  runPython("import sys", { globals: tmp_dict });
  runPython("sys.path.append('/lib')", { globals: tmp_dict });

  const importlib = bootstrap_pyimport("importlib");
  function pyimport(mod) {
    return importlib.import_module(mod);
  }

  runPython("def register_js_module(name, mod): sys.modules[name] = mod", {
    globals: tmp_dict,
  });
  const register_js_module = tmp_dict.get("register_js_module");
  tmp_dict.destroy();

  function registerJsModule(name, mod) {
    register_js_module(name, mod);
  }
  const public_api = {
    _module: Module,
    FS: Module.FS,
    runPython,
    pyimport,
    globals,
    registerJsModule,
  };
  registerJsModule("js", globalThis);
  registerJsModule("pyodide_js", public_api);

  return public_api;
}
globalThis.loadMicroPython = loadMicroPython;
