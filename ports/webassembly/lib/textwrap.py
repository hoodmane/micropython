# micropython regex are kinda useless so we implement in JavaScript
import pyodide_js

dedent = pyodide_js._module.API.dedent
