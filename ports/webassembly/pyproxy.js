JS_FILE(pyproxy_init_js, () => {
  0, 0; /* Magic, see include_js_file.h */

  function isPyProxy(jsobj) {
    return jsobj instanceof PyProxy;
  }
  API.isPyProxy = isPyProxy;

  Module.callPyObjectKwargs = function (ptrobj, jsargs, kwargs) {
    // We don't do any checking for kwargs, checks are in PyProxy.callKwargs
    // which only is used when the keyword arguments come from the user.
    let num_pos_args = jsargs.length;
    let kwargs_names = Object.entries(kwargs);
    let num_kwargs = kwargs_names.length;
    jsargs.push(...kwargs_names.flat());
    let idargs = Hiwire.new_value(jsargs);
    console.warn("XX", { jsargs, idargs });
    let idresult;
    try {
      Py_ENTER();
      idresult = Module.__pyproxy_apply(
        ptrobj,
        num_pos_args,
        num_kwargs,
        idargs
      );
      Py_EXIT();
    } catch (e) {
      if (API._skip_unwind_fatal_error) {
        API.maybe_fatal_error(e);
      } else {
        API.fatal_error(e);
      }
      return;
    } finally {
      Hiwire.decref(idargs);
    }
    if (idresult === 0) {
      Module._pythonexc2js();
    }
    let result = Hiwire.pop_value(idresult);
    // Automatically schedule coroutines
    if (result && result.type === "coroutine" && result._ensure_future) {
      result._ensure_future();
    }
    return result;
  };

  Module.callPyObject = function (ptrobj, jsargs) {
    return Module.callPyObjectKwargs(ptrobj, jsargs, {});
  };

  class PyProxy {
    /**
     * @private
     * @hideconstructor
     */
    constructor() {
      throw new TypeError("PyProxy is not a constructor");
    }

    /** @private */
    get [Symbol.toStringTag]() {
      return "PyProxy";
    }
    /**
     * The name of the type of the object.
     *
     * Usually the value is ``"module.name"`` but for builtins or
     * interpreter-defined types it is just ``"name"``. As pseudocode this is:
     *
     * .. code-block:: python
     *
     *    ty = type(x)
     *    if ty.__module__ == 'builtins' or ty.__module__ == "__main__":
     *        return ty.__name__
     *    else:
     *        ty.__module__ + "." + ty.__name__
     *
     */
    get type() {
      let ptrobj = _getPtr(this);
      return Hiwire.pop_value(Module.__pyproxy_type(ptrobj));
    }

    toString() {
      let ptrobj = _getPtr(this);
      let jsref_repr;
      try {
        Py_ENTER();
        jsref_repr = Module.__pyproxy_repr(ptrobj);
        Py_EXIT();
      } catch (e) {
        API.fatal_error(e);
      }
      if (jsref_repr === 0) {
        Module._pythonexc2js();
      }
      return Hiwire.pop_value(jsref_repr);
    }
    /**
     * Destroy the :js:class:`~pyodide.ffi.PyProxy`. This will release the memory. Any further attempt
     * to use the object will raise an error.
     *
     * In a browser supporting :js:data:`FinalizationRegistry`, Pyodide will
     * automatically destroy the :js:class:`~pyodide.ffi.PyProxy` when it is garbage collected, however
     * there is no guarantee that the finalizer will be run in a timely manner so
     * it is better to destroy the proxy explicitly.
     *
     * @param options
     * @param options.message The error message to print if use is attempted after
     *        destroying. Defaults to "Object has already been destroyed".
     *
     */
    destroy(options) {
      options = Object.assign({ message: "", destroyRoundtrip: true }, options);
      const { message: m, destroyRoundtrip: d } = options;
      Module.pyproxy_destroy(this, m, d);
    }
    /**
     * Make a new :js:class:`~pyodide.ffi.PyProxy` pointing to the same Python object.
     * Useful if the :js:class:`~pyodide.ffi.PyProxy` is destroyed somewhere else.
     */
    copy() {
      let ptrobj = _getPtr(this);
      return pyproxy_new(ptrobj, {
        flags: this.$$flags,
        cache: this.$$.cache,
        props: this.$$props,
      });
    }

    /**
     * The ``apply()`` method calls the specified function with a given this
     * value, and arguments provided as an array (or an array-like object). Like
     * :js:meth:`Function.apply`.
     *
     * @param thisArg The ``this`` argument. Has no effect unless the
     * :js:class:`~pyodide.ffi.PyCallable` has :js:meth:`captureThis` set. If
     * :js:meth:`captureThis` is set, it will be passed as the first argument to
     * the Python function.
     * @param jsargs The array of arguments
     * @returns The result from the function call.
     */
    apply(thisArg, jsargs) {
      // Convert jsargs to an array using ordinary .apply in order to match the
      // behavior of .apply very accurately.
      jsargs = function (...args) {
        return args;
      }.apply(undefined, jsargs);
      return Module.callPyObject(_getPtr(this), jsargs);
    }
  }

  /**
   * Create a new PyProxy wrapping ptrobj which is a PyObject*.
   *
   * The argument cache is only needed to implement the PyProxy.copy API, it
   * allows the copy of the PyProxy to share its attribute cache with the original
   * version. In all other cases, pyproxy_new should be called with one argument.
   *
   * In the case that the Python object is callable, PyProxy inherits from
   * Function so that PyProxy objects can be callable. In that case we MUST expose
   * certain properties inherited from Function, but we do our best to remove as
   * many as possible.
   * @private
   */
  function pyproxy_new(ptrobj, { flags: flags_arg, cache, props, $$ } = {}) {
    // const flags =
    //   flags_arg !== undefined ? flags_arg : Module._pyproxy_getflags(ptrobj);
    // if (flags === -1) {
    //   Module._pythonexc2js();
    // }
    // const cls = Module.getPyProxyClass(flags);
    const cls = PyProxy;
    const flags = IS_CALLABLE;
    let target;
    if (flags & IS_CALLABLE) {
      // In this case we are effectively subclassing Function in order to ensure
      // that the proxy is callable. With a Content Security Protocol that doesn't
      // allow unsafe-eval, we can't invoke the Function constructor directly. So
      // instead we create a function in the universally allowed way and then use
      // `setPrototypeOf`. The documentation for `setPrototypeOf` says to use
      // `Object.create` or `Reflect.construct` instead for performance reasons
      // but neither of those work here.
      target = function () {};
      Object.setPrototypeOf(target, cls.prototype);
      // Remove undesirable properties added by Function constructor. Note: we
      // can't remove "arguments" or "caller" because they are not configurable
      // and not writable
      // @ts-ignore
      delete target.length;
      // @ts-ignore
      delete target.name;
      // prototype isn't configurable so we can't delete it but it's writable.
      target.prototype = undefined;
    } else {
      target = Object.create(cls.prototype);
    }

    const isAlias = !!$$;

    if (!isAlias) {
      if (!cache) {
        // The cache needs to be accessed primarily from the C function
        // _pyproxy_getattr so we make a hiwire id.
        let cacheId = Hiwire.new_value(new Map());
        cache = { cacheId, refcnt: 0 };
      }
      cache.refcnt++;
      $$ = { ptr: ptrobj, type: "PyProxy", cache, flags };
      // Module.finalizationRegistry.register($$, [ptrobj, cache], $$);
      //   Module._Py_IncRef(ptrobj);
    }

    Object.defineProperty(target, "$$", { value: $$ });
    if (!props) {
      props = {};
    }
    props = Object.assign(
      { isBound: false, captureThis: false, boundArgs: [], roundtrip: false },
      props
    );
    Object.defineProperty(target, "$$props", { value: props });

    let proxy = new Proxy(target, PyProxyHandlers);
    // if (!isAlias) {
    //   trace_pyproxy_alloc(proxy);
    // }
    return proxy;
  }
  Module.pyproxy_new = pyproxy_new;

  function _getPtr(jsobj) {
    let ptr = jsobj.$$.ptr;
    if (ptr === 0) {
      throw new Error(jsobj.$$.destroyed_msg);
    }
    return ptr;
  }

  // See explanation of which methods should be defined here and what they do
  // here:
  // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Proxy
  let PyProxyHandlers = {
    isExtensible() {
      return true;
    },
    has(jsobj, jskey) {
      // Note: must report "prototype" in proxy when we are callable.
      // (We can return the wrong value from "get" handler though.)
      let objHasKey = Reflect.has(jsobj, jskey);
      if (objHasKey) {
        return true;
      }
      // python_hasattr will crash if given a Symbol.
      if (typeof jskey === "symbol") {
        return false;
      }
      if (jskey.startsWith("$")) {
        jskey = jskey.slice(1);
      }
      return python_hasattr(jsobj, jskey);
    },
    get(jsobj, jskey) {
      // Preference order:
      // 1. stuff from JavaScript
      // 2. the result of Python getattr

      // python_getattr will crash if given a Symbol.
      if (jskey in jsobj || typeof jskey === "symbol") {
        return Reflect.get(jsobj, jskey);
      }
      // If keys start with $ remove the $. User can use initial $ to
      // unambiguously ask for a key on the Python object.
      if (jskey.startsWith("$")) {
        jskey = jskey.slice(1);
      }
      // 2. The result of getattr
      let idresult = python_getattr(jsobj, jskey);
      if (idresult !== 0) {
        return Hiwire.pop_value(idresult);
      }
    },
    set(jsobj, jskey, jsval) {
      let descr = Object.getOwnPropertyDescriptor(jsobj, jskey);
      if (descr && !descr.writable) {
        throw new TypeError(`Cannot set read only field '${jskey}'`);
      }
      // python_setattr will crash if given a Symbol.
      if (typeof jskey === "symbol") {
        return Reflect.set(jsobj, jskey, jsval);
      }
      if (jskey.startsWith("$")) {
        jskey = jskey.slice(1);
      }
      python_setattr(jsobj, jskey, jsval);
      return true;
    },
    deleteProperty(jsobj, jskey) {
      let descr = Object.getOwnPropertyDescriptor(jsobj, jskey);
      if (descr && !descr.writable) {
        throw new TypeError(`Cannot delete read only field '${jskey}'`);
      }
      if (typeof jskey === "symbol") {
        return Reflect.deleteProperty(jsobj, jskey);
      }
      if (jskey.startsWith("$")) {
        jskey = jskey.slice(1);
      }
      python_delattr(jsobj, jskey);
      // Must return "false" if "jskey" is a nonconfigurable own property.
      // Otherwise JavaScript will throw a TypeError.
      return !descr || !!descr.configurable;
    },
    ownKeys(jsobj) {
      let ptrobj = _getPtr(jsobj);
      let idresult;
      try {
        Py_ENTER();
        idresult = Module.__pyproxy_ownKeys(ptrobj);
        Py_EXIT();
      } catch (e) {
        API.fatal_error(e);
      }
      if (idresult === 0) {
        Module._pythonexc2js();
      }
      let result = Hiwire.pop_value(idresult);
      result.push(...Reflect.ownKeys(jsobj));
      return result;
    },
    apply(jsobj, jsthis, jsargs) {
      return jsobj.apply(jsthis, jsargs);
    },
  };

  function python_hasattr(jsobj, jskey) {
    let ptrobj = _getPtr(jsobj);
    let idkey = Hiwire.new_value(jskey);
    let result;
    try {
      Py_ENTER();
      result = Module.__pyproxy_hasattr(ptrobj, idkey);
      Py_EXIT();
    } catch (e) {
      API.fatal_error(e);
    } finally {
      Hiwire.decref(idkey);
    }
    if (result === -1) {
      Module._pythonexc2js();
    }
    return result !== 0;
  }

  function python_getattr(jsobj, jskey) {
    let ptrobj = _getPtr(jsobj);
    let idkey = Hiwire.new_value(jskey);
    let idresult;
    let cacheId = jsobj.$$.cache.cacheId;
    try {
      Py_ENTER();
      idresult = Module.__pyproxy_getattr(ptrobj, idkey, cacheId);
      Py_EXIT();
    } catch (e) {
      API.fatal_error(e);
    } finally {
      Hiwire.decref(idkey);
    }
    // if (idresult === 0) {
    //   if (Module._PyErr_Occurred()) {
    //     Module._pythonexc2js();
    //   }
    // }
    return idresult;
  }

  function python_setattr(jsobj, jskey, jsval) {
    let ptrobj = _getPtr(jsobj);
    let idkey = Hiwire.new_value(jskey);
    let idval = Hiwire.new_value(jsval);
    let errcode;
    try {
      Py_ENTER();
      errcode = Module.__pyproxy_setattr(ptrobj, idkey, idval);
      Py_EXIT();
    } catch (e) {
      API.fatal_error(e);
    } finally {
      Hiwire.decref(idkey);
      Hiwire.decref(idval);
    }
    if (errcode === -1) {
      Module._pythonexc2js();
    }
  }

  function python_delattr(jsobj, jskey) {
    let ptrobj = _getPtr(jsobj);
    let idkey = Hiwire.new_value(jskey);
    let errcode;
    try {
      Py_ENTER();
      errcode = Module.__pyproxy_delattr(ptrobj, idkey);
      Py_EXIT();
    } catch (e) {
      API.fatal_error(e);
    } finally {
      Hiwire.decref(idkey);
    }
    if (errcode === -1) {
      Module._pythonexc2js();
    }
  }
});
