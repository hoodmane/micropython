const Hiwire = {};
const Tests = {};
const API = {};
Module.hiwire = Hiwire;
API.fatal_error = function (e) {
  console.warn("fatal error!");
  throw e;
};
Module.api = API;
