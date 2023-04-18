async function loadMicroPython(options) {
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

  /**
   *  If indexURL isn't provided, throw an error and catch it and then parse our
   *  file name out from the stack trace.
   *
   *  Question: But getting the URL from error stack trace is well... really
   *  hacky. Can't we use
   *  [`document.currentScript`](https://developer.mozilla.org/en-US/docs/Web/API/Document/currentScript)
   *  or
   *  [`import.meta.url`](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/import.meta)
   *  instead?
   *
   *  Answer: `document.currentScript` works for the browser main thread.
   *  `import.meta` works for es6 modules. In a classic webworker, I think there
   *  is no approach that works. Also we would need some third approach for node
   *  when loading a commonjs module using `require`. On the other hand, this
   *  stack trace approach works for every case without any feature detection
   *  code.
   */
  function calculateIndexURL() {
    if (typeof __dirname === "string") {
      return __dirname;
    }
    let err;
    try {
      throw new Error();
    } catch (e) {
      err = e;
    }
    let fileName = ErrorStackParser.parse(err)[0].fileName;
    const indexOfLastSlash = fileName.lastIndexOf("/");
    if (indexOfLastSlash === -1) {
      throw new Error("Could not extract indexURL path from module location");
    }
    return fileName.slice(0, indexOfLastSlash + 1);
  }

  let { heapsize, indexURL } = Object.assign(
    { heapsize: 1024 * 1024 },
    options
  );
  if (indexURL === undefined) {
    indexURL = calculateIndexURL();
  }
  const Module = {};
  const moduleLoaded = new Promise((r) => (Module.postRun = r));

  Module.locateFile = (path) => indexURL + path;
  _createMicropythonModule(Module);
  await moduleLoaded;
  Module.locateFile = (path) => {
    throw new Error("Didn't expect to load any more file_packager files!");
  };
  Module._mp_js_init(heapsize);
  function bootstrap_pyimport(name) {
    const nameid = Module.hiwire.new_value(name);
    const proxy_id = Module._pyimport(nameid);
    Module.hiwire.decref(nameid);
    Module.API.throw_if_error();
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

  const pyexec = globals.get("exec");
  function bootstrap_runPython(code) {
    pyexec(code, tmp_dict);
  }

  bootstrap_runPython("import sys");
  bootstrap_runPython("sys.path.append('/lib')");

  const importlib = bootstrap_pyimport("importlib");
  function pyimport(mod) {
    return importlib.import_module(mod);
  }
  bootstrap_runPython(
    "def register_js_module(name, mod): sys.modules[name] = mod"
  );
  const register_js_module = tmp_dict.get("register_js_module");
  tmp_dict.destroy();

  let eval_code;

  function runPython(code, { globals, locals } = {}) {
    eval_code(code, globals, locals);
  }

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
  eval_code = pyimport("pyodide.code").eval_code;

  return public_api;
}
globalThis.loadMicroPython = loadMicroPython;
