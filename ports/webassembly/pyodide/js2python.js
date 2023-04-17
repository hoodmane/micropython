JS_FILE(js2python_init_js, () => {
  0, 0; /* Magic, see include_js_file.h */
  let PropagateError = Module._PropagatePythonError;
  function js2python_string(jsString) {
    const length = lengthBytesUTF8(jsString) + 1;
    const cString = _malloc(length);
    stringToUTF8(jsString, cString, length);
    return _mp_obj_new_str(cString, length - 1);
  }
  Module.js2python_string = js2python_string;

  function js2python_bigint(value) {
    let value_orig = value;
    let length = 0;
    if (value < 0) {
      value = -value;
    }
    value <<= BigInt(1);
    while (value) {
      length++;
      value >>= BigInt(32);
    }
    let stackTop = stackSave();
    let ptr = stackAlloc(length * 4);
    value = value_orig;
    for (let i = 0; i < length; i++) {
      ASSIGN_U32(ptr, i, Number(value & BigInt(0xffffffff)));
      value >>= BigInt(32);
    }
    let result = _mp_obj_int_from_bytes_impl(
      false /* little endian */,
      length * 4 /* length in bytes */,
      ptr
    );
    stackRestore(stackTop);
    return result;
  }

  /**
   * This function converts immutable types. numbers, bigints, strings,
   * booleans, undefined, and null are converted. PyProxies are unwrapped.
   *
   * If `value` is of any other type then `undefined` is returned.
   *
   * If `value` is one of those types but an error is raised during conversion,
   * we throw a PropagateError to propagate the error out to C. This causes
   * special handling in the EM_JS wrapper.
   */
  function js2python_convertImmutable(value, id) {
    let result = js2python_convertImmutableInner(value, id);
    if (result === 0) {
      throw new PropagateError();
    }
    return result;
  }
  // js2python_convertImmutable is used from js2python.c so we need to add it
  // to Module.
  Module.js2python_convertImmutable = js2python_convertImmutable;

  /**
   * Returns a pointer to a Python object, 0, or undefined.
   *
   * If we return 0 it means we tried to convert but an error occurred, if we
   * return undefined, no conversion was attempted.
   */
  function js2python_convertImmutableInner(value, id) {
    let type = typeof value;
    if (type === "string") {
      return js2python_string(value);
    } else if (type === "number") {
      if (Number.isSafeInteger(value)) {
        return _mp_obj_new_int(value);
      } else {
        return _mp_obj_new_float(value);
      }
    } else if (type === "bigint") {
      return js2python_bigint(value);
    } else if (value === undefined || value === null) {
      return __js2python_none();
    } else if (value === true) {
      return __js2python_true();
    } else if (value === false) {
      return __js2python_false();
    } else if (API.isPyProxy(value)) {
      if (value.$$.ptr == 0) {
        // Make sure to throw an error!
        Module.PyProxy_getPtr(value);
      }
      if (value.$$props.roundtrip) {
        if (id === undefined) {
          id = Hiwire.new_value(value);
        }
        return _JsProxy_create(id);
      } else {
        return Module.PyProxy_getPtr(value);
      }
    }
    return undefined;
  }
});
