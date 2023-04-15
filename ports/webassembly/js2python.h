#ifndef JS2PYTHON_H
#define JS2PYTHON_H

/**
 * Translate JavaScript objects to Python objects.
 */
#include "py/obj.h"
#include "hiwire.h"

/**
 * Convert a JavaScript object to a Python object.
 *  \param x The JavaScript object.
 *  \return The Python object resulting from the conversion. Returns NULL and
 *    sets the Python error indicator if a conversion error occurs.
 */
mp_obj_t
js2python(JsRef x);

mp_obj_t
js2python_immutable(JsRef x);

mp_obj_t
js2python_convert(JsRef x, int depth, JsRef defaultConverter);

/** Initialize any global variables used by this module. */
int
js2python_init();

#endif /* JS2PYTHON_H */
