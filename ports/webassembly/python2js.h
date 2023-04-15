#ifndef PYTHON2JS_H
#define PYTHON2JS_H

/** Translate Python objects to JavaScript.
 */
#include "py/obj.h"
#include "hiwire.h"

/**
 * Do a shallow conversion from python to JavaScript. Convert immutable types
 * with equivalent JavaScript immutable types, but all other types are proxied.
 */
JsRef
python2js(mp_obj_t x);

/**
 * Like python2js except in the handling of PyProxy creation.
 *
 * If proxies is NULL, will throw an error instead of creating a PyProxy.
 * Otherwise, proxies should be an Array and python2js_track_proxies will add
 * the proxy to the array if one is created.
 */
JsRef
python2js_track_proxies(mp_obj_t x, JsRef proxies);

/**
 * Convert a Python object to a JavaScript object, copying standard collections
 * into javascript down to specified depth
 * \param x The Python object
 * \param depth The maximum depth to copy
 * \param proxies If this is Null, will raise an error if we have no JavaScript
 *        conversion for a Python object. If not NULL, should be a JavaScript
 *        list. We will add all PyProxies created to the list.
 * \return The JavaScript object -- might be an Error object in the case of an
 *         exception.
 */
JsRef
python2js_with_depth(mp_obj_t x, int depth, JsRef proxies);

/**
 * dict_converter should be a JavaScript function that converts an Iterable of
 * pairs into the desired JavaScript object. If dict_converter is NULL, we use
 * python2js_with_depth which converts dicts to Map (the default)
 */
JsRef
python2js_custom(mp_obj_t x,
                 int depth,
                 JsRef proxies,
                 JsRef dict_converter,
                 JsRef default_converter);

int
python2js_init(mp_obj_t core);

#endif /* PYTHON2JS_H */
