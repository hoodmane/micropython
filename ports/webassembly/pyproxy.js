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
      API.fatal_error(e);
      return;
    } finally {
      Hiwire.decref(idargs);
    }
    throw_if_error();
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

  Module.pyproxy_destroy = function (
    proxy,
    destroyed_msg,
    destroy_roundtrip,
  ) {
    if (proxy.$$.ptr === 0) {
      return;
    }
    if (!destroy_roundtrip && proxy.$$props.roundtrip) {
      return;
    }
    let ptrobj = _getPtr(proxy);
    // Module.finalizationRegistry.unregister(proxy.$$);
    destroyed_msg = destroyed_msg || "Object has already been destroyed";
    let proxy_type = proxy.type;
    let proxy_repr;
    try {
      proxy_repr = proxy.toString();
    } catch (e) {
      if (e.pyodide_fatal_error) {
        throw e;
      }
    }
    // Maybe the destructor will call JavaScript code that will somehow try
    // to use this proxy. Mark it deleted before decrementing reference count
    // just in case!
    proxy.$$.ptr = 0;
    _pyproxy_decref(ptrobj);
    destroyed_msg += "\n" + `The object was of type "${proxy_type}" and `;
    if (proxy_repr) {
      destroyed_msg += `had repr "${proxy_repr}"`;
    } else {
      destroyed_msg += "an error was raised when trying to generate its repr";
    }
    proxy.$$.destroyed_msg = destroyed_msg;
    // pyproxy_decref_cache(proxy.$$.cache);
    // try {
    //   Py_ENTER();
    //   Module._Py_DecRef(ptrobj);
    //   trace_pyproxy_dealloc(proxy);
    //   Py_EXIT();
    // } catch (e) {
    //   API.fatal_error(e);
    // }
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
      throw_if_error();
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
  }
  let pyproxyClassMap = new Map();
  /**
   * Retrieve the appropriate mixins based on the features requested in flags.
   * Used by pyproxy_new. The "flags" variable is produced by the C function
   * pyproxy_getflags. Multiple PyProxies with the same set of feature flags
   * will share the same prototype, so the memory footprint of each individual
   * PyProxy is minimal.
   * @private
   */
  function getPyProxyClass(flags) {
    const FLAG_TYPE_PAIRS = [
      // [HAS_LENGTH, PyLengthMethods],
      [HAS_SUBSCR, PySubscriptMethods],
      [HAS_CONTAINS, PyContainsMethods],
      // [IS_ITERABLE, PyIterableMethods],
      // [IS_ITERATOR, PyIteratorMethods],
      // [IS_GENERATOR, PyGeneratorMethods],
      // [IS_ASYNC_ITERABLE, PyAsyncIterableMethods],
      // [IS_ASYNC_ITERATOR, PyAsyncIteratorMethods],
      // [IS_ASYNC_GENERATOR, PyAsyncGeneratorMethods],
      // [IS_AWAITABLE, PyAwaitableMethods],
      // [IS_BUFFER, PyBufferMethods],
      [IS_CALLABLE, PyCallableMethods],
    ];
    let result = pyproxyClassMap.get(flags);
    if (result) {
      return result;
    }
    let descriptors = {};
    for (let [feature_flag, methods] of FLAG_TYPE_PAIRS) {
      if (flags & feature_flag) {
        Object.assign(
          descriptors,
          Object.getOwnPropertyDescriptors(methods.prototype)
        );
      }
    }
    // Use base constructor (just throws an error if construction is attempted).
    descriptors.constructor = Object.getOwnPropertyDescriptor(
      PyProxy.prototype,
      "constructor"
    );
    Object.assign(
      descriptors,
      Object.getOwnPropertyDescriptors({ $$flags: flags })
    );
    let new_proto = Object.create(PyProxy.prototype, descriptors);
    function NewPyProxyClass() {}
    NewPyProxyClass.prototype = new_proto;
    pyproxyClassMap.set(flags, NewPyProxyClass);
    return NewPyProxyClass;
  }

  // Controlled by HAS_SUBSCR
  class PySubscriptMethods {
    /**
     * This translates to the Python code ``obj[key]``.
     *
     * @param key The key to look up.
     * @returns The corresponding value.
     */
    get(key) {
      let ptrobj = _getPtr(this);
      let idkey = Hiwire.new_value(key);
      let idresult;
      try {
        Py_ENTER();
        idresult = Module.__pyproxy_getitem(ptrobj, idkey);
        Py_EXIT();
      } catch (e) {
        API.fatal_error(e);
      } finally {
        Hiwire.decref(idkey);
      }
      throw_if_error();
      return Hiwire.pop_value(idresult);
    }

    /**
     * This translates to the Python code ``obj[key] = value``.
     *
     * @param key The key to set.
     * @param value The value to set it to.
     */
    set(key, value) {
      let ptrobj = _getPtr(this);
      let idkey = Hiwire.new_value(key);
      let idval = Hiwire.new_value(value);
      let errcode;
      try {
        Py_ENTER();
        errcode = Module.__pyproxy_setitem(ptrobj, idkey, idval);
        Py_EXIT();
      } catch (e) {
        API.fatal_error(e);
      } finally {
        Hiwire.decref(idkey);
        Hiwire.decref(idval);
      }
      throw_if_error();
    }

    /**
     * This translates to the Python code ``del obj[key]``.
     *
     * @param key The key to delete.
     */
    delete(key) {
      let ptrobj = _getPtr(this);
      let idkey = Hiwire.new_value(key);
      let errcode;
      try {
        Py_ENTER();
        errcode = Module.__pyproxy_delitem(ptrobj, idkey);
        Py_EXIT();
      } catch (e) {
        API.fatal_error(e);
      } finally {
        Hiwire.decref(idkey);
      }
      throw_if_error();
    }
  }

  // Controlled by HAS_CONTAINS flag, appears for any class with __contains__ or
  // sq_contains
  class PyContainsMethods {
    /**
     * This translates to the Python code ``key in obj``.
     *
     * @param key The key to check for.
     * @returns Is ``key`` present?
     */
    has(key) {
      let ptrobj = _getPtr(this);
      let idkey = Hiwire.new_value(key);
      let result;
      try {
        Py_ENTER();
        result = Module.__pyproxy_contains(ptrobj, idkey);
        Py_EXIT();
      } catch (e) {
        API.fatal_error(e);
      } finally {
        Hiwire.decref(idkey);
      }
      throw_if_error();
      return result === 1;
    }
  }

  class PyCallableMethods {
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

    callKwargs(...jsargs) {
      if (jsargs.length === 0) {
        throw new TypeError(
          "callKwargs requires at least one argument (the key word argument object)"
        );
      }
      let kwargs = jsargs.pop();
      if (
        kwargs.constructor !== undefined &&
        kwargs.constructor.name !== "Object"
      ) {
        throw new TypeError("kwargs argument is not an object");
      }
      return Module.callPyObjectKwargs(_getPtr(this), jsargs, kwargs);
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
    const flags = Module._pyproxy_getflags(ptrobj);
    const cls = getPyProxyClass(flags);
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
    throw_if_error();
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
