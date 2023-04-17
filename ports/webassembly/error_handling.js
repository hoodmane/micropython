class PythonError extends Error {}

function setName(errClass) {
    Object.defineProperty(errClass.prototype, "name", {
        value: errClass.name,
    });
}
setName(PythonError);

function throw_if_error(){
    const python_tb = API.python_tb;
    API.python_tb = undefined;
    if (python_tb) {
      throw new PythonError(python_tb);
    }
}
API.throw_if_error = throw_if_error;

API.handle_js_error = function(e) {
    const name_utf8 = stringToNewUTF8(e.name);
    const message_utf8 = stringToNewUTF8(e.message);
    const stack_utf8 = stringToNewUTF8(e.stack);
    try {
        _raise_js_exception(name_utf8, message_utf8, stack_utf8);
    } finally {
        _free(name_utf8);
        _free(message_utf8);
        _free(stack_utf8);
    }
};
