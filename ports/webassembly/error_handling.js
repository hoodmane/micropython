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
